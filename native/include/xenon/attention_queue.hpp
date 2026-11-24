#pragma once
#include "xenon/contracts.hpp"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xenon {

// Native shell queue and banner state for agent attention requests (help vs.
// review), kept free of Windows calls so it can be unit tested directly.
// Input is the broker's native tab state; nothing here is reachable through MCP.
enum class AttentionKind { help, review };

inline std::optional<AttentionKind> parse_attention_kind(std::string_view value) {
  if (value == "help") return AttentionKind::help;
  if (value == "review") return AttentionKind::review;
  return std::nullopt;
}

inline const char* attention_kind_name(AttentionKind kind) {
  return kind == AttentionKind::review ? "review" : "help";
}

namespace detail {
inline bool attention_json_bool(const Json& state, const char* key, bool fallback = false) {
  if (!state.is_object()) return fallback;
  const auto found = state.find(key);
  if (found == state.end() || !found->is_boolean()) return fallback;
  return found->get<bool>();
}

inline std::string attention_json_string(const Json& state, const char* key) {
  if (!state.is_object()) return {};
  const auto found = state.find(key);
  if (found == state.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

inline uint64_t attention_json_uint64(const Json& state, const char* key, uint64_t fallback = 0) {
  if (!state.is_object()) return fallback;
  const auto found = state.find(key);
  if (found == state.end()) return fallback;
  if (found->is_number_unsigned()) return found->get<uint64_t>();
  if (found->is_number_integer()) {
    const auto value = found->get<int64_t>();
    return value >= 0 ? static_cast<uint64_t>(value) : fallback;
  }
  return fallback;
}
}  // namespace detail

inline AttentionKind effective_attention_kind(const Json& tab_state) {
  if (tab_state.is_object()) {
    const auto found = tab_state.find("attentionKind");
    if (found != tab_state.end() && found->is_string()) {
      if (const auto parsed = parse_attention_kind(found->get_ref<const std::string&>())) {
        return *parsed;
      }
    }
  }
  return detail::attention_json_bool(tab_state, "attentionPersists", false)
             ? AttentionKind::review
             : AttentionKind::help;
}

struct AttentionQueueItem {
  std::string tab_id, workspace_id, reason, session_id;
  AttentionKind kind{AttentionKind::help};
  bool unseen{false};
  bool persisted{false};
  bool human_help_hold{false};
  uint64_t sequence{};
};

struct AttentionQueueSummary {
  std::vector<AttentionQueueItem> items;
  size_t help_count{}, review_count{}, unseen_help_count{}, unseen_review_count{};
  size_t total() const { return items.size(); }
  size_t unseen_total() const { return unseen_help_count + unseen_review_count; }
};

inline AttentionQueueSummary build_attention_queue(const std::map<std::string, Json>& tab_states) {
  AttentionQueueSummary summary;
  for (const auto& [tab_id, state] : tab_states) {
    if (!detail::attention_json_bool(state, "attentionRequested", false)) continue;
    AttentionQueueItem item;
    item.tab_id = tab_id;
    item.workspace_id = detail::attention_json_string(state, "workspaceId");
    item.reason = detail::attention_json_string(state, "attentionReason");
    item.session_id = detail::attention_json_string(state, "attentionSessionId");
    item.kind = effective_attention_kind(state);
    item.unseen = !detail::attention_json_bool(state, "attentionSeen", false);
    item.persisted = detail::attention_json_bool(state, "attentionPersists", false);
    item.human_help_hold = detail::attention_json_bool(state, "humanHelpHold", false);
    item.sequence = detail::attention_json_uint64(state, "attentionSequence", 0);
    if (item.kind == AttentionKind::help) {
      ++summary.help_count;
      if (item.unseen) ++summary.unseen_help_count;
    } else {
      ++summary.review_count;
      if (item.unseen) ++summary.unseen_review_count;
    }
    summary.items.push_back(std::move(item));
  }
  const auto priority_tier = [](const AttentionQueueItem& item) -> int {
    if (item.kind == AttentionKind::help) return item.unseen ? 0 : 1;
    return item.unseen ? 2 : 3;
  };
  std::sort(summary.items.begin(), summary.items.end(),
            [&](const AttentionQueueItem& lhs, const AttentionQueueItem& rhs) {
              const int lhs_tier = priority_tier(lhs);
              const int rhs_tier = priority_tier(rhs);
              if (lhs_tier != rhs_tier) return lhs_tier < rhs_tier;
              if (lhs.sequence != rhs.sequence) return lhs.sequence < rhs.sequence;
              return lhs.tab_id < rhs.tab_id;
            });
  return summary;
}

inline std::optional<std::string> next_attention_tab(const AttentionQueueSummary& queue,
                                                     std::string_view current_tab_id) {
  if (queue.items.empty()) return std::nullopt;
  for (size_t i = 0; i < queue.items.size(); ++i) {
    if (queue.items[i].tab_id == current_tab_id) {
      return queue.items[(i + 1) % queue.items.size()].tab_id;
    }
  }
  return queue.items.front().tab_id;
}

enum class BannerMode { none, help_waiting, help_hold, review_ready };

inline BannerMode tab_banner_mode(const Json& tab_state) {
  if (!tab_state.is_object()) return BannerMode::none;
  if (detail::attention_json_bool(tab_state, "humanHelpHold", false)) return BannerMode::help_hold;
  if (detail::attention_json_bool(tab_state, "attentionRequested", false)) {
    return effective_attention_kind(tab_state) == AttentionKind::review
               ? BannerMode::review_ready
               : BannerMode::help_waiting;
  }
  return BannerMode::none;
}

}  // namespace xenon
