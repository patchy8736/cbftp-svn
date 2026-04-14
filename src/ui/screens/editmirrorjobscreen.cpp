#include "editmirrorjobscreen.h"

#include "../ui.h"
#include "../menuselectoptioncheckbox.h"
#include "../menuselectoptionelement.h"
#include "../menuselectoptiontextfield.h"
#include "../menuselectoptiontextarrow.h"

#include "../../globalcontext.h"
#include "../../sitemanager.h"
#include "../../site.h"
#include "../../util.h"

EditMirrorJobScreen::EditMirrorJobScreen(Ui* ui) : UIWindow(ui, "EditMirrorJobScreen"), oldid(-1), mode(Mode::ADD), selectionmode(SelectionMode::NONE), mso(*vv) {
  keybinds.addBind(10, KEYACTION_ENTER, "Modify");
  keybinds.addBind(KEY_DOWN, KEYACTION_DOWN, "Next option");
  keybinds.addBind(KEY_UP, KEYACTION_UP, "Previous option");
  keybinds.addBind('d', KEYACTION_DONE, "Done");
  keybinds.addBind('c', KEYACTION_BACK_CANCEL, "Cancel");
}

EditMirrorJobScreen::~EditMirrorJobScreen() {
}

void EditMirrorJobScreen::initialize(unsigned int row, unsigned int col, int id) {
  selectionmode = SelectionMode::NONE;
  oldid = id;
  if (id < 0) {
    mode = Mode::ADD;
    job = MirrorJob();
    job.name = "mirror-" + std::to_string(global->getMirrorManager()->getJobs().size() + 1);
  }
  else {
    mode = Mode::EDIT;
    MirrorJob* existing = global->getMirrorManager()->getJob(id);
    if (existing) {
      job = *existing;
    }
  }

  mso.reset();
  unsigned int y = 1;
  mso.addStringField(y++, 1, "name", "Name:", job.name, false, 48);
  mso.addCheckBox(y++, 1, "enabled", "Enabled:", job.enabled);

  std::shared_ptr<MenuSelectOptionTextArrow> monitor = mso.addTextArrow(y++, 1, "monitor_site", "Monitor site:");
  std::shared_ptr<MenuSelectOptionTextArrow> target = mso.addTextArrow(y++, 1, "target_site", "Target site:");
  for (std::vector<std::shared_ptr<Site> >::const_iterator it = global->getSiteManager()->begin(); it != global->getSiteManager()->end(); ++it) {
    monitor->addOption((*it)->getName(), 1);
    target->addOption((*it)->getName(), 1);
  }
  monitor->setOptionText(job.monitorsite);
  target->setOptionText(job.targetsite);

  mso.addTextButtonNoContent(y++, 1, "spread_sites", "Spread sites...");
  mso.addTextButtonNoContent(y++, 1, "sections", "Sections...");

  std::shared_ptr<MenuSelectOptionTextArrow> profile = mso.addTextArrow(y++, 1, "profile", "Profile:");
  profile->addOption("Distribute", static_cast<int>(MirrorProfile::DISTRIBUTE));
  profile->addOption("Race", static_cast<int>(MirrorProfile::RACE));
  profile->setOption(static_cast<int>(job.profile));

  mso.addStringField(y++, 1, "poll_interval_seconds", "Poll interval (s):", std::to_string(job.pollintervalseconds), false, 8);
  mso.addStringField(y++, 1, "release_name_pattern", "Release pattern:", job.releasenamepattern, false, 48);
  mso.addStringField(y++, 1, "min_release_age_seconds", "Min release age (s):", std::to_string(job.minreleaseageseconds), false, 8);
  mso.addCheckBox(y++, 1, "create_done_file", "Create done.file:", job.createdonefile);
  mso.addCheckBox(y++, 1, "send_rpu_webhook", "Send RPU webhook:", job.sendrpuwebhook);
  mso.addStringField(y++, 1, "rpu_webhook_url", "RPU webhook URL:", job.rpuwebhookurl, false, 72);
  mso.addTextButtonNoContent(y++, 1, "section_local_paths", "Section local paths...");
  mso.enterFocusFrom(0);
  init(row, col);
}

