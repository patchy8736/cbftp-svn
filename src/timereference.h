#pragma once

#include "core/eventreceiver.h"

class TimeReference : public Core::EventReceiver {
public:
  TimeReference();
  void tick(int);
  unsigned long long timeReference() const;
  unsigned long long timePassedSince(unsigned long long) const;
  std::string getCurrentFullTimeStamp() const;
  std::string getCurrentLogTimeStamp() const;
  bool getTimeStampMilliseconds() const;
  void setTimeStampMilliseconds(bool ms);
private:
  std::string getCurrentTimeStamp(bool includedate) const;
  unsigned long long timeticker;
  bool timestampms;

public:
  static void updateTime();
  static int currentYear();
  static int currentMonth();
  static int currentDay();
};
