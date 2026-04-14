#pragma once

#include "../uiwindow.h"
#include "../menuselectoption.h"

class MirrorJobsScreen : public UIWindow {
public:
  MirrorJobsScreen(Ui* ui);
  ~MirrorJobsScreen();
  void initialize(unsigned int row, unsigned int col);
  void redraw() override;
  bool keyPressed(unsigned int ch) override;
  void command(const std::string& command) override;
  std::string getInfoLabel() const override;
  std::string getInfoText() const override;
private:
  int getSelectedJobId() const;
  MenuSelectOption mso;
  int deletejobid;
};
