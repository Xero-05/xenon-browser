#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <string>
namespace xenon {
// Frame pacing for tab pages. Every tab keeps rendering. A tab covered by the
// selected tab of its window paints at kIdleRate unless an agent operation is
// running in it or finished within kAgentLease, so agent input guards (which
// wait for animation frames) and fresh evidence never wait on a throttled
// frame. Pacing follows visibility and agent activity only, never tab
// ownership: a handoff commit leaves it unchanged, and the recipient's first
// operation restores full rate before any input or capture.
//
// Rates are passed to CefBrowserHost::SetWindowlessFrameRate. Xenon's engine
// patch applies them to windowed tabs; the stock engine ignores them.
class TabRenderPacing {
 public:
  using Clock = std::chrono::steady_clock;
  // At or above this rate the engine removes the throttle entirely.
  static constexpr int kFullRate = 60;
  static constexpr int kIdleRate = 1;
  static constexpr std::chrono::seconds kAgentLease{15};

  void set_covered(const std::string& tab, bool covered) { tabs_[tab].covered = covered; }
  void agent_started(const std::string& tab) { ++tabs_[tab].operations; }
  // Returns when the lease ends, for a timer that re-evaluates the tab.
  Clock::time_point agent_finished(const std::string& tab, Clock::time_point now) {
    auto& state = tabs_[tab];
    if (state.operations) --state.operations;
    state.lease_until = std::max(state.lease_until, now + kAgentLease);
    return state.lease_until;
  }
  void remove(const std::string& tab) { tabs_.erase(tab); }
  // Unknown tabs are never throttled.
  int rate(const std::string& tab, Clock::time_point now) const {
    const auto found = tabs_.find(tab);
    if (found == tabs_.end()) return kFullRate;
    const auto& state = found->second;
    return !state.covered || state.operations || now < state.lease_until ? kFullRate : kIdleRate;
  }

 private:
  struct State { bool covered = false; size_t operations = 0; Clock::time_point lease_until{}; };
  std::map<std::string, State> tabs_;
};
}  // namespace xenon
