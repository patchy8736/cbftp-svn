#include <cassert>

#include "../src/racemath.h"

int main() {
  assert(racemath::lastFileBoost(1) == true);
  assert(racemath::lastFileBoost(0) == false);
  assert(racemath::lastFileBoost(2) == false);
  assert(racemath::lastFileBoost(10) == false);

  // recent form "MMM DD HH:MM", year supplied. Anchor: 2024-11-01 00:00 UTC = 1730419200
  assert(racemath::parseMTime("Nov 17 14:23", 2024) == 1731853380);
  // day is space-padded in listings
  assert(racemath::parseMTime("Nov  7 14:23", 2024) == 1730989380);
  // older date-with-year form: 2024-03-12 00:00 UTC (2024-01-01 = 1704067200, +71 days)
  assert(racemath::parseMTime("Mar 12  2024", 2025) == 1710201600);
  assert(racemath::parseMTime("Mar 12  2024", 1999) == 1710201600); // year arg ignored

  // win summary: anchor 2024-11-01 00:00 UTC = 1730419200, job start; completion 2024-11-17 14:23
  std::vector<racemath::ListingEntry> entries = {
    {"file1.rar", "myuser", "Nov 17 10:00"},
    {"file2.rar", "myuser", "Nov 17 11:00"},
    {"file3.rar", "rival1", "Nov 17 12:00"},
  };
  racemath::RaceWinSummary summary =
      racemath::winSummary(entries, "myuser", 1730419200, 1731853380, 1731853380, 3);
  assert(summary.files_total == 3);
  assert(summary.files_won == 2);
  assert(summary.files_lost == 1);
  assert(summary.local_completion_seconds == 1731853380);
  // rival's latest file (12:00) predates our completion (14:23) — they finished first
  assert(summary.race_won == false);

  // competitor file dated before the job start was never raced: not a loss, doesn't set the estimate
  std::vector<racemath::ListingEntry> prejob = {
    {"file1.rar", "myuser", "Nov 17 10:00"},
    {"file2.rar", "rival1", "Oct 15 09:00"},
  };
  racemath::RaceWinSummary s2 =
      racemath::winSummary(prejob, "myuser", 1730419200, 1731853380, 1731853380, 2);
  assert(s2.files_won == 1);
  assert(s2.files_lost == 0);
  assert(s2.race_won == true); // no competitor file in the window: trivially won

  // we complete before the rival's latest in-window file: won
  std::vector<racemath::ListingEntry> won = {
    {"file1.rar", "myuser", "Nov 17 10:00"},
    {"file2.rar", "rival1", "Nov 17 12:00"},
  };
  racemath::RaceWinSummary s3 =
      racemath::winSummary(won, "myuser", 1730419200, 1730419200 + 11 * 3600, 1730419200 + 11 * 3600, 2); // 11:00 completion
  assert(s3.race_won == true);

  // no local completion (completion_time 0): never won
  racemath::RaceWinSummary s4 =
      racemath::winSummary(won, "myuser", 1730419200, 0, 1731853380, 2);
  assert(s4.local_completion_seconds == 0);
  assert(s4.race_won == false);

  // boundaries: file dated exactly at race start counts; exactly at completion is a loss and beats us
  std::vector<racemath::ListingEntry> boundary = {
    {"file1.rar", "myuser", "Nov  1 00:00"},
    {"file2.rar", "rival1", "Nov  1 00:00"},
    {"file3.rar", "rival2", "Nov 17 14:23"},
  };
  racemath::RaceWinSummary s5 =
      racemath::winSummary(boundary, "myuser", 1730419200, 1731853380, 1731853380, 3);
  assert(s5.files_won == 1);
  assert(s5.files_lost == 2);
  assert(s5.race_won == false);

  // unparseable mtimes return -1
  assert(racemath::parseMTime("Nov 17", 2024) == -1); // wrong token count
  assert(racemath::parseMTime("Xyz 17 14:23", 2024) == -1); // unknown month
  assert(racemath::parseMTime("Nov 17 99:99", 2024) == -1); // out-of-range clock
  assert(racemath::parseMTime("Mar 12  1969", 2024) == -1); // year below epoch

  // recent-form year rollover: job starts Dec 31, rival file lands Jan 01 next year
  long long newyear_start = 1735687800; // 2024-12-31 23:30 UTC (2025-01-01 00:00 = 1735689600)
  std::vector<racemath::ListingEntry> rollover = {
    {"file1.rar", "myuser", "Dec 31 23:40"},
    {"file2.rar", "rival1", "Jan  1 00:10"},
  };
  racemath::RaceWinSummary s6 =
      racemath::winSummary(rollover, "myuser", newyear_start, newyear_start + 3600, newyear_start + 3600, 2);
  assert(s6.files_lost == 1); // rival file must parse into the window's year, not the start year
  assert(s6.race_won == false);

  // live job: window runs to now, no completion yet — losses counted, never won
  std::vector<racemath::ListingEntry> live = {
    {"file1.rar", "myuser", "Nov 17 10:00"},
    {"file2.rar", "rival1", "Nov 17 12:00"},
  };
  racemath::RaceWinSummary s7 =
      racemath::winSummary(live, "myuser", 1730419200, 0, 1731853380, 2);
  assert(s7.files_lost == 1);
  assert(s7.local_completion_seconds == 0);
  assert(s7.race_won == false);
  return 0;
}
