#pragma once

#include <list>
#include <string>
#include <unordered_map>

#include "../uiwindow.h"
#include "../menuselectoption.h"

class MirrorSectionPathsScreen : public UIWindow {
public:
  MirrorSectionPathsScreen(Ui* ui);
  ~MirrorSectionPathsScreen();
  void initialize(unsigned int row, unsigned int col, const std::list<std::string>& sections,
                  const std::unordered_map<std::string, std::string>& sectionlocalpaths);
  void redraw() override;
  bool keyPressed(unsigned int ch) override;
  std::string getInfoLabel() const override;
private:
  void done();
  std::list<std::string> sections;
  MenuSelectOption mso;
};
