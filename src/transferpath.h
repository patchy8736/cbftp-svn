#pragma once

// Pure transfer-path decision for site-to-site transfers: which route a
// transfer of one file takes. Plain bools + a file size in, enum out.
// No site classes, FTP logic, or globals. The either-side rule and the
// per-file size cap are encoded here and nowhere else.

namespace transferpath {

enum class Path {
  FXP,      // normal server-to-server FXP
  InMemory, // in-memory transfer: routed through client RAM, no disk
  Oversize, // failure: file exceeds the per-file cap, never falls back to disk
};

// Fixed 4 GiB per-file cap for in-memory transfers, in bytes.
const long long kPerFileCap = 4LL * 1024 * 1024 * 1024;

// Either-side rule: source or target flagged -> in-memory transfer;
// both unflagged -> FXP regardless of file size. A flagged pair with a
// file above the cap is an oversize failure.
Path decide(bool source_inmemory, bool target_inmemory, long long file_size);

}
