#pragma once

#include <string>
#include <vector>

// Pure race-math helper: computes per-site race outcomes from listing data.
// No FTP, engine, or connection dependencies.

namespace racemath {

struct ListingEntry {
  std::string name;
  std::string owner;
  std::string lastmodified; // raw listing time field: "MMM DD HH:MM" (recent) or "MMM DD  YYYY" (older)
};

struct RaceWinSummary {
  unsigned int files_total;
  unsigned int files_won;
  unsigned int files_lost;
  long long local_completion_seconds;
  bool race_won;
};

// Parses a listing mtime to comparable epoch seconds. Recent form
// ("MMM DD HH:MM") is assumed to fall in the given year; older form
// ("MMM DD  YYYY") carries its own year. Returns -1 if unparseable.
long long parseMTime(const std::string& mtime, int year);

// Per-site race outcome. Entries owned by our username count as wins;
// competitor entries dated within [race_start, window_end] count as losses.
// The race is won when we reached local completion earlier than the
// latest competitor file in the window (trivially won if there is none).
// completion_time <= 0 means local completion was not reached (live reads:
// pass window_end = now, completion_time = 0).
RaceWinSummary winSummary(const std::vector<ListingEntry>& entries, const std::string& username,
    long long race_start, long long completion_time, long long window_end, unsigned int expected_files);

// True when the destination site is missing exactly one release file.
bool lastFileBoost(unsigned int remaining_files);

// Adds the bonus to the score when the boost applies: race profile with the
// feature enabled and the destination missing exactly one release file.
unsigned int applyLastFileBoost(unsigned int score, unsigned int bonus, bool enabled, unsigned int remaining_files);

}
