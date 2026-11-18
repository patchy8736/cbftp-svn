#include <cassert>

#include "../src/transferpath.h"

int main() {
  using transferpath::Path;

  // either-side rule: either site flagged -> in-memory transfer, one flag is enough
  assert(transferpath::decide(false, true, 1024) == Path::InMemory);
  assert(transferpath::decide(true, false, 1024) == Path::InMemory);
  // both flagged is not a special case: same in-memory path
  assert(transferpath::decide(true, true, 1024) == Path::InMemory);
  // both unflagged -> normal FXP, regardless of file size
  assert(transferpath::decide(false, false, 1024) == Path::FXP);
  assert(transferpath::decide(false, false, 10LL * 1024 * 1024 * 1024) == Path::FXP); // 10 GiB, still FXP

  // per-file cap: flagged pair, file at the cap -> in-memory
  assert(transferpath::decide(true, false, transferpath::kPerFileCap) == Path::InMemory);
  assert(transferpath::decide(false, true, transferpath::kPerFileCap) == Path::InMemory);
  // one byte above the cap -> oversize failure, no disk fallback
  assert(transferpath::decide(true, false, transferpath::kPerFileCap + 1) == Path::Oversize);
  assert(transferpath::decide(false, true, 5LL * 1024 * 1024 * 1024) == Path::Oversize);
  // cap applies on both-flagged too
  assert(transferpath::decide(true, true, 6LL * 1024 * 1024 * 1024) == Path::Oversize);
  return 0;
}
