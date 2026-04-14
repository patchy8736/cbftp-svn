#include "mirrorjobsscreen.h"

#include "../ui.h"
#include "../menuselectoptionelement.h"
#include "../menuselectoptiontextbutton.h"

#include "../../globalcontext.h"
#include "../../mirrormanager.h"

namespace {

enum KeyAction {
  KEYACTION_ADD_MIRROR_JOB,
  KEYACTION_ENABLE_DISABLE,
  KEYACTION_SCAN_NOW
};

}

MirrorJobsScreen::MirrorJobsScreen(Ui* ui) : UIWindow(ui, "MirrorJobsScreen"), mso(*vv), deletejobid(-1) {
  keybinds.addBind(10, KEYACTION_ENTER, "Edit mirror job");
  keybinds.addBind(KEY_DOWN, KEYACTION_DOWN, "Next option");
  keybinds.addBind(KEY_UP, KEYACTION_UP, "Previous option");
  keybinds.addBind('A', KEYACTION_ADD_MIRROR_JOB, "Add mirror job");
  keybinds.addBind('E', KEYACTION_ENABLE_DISABLE, "Enable/disable");
  keybinds.addBind('S', KEYACTION_SCAN_NOW, "Scan now");
  keybinds.addBind(KEY_DC, KEYACTION_DELETE, "Delete mirror job");
  keybinds.addBind('d', KEYACTION_DONE, "Done");
  keybinds.addBind('c', KEYACTION_BACK_CANCEL, "Cancel");
}

MirrorJobsScreen::~MirrorJobsScreen() {
}

void MirrorJobsScreen::initialize(unsigned int row, unsigned int col) {
  deletejobid = -1;
  mso.reset();
  mso.enterFocusFrom(0);
  init(row, col);
}

void MirrorJobsScreen::redraw() {
  vv->clear();
  mso.clear();
  unsigned int y = 1;
  mso.addTextButtonNoContent(y++, 1, "add", "Add mirror job...");
  vv->putStr(y++, 1, "EN NAME                 MONITOR         TARGET          SECTIONS   SPREAD   LAST SCAN");
  for (std::map<int, MirrorJob>::const_iterator it = global->getMirrorManager()->getJobs().begin();
      it != global->getMirrorManager()->getJobs().end(); ++it)
  {
    const MirrorJob& job = it->second;
    std::string line = std::string(job.enabled ? "[X] " : "[ ] ") +
        job.name + " | " + job.monitorsite + " -> " + job.targetsite +
        " | sec:" + std::to_string(job.sections.size()) +
        " spread:" + std::to_string(job.spreadsites.size()) +
        " last:" + std::to_string(job.lastscanepoch);
    mso.addTextButtonNoContent(y++, 1, std::to_string(job.id), line);
  }
  mso.checkPointer();
  bool highlight;
  for (unsigned int i = 0; i < mso.size(); i++) {
    std::shared_ptr<MenuSelectOptionElement> elem = mso.getElement(i);
    highlight = mso.isFocused() && mso.getSelectionPointer() == i;
    vv->putStr(elem->getRow(), elem->getCol(), elem->getLabelText(), highlight);
  }
}

int MirrorJobsScreen::getSelectedJobId() const {
  std::shared_ptr<MenuSelectOptionElement> selected = mso.getElement(mso.getSelectionPointer());
  if (!selected) {
    return -1;
  }
  if (selected->getIdentifier() == "add") {
    return -1;
  }
  return std::stoi(selected->getIdentifier());
}

bool MirrorJobsScreen::keyPressed(unsigned int ch) {
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
      int id = getSelectedJobId();
      if (id == -1) {
        ui->goAddMirrorJob();
      }
      else {
        ui->goEditMirrorJob(id);
      }
      return true;
    }
    case KEYACTION_ADD_MIRROR_JOB:
      ui->goAddMirrorJob();
      return true;
    case KEYACTION_ENABLE_DISABLE: {
      int id = getSelectedJobId();
      if (id != -1) {
        MirrorJob* job = global->getMirrorManager()->getJob(id);
        if (job) {
          global->getMirrorManager()->setEnabled(id, !job->enabled);
          ui->redraw();
        }
      }
      return true;
    }
    case KEYACTION_SCAN_NOW: {
      int id = getSelectedJobId();
      if (id != -1) {
        global->getMirrorManager()->scanNow(id, true);
        ui->redraw();
      }
      return true;
    }
    case KEYACTION_DELETE: {
      int id = getSelectedJobId();
      if (id != -1) {
        deletejobid = id;
        ui->goConfirmation("Delete mirror job " + std::to_string(id) + "?");
      }
      return true;
    }
    case KEYACTION_DONE:
    case KEYACTION_BACK_CANCEL:
      ui->returnToLast();
      return true;
  }
  return false;
}

void MirrorJobsScreen::command(const std::string& command) {
  if (command == "yes" && deletejobid != -1) {
    global->getMirrorManager()->removeJob(deletejobid);
    deletejobid = -1;
    ui->redraw();
  }
}

std::string MirrorJobsScreen::getInfoLabel() const {
  return "MIRROR JOBS";
}

std::string MirrorJobsScreen::getInfoText() const {
  return "Total: " + std::to_string(global->getMirrorManager()->getJobs().size());
}