void EditMirrorJobScreen::redraw() {
  vv->clear();
  mso.getElement("spread_sites")->setLabel("Spread sites: " + spreadSitesSummary());
  mso.getElement("sections")->setLabel("Sections: " + sectionsSummary());
  mso.getElement("section_local_paths")->setLabel("Section local paths: " + sectionLocalPathsSummary());
  bool highlight;
  for (unsigned int i = 0; i < mso.size(); i++) {
    std::shared_ptr<MenuSelectOptionElement> elem = mso.getElement(i);
    highlight = mso.isFocused() && mso.getSelectionPointer() == i;
    vv->putStr(elem->getRow(), elem->getCol(), elem->getLabelText(), highlight);
    vv->putStr(elem->getRow(), elem->getCol() + elem->getLabelText().length() + 1, elem->getContentText());
  }
  std::shared_ptr<MenuSelectOptionElement> elem = mso.getElement(mso.getSelectionPointer());
  if (active && elem->cursorPosition() >= 0) {
    ui->showCursor();
    vv->moveCursor(elem->getRow(), elem->getCol() + elem->getLabelText().length() + 1 + elem->cursorPosition());
  }
  else {
    ui->hideCursor();
  }
}

bool EditMirrorJobScreen::keyPressed(unsigned int ch) {
  int action = keybinds.getKeyAction(ch);
  switch (action) {
    case KEYACTION_UP:
      if (mso.goUp()) {
        ui->update();
      }
      return true;
    case KEYACTION_DOWN:
      if (mso.goDown()) {
        ui->update();
      }
      return true;
    case KEYACTION_ENTER: {
      activeelement = mso.getElement(mso.getSelectionPointer());
      bool activation = activeelement->activate();
      if (!activation) {
        if (activeelement->getIdentifier() == "spread_sites") {
          selectionmode = SelectionMode::SPREAD_SITES;
          std::shared_ptr<Site> monitor = global->getSiteManager()->getSite(job.monitorsite);
          std::shared_ptr<Site> target = global->getSiteManager()->getSite(job.targetsite);
          std::list<std::shared_ptr<Site> > excluded;
          if (monitor) {
            excluded.push_back(monitor);
          }
          if (target) {
            excluded.push_back(target);
          }
          ui->goSelectSites("Select additional source sites (monitor is auto-included)", getSelectedSpreadSiteObjects(), excluded);
          return true;
        }
        if (activeelement->getIdentifier() == "sections") {
          selectionmode = SelectionMode::SECTIONS;
          ui->goSelectSection(job.sections, std::list<std::string>());
          return true;
        }
        if (activeelement->getIdentifier() == "section_local_paths") {
          selectionmode = SelectionMode::SECTION_LOCAL_PATHS;
          ui->goMirrorSectionPaths(job.sections, job.sectionlocalpaths);
          return true;
        }
        ui->update();
        return true;
      }
      active = true;
      ui->setLegend();
      ui->update();
      return true;
    }
    case KEYACTION_DONE:
      done();
      return true;
    case KEYACTION_BACK_CANCEL:
      ui->returnToLast();
      return true;
  }
  return false;
}

void EditMirrorJobScreen::done() {
  std::string oldmonitor = job.monitorsite;
  for (unsigned int i = 0; i < mso.size(); i++) {
    std::shared_ptr<MenuSelectOptionElement> elem = mso.getElement(i);
    std::string id = elem->getIdentifier();
    if (id == "name") {
      job.name = std::static_pointer_cast<MenuSelectOptionTextField>(elem)->getData();
    }
    else if (id == "enabled") {
      job.enabled = std::static_pointer_cast<MenuSelectOptionCheckBox>(elem)->getData();
    }
    else if (id == "monitor_site") {
      job.monitorsite = std::static_pointer_cast<MenuSelectOptionTextArrow>(elem)->getDataText();
    }
    else if (id == "target_site") {
      job.targetsite = std::static_pointer_cast<MenuSelectOptionTextArrow>(elem)->getDataText();
    }
    else if (id == "profile") {
      job.profile = static_cast<MirrorProfile>(std::static_pointer_cast<MenuSelectOptionTextArrow>(elem)->getData());
    }
    else if (id == "poll_interval_seconds") {
      job.pollintervalseconds = std::stoi(std::static_pointer_cast<MenuSelectOptionTextField>(elem)->getData());
    }
    else if (id == "release_name_pattern") {
      job.releasenamepattern = std::static_pointer_cast<MenuSelectOptionTextField>(elem)->getData();
    }
    else if (id == "min_release_age_seconds") {
      job.minreleaseageseconds = std::stoi(std::static_pointer_cast<MenuSelectOptionTextField>(elem)->getData());
    }
    else if (id == "create_done_file") {
      job.createdonefile = std::static_pointer_cast<MenuSelectOptionCheckBox>(elem)->getData();
    }
    else if (id == "send_rpu_webhook") {
      job.sendrpuwebhook = std::static_pointer_cast<MenuSelectOptionCheckBox>(elem)->getData();
    }
    else if (id == "rpu_webhook_url") {
      job.rpuwebhookurl = std::static_pointer_cast<MenuSelectOptionTextField>(elem)->getData();
    }
  }
  job.monitorsite = util::trim(job.monitorsite);
  job.targetsite = util::trim(job.targetsite);
  std::list<std::string> newspread;
  for (const std::string& site : job.spreadsites) {
    std::string trimmed = util::trim(site);
    if (!trimmed.empty() && trimmed != job.monitorsite && trimmed != job.targetsite) {
      newspread.push_back(trimmed);
    }
  }
  if (!oldmonitor.empty() && oldmonitor != job.monitorsite && oldmonitor != job.targetsite) {
    bool hasold = false;
    for (const std::string& site : newspread) {
      if (site == oldmonitor) {
        hasold = true;
        break;
      }
    }
    if (!hasold) {
      newspread.push_back(oldmonitor);
    }
  }
  job.spreadsites = newspread;
  std::list<std::string> newsections;
  for (const std::string& section : job.sections) {
    std::string trimmed = util::trim(section);
    if (!trimmed.empty()) {
      newsections.push_back(trimmed);
    }
  }
  job.sections = newsections;
  util::Result result;
  if (mode == Mode::ADD) {
    result = global->getMirrorManager()->addJob(job);
  }
  else {
    result = global->getMirrorManager()->replaceJob(oldid, job);
  }
  if (!result.success) {
    ui->goInfo(result.error);
    return;
  }
  ui->returnToLast();
}

