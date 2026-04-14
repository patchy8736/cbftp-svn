#pragma once

#include "../uiwindow.h"
#include "../menuselectoption.h"

#include "../../mirrormanager.h"

class Site;

class EditMirrorJobScreen : public UIWindow {
public:
  EditMirrorJobScreen(Ui* ui);
  ~EditMirrorJobScreen();
  void initialize(unsigned int row, unsigned int col, int id = -1);
  void redraw() override;
  bool keyPressed(unsigned int ch) override;
  void command(const std::string& command, const std::string& arg) override;
  std::string getInfoLabel() const override;
private:
  enum class Mode {
    ADD,
    EDIT
  };
  void done();
  std::list<std::shared_ptr<Site> > getSelectedSpreadSiteObjects() const;
  std::string spreadSitesSummary() const;
  std::string sectionsSummary() const;
  std::string sectionLocalPathsSummary() const;
  std::list<std::string> parseCsv(const std::string& csv) const;
  enum class SelectionMode {
    NONE,
    SPREAD_SITES,
    SECTIONS,
    SECTION_LOCAL_PATHS
  };
  MirrorJob job;
  int oldid;
  Mode mode;
  SelectionMode selectionmode;
  MenuSelectOption mso;
};
