#pragma once
#include "xenon/contracts.hpp"
#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <utility>

namespace xenon {
struct HumanDialogNotice {
  std::string tab_id, type, message, origin;
  bool protected_auth = false;
};

// Used only by the native UI. Ownership is read at delivery, not when a page
// creates its dialog. Pending page dialogs never grant ownership themselves.
class DialogNotices {
 public:
  void notify(const std::string& tab_id) {
    // A new OnJSDialog callback in the same tab may immediately follow an
    // answered dialog. It must be eligible for a new native notice.
    if (active_ == tab_id) active_.clear();
    if (!tab_id.empty() && pending_.size() < 1024 &&
        std::find(pending_.begin(), pending_.end(), tab_id) == pending_.end())
      pending_.push_back(tab_id);
  }
  std::optional<HumanDialogNotice> take(const Json& state, const Json& dialogs,
                                        bool physical_input_held) {
    if (physical_input_held) return std::nullopt;
    const auto human_tab = [&](const std::string& id) -> const Json* {
      auto tabs = state.find("tabs");
      if (tabs == state.end() || !tabs->is_array()) return nullptr;
      for (const auto& tab : *tabs)
        if (tab.value("tabId", "") == id && (tab.value("ownerSessionId", "") == "human" || tab.value("humanPaused", false))) return &tab;
      return nullptr;
    };
    const auto current_dialog = [&](const std::string& id) -> const Json* {
      if (dialogs.is_array()) for (const auto& dialog : dialogs)
        if (dialog.value("tabId", "") == id) return &dialog;
      return nullptr;
    };
    // Do not switch the selected dialog while a person may be reading it.
    if (!active_.empty() && human_tab(active_) && current_dialog(active_)) return std::nullopt;
    active_.clear();
    while (!pending_.empty()) {
      auto id = std::move(pending_.front()); pending_.pop_front();
      const auto tab = human_tab(id), dialog = current_dialog(id);
      if (!tab || !dialog) continue;
      active_ = id;
      return HumanDialogNotice{id, dialog->value("type", ""), dialog->value("message", ""),
                               dialog->value("origin", ""), tab->value("protected", false)};
    }
    return std::nullopt;
  }
 private:
  std::deque<std::string> pending_;
  std::string active_;
};
}