void EditMirrorJobScreen::command(const std::string& command, const std::string& arg) {
  if (command != "returnselectitems") {
    return;
  }
  if (selectionmode == SelectionMode::SPREAD_SITES) {
    job.spreadsites = parseCsv(arg);
  }
  else if (selectionmode == SelectionMode::SECTIONS) {
    job.sections = parseCsv(arg);
  }
  else if (selectionmode == SelectionMode::SECTION_LOCAL_PATHS) {
    job.sectionlocalpaths = MirrorManager::parseSectionLocalPaths(arg);
  }
  selectionmode = SelectionMode::NONE;
  ui->redraw();
}

std::list<std::shared_ptr<Site> > EditMirrorJobScreen::getSelectedSpreadSiteObjects() const {
  std::list<std::shared_ptr<Site> > out;
  for (const std::string& sitename : job.spreadsites) {
    std::shared_ptr<Site> site = global->getSiteManager()->getSite(util::trim(sitename));
    if (site && site->getName() != job.monitorsite && site->getName() != job.targetsite) {
      out.push_back(site);
    }
  }
  return out;
}

std::string EditMirrorJobScreen::spreadSitesSummary() const {
  std::list<std::string> display;
  if (!job.monitorsite.empty()) {
    display.push_back(job.monitorsite + "(auto)");
  }
  for (const std::string& site : job.spreadsites) {
    std::string trimmed = util::trim(site);
    if (!trimmed.empty() && trimmed != job.monitorsite && trimmed != job.targetsite) {
      display.push_back(trimmed);
    }
  }
  if (display.empty()) {
    return "(none)";
  }
  return util::join(display, ",");
}

std::string EditMirrorJobScreen::sectionsSummary() const {
  if (job.sections.empty()) {
    return "(none)";
  }
  return util::join(job.sections, ",");
}

std::string EditMirrorJobScreen::sectionLocalPathsSummary() const {
  if (job.sections.empty()) {
    return "(none)";
  }
  int mapped = 0;
  for (const std::string& section : job.sections) {
    std::unordered_map<std::string, std::string>::const_iterator it = job.sectionlocalpaths.find(section);
    if (it != job.sectionlocalpaths.end() && !util::trim(it->second).empty()) {
      ++mapped;
    }
  }
  return std::to_string(mapped) + "/" + std::to_string(job.sections.size()) + " mapped";
}

std::list<std::string> EditMirrorJobScreen::parseCsv(const std::string& csv) const {
  std::list<std::string> parsed = util::split(csv, ",");
  std::list<std::string> out;
  for (const std::string& item : parsed) {
    std::string trimmed = util::trim(item);
    if (!trimmed.empty()) {
      out.push_back(trimmed);
    }
  }
  return out;
}

std::string EditMirrorJobScreen::getInfoLabel() const {
  return mode == Mode::ADD ? "ADD MIRROR JOB" : "EDIT MIRROR JOB";
}
