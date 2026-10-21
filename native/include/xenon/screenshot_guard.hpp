#pragma once

#include "xenon/contracts.hpp"
#include <cstdint>
#include <vector>

namespace xenon::screenshot {
// Trusted native state only. No page strings or credential values escape the scan.
struct State {
  uint64_t epoch{}, human{}, frames{};
  bool valid=true, protected_auth=false, screen_sharing=false, guards_ready=true;
  std::vector<std::string> sessions;
};
using ReadState = std::function<State()>;
using ReadDocument = std::function<void(const std::string&, Reply)>;
using Checked = std::function<void(Json, State)>;

// One complete retry for a failed frame read; credentials remain non-retryable.
void check(ReadState read, ReadDocument document, Checked checked);
// Recheck the scanned identity before capture dispatch and pixel delivery.
Json validate(const State& scanned, const State& now, bool check_frames=true);
}
