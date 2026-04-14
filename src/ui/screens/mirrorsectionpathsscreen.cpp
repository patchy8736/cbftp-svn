#include "mirrorsectionpathsscreen.h"

#include "../ui.h"
#include "../menuselectoptionelement.h"
#include "../menuselectoptiontextfield.h"

#include "../../mirrormanager.h"
#include "../../util.h"

MirrorSectionPathsScreen::MirrorSectionPathsScreen(Ui* ui) : UIWindow(ui, "MirrorSectionPathsScreen"), mso(*vv) {
  keybinds.addBind(10, KEYACTION_ENTER, "Modify");
  keybinds.addBind(KEY_DOWN, KEYACTION_DOWN, "Next option");
  keybinds.addBind(KEY_UP, KEYACTION_UP, "Previous option");
  keybinds.addBind('d', KEYACTION_DONE, "Done");
  keybinds.addBind('c', KEYACTION_BACK_CANCEL, "Cancel");
}

MirrorSectionPathsScreen::~MirrorSectionPathsScreen() {
}

void MirrorSectionPathsScreen::initialize(unsigned int row, unsigned int col, const std::list<std::string>& sections,
                                          const std::unordered_map<std::string, std::string>& sectionlocalpaths)
{
  this->sections = sections;
  mso.reset();
  unsigned int y = 2;
  if (sections.empty()) {
    mso.addTextButtonNoContent(y++, 1, "no_sections", "No sections selected in mirror job.");
  }
  else {
    for (const std::string& section : sections) {
      std::unordered_map<std::string, std::string>::const_iterator it = sectionlocalpaths.find(section);
      std::string path = (it != sectionlocalpaths.end()) ? it->second : "";
      mso.addStringField(y++, 1, section, section + ":", path, false, 96, 4096);
    }
  }
  mso.enterFocusFrom(0);
  init(row, col);
}

void MirrorSectionPathsScreen::redraw() {
  vv->clear();
  vv->putStr(1, 1, "Set local category path per section. Empty path means unmapped.");
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

bool MirrorSectionPathsScreen::keyPressed(unsigned int ch) {
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

void MirrorSectionPathsScreen::done() {
  std::unordered_map<std::string, std::string> mapped;
  for (unsigned int i = 0; i < mso.size(); i++) {
    std::shared_ptr<MenuSelectOptionElement> elem = mso.getElement(i);
    std::string section = elem->getIdentifier();
    if (section == "no_sections") {
      continue;
    }
    std::string path = util::trim(std::static_pointer_cast<MenuSelectOptionTextField>(elem)->getData());
    if (!path.empty()) {
      mapped[section] = path;
    }
  }
  ui->returnSelectItems(MirrorManager::sectionLocalPathsToString(mapped));
}

std::string MirrorSectionPathsScreen::getInfoLabel() const {
  return "MIRROR SECTION LOCAL PATHS";
}
