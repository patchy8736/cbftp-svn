#include "racemath.h"

#include <cctype>
#include <map>

namespace {

long long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = y - era * 400;
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (long long)era * 146097 + (int)doe - 719468;
}

int yearOfEpoch(long long epoch) {
  long long days = epoch >= 0 ? epoch / 86400 : (epoch - 86399) / 86400;
  // inverse of daysFromCivil (Hinnant civil_from_days)
  days += 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = (unsigned)(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int y = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  return y + (m <= 2);
}

unsigned int parseMonth(const std::string& month) {
  static const std::map<std::string, unsigned int> months = {
    {"Jan", 1}, {"Feb", 2}, {"Mar", 3}, {"Apr", 4}, {"May", 5}, {"Jun", 6},
    {"Jul", 7}, {"Aug", 8}, {"Sep", 9}, {"Oct", 10}, {"Nov", 11}, {"Dec", 12}
  };
  std::map<std::string, unsigned int>::const_iterator it = months.find(month);
  return it == months.end() ? 0 : it->second;
}

std::vector<std::string> splitTokens(const std::string& mtime) {
  std::vector<std::string> tokens;
  size_t pos = 0;
  while (pos < mtime.length()) {
    while (pos < mtime.length() && isspace(mtime[pos])) {
      ++pos;
    }
    size_t start = pos;
    while (pos < mtime.length() && !isspace(mtime[pos])) {
      ++pos;
    }
    if (pos > start) {
      tokens.push_back(mtime.substr(start, pos - start));
    }
  }
  return tokens;
}

}

namespace racemath {

long long parseMTime(const std::string& mtime, int year) {
  std::vector<std::string> tokens = splitTokens(mtime);
  if (tokens.size() != 3) {
    return -1;
  }
  unsigned int month = parseMonth(tokens[0]);
  if (!month || !isdigit(tokens[1][0])) {
    return -1;
  }
  unsigned int day = (unsigned int)atoi(tokens[1].c_str());
  if (tokens[2].find(':') != std::string::npos) {
    // recent form HH:MM
    if (tokens[2].length() != 5 || tokens[2][2] != ':') {
      return -1;
    }
    unsigned int hour = (unsigned int)atoi(tokens[2].substr(0, 2).c_str());
    unsigned int minute = (unsigned int)atoi(tokens[2].substr(3).c_str());
    if (hour > 23 || minute > 59) {
      return -1;
    }
    return (daysFromCivil(year, month, day) * 86400) + hour * 3600 + minute * 60;
  }
  // older form carries its own year
  int mtimeyear = atoi(tokens[2].c_str());
  if (mtimeyear < 1970) {
    return -1;
  }
  return daysFromCivil(mtimeyear, month, day) * 86400;
}

bool lastFileBoost(unsigned int remaining_files) {
  return remaining_files == 1;
}

RaceWinSummary winSummary(const std::vector<ListingEntry>& entries, const std::string& username,
    long long race_start, long long completion_time, long long window_end, unsigned int expected_files) {
  RaceWinSummary summary;
  summary.files_total = expected_files;
  summary.files_won = 0;
  summary.files_lost = 0;
  summary.local_completion_seconds = completion_time;
  summary.race_won = false;
  int year = yearOfEpoch(race_start);
  long long competitor_latest = -1;
  for (std::vector<ListingEntry>::const_iterator it = entries.begin(); it != entries.end(); ++it) {
    if (it->owner == username) {
      ++summary.files_won;
      continue;
    }
    long long mtime = parseMTime(it->lastmodified, year);
    // shortcut: recent-form mtimes carry no year; a job spanning New Year needs the next year
    if (mtime < race_start) {
      mtime = parseMTime(it->lastmodified, year + 1);
    }
    if (mtime >= race_start && mtime <= window_end) {
      ++summary.files_lost;
      if (mtime > competitor_latest) {
        competitor_latest = mtime;
      }
    }
  }
  summary.race_won = completion_time > 0 &&
      (competitor_latest < 0 || completion_time < competitor_latest);
  return summary;
}

}
