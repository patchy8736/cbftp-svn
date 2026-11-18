#include "transferpath.h"

namespace transferpath {

Path decide(bool source_inmemory, bool target_inmemory, long long file_size) {
  if (source_inmemory || target_inmemory) {
    if (file_size > kPerFileCap) {
      return Path::Oversize;
    }
    return Path::InMemory;
  }
  return Path::FXP;
}

}
