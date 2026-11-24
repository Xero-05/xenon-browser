#include "xenon/attention_queue.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace xenon;

namespace {

void require(bool condition, const char* message) {
  assert(condition);
  if (!condition) throw std::runtime_error(message);
}

Json make_tab(bool requested, const char* kind = nullptr, bool seen = false, bool persists = false,
              bool hold = false, uint64_t sequence = 0, const char* workspace = "ws-1",
              const char* reason = "Need input", const char* session = "worker-1") {
  Json state{
      {"attentionRequested", requested},
      {"attentionSeen", seen},
      {"attentionPersists", persists},
      {"humanHelpHold", hold},
      {"attentionSequence", sequence},
      {"workspaceId", workspace},
      {"attentionReason", reason},
      {"attentionSessionId", session},
  };
  if (kind != nullptr) state["attentionKind"] = kind;
  return state;
}

void test_attention_kind_parsing_and_names() {
  require(parse_attention_kind("help") == AttentionKind::help, "Parses 'help' as AttentionKind::help");
  require(parse_attention_kind("review") == AttentionKind::review, "Parses 'review' as AttentionKind::review");
  for (const std::string_view invalid : {"", "HELP", "Review", " help", "help ", "hold", "none", "unknown"}) {
    require(!parse_attention_kind(invalid).has_value(), "Rejects unknown or improperly cased attention kinds");
  }

  require(std::string_view(attention_kind_name(AttentionKind::help)) == "help",
          "attention_kind_name returns 'help'");
  require(std::string_view(attention_kind_name(AttentionKind::review)) == "review",
          "attention_kind_name returns 'review'");
  require(parse_attention_kind(attention_kind_name(AttentionKind::help)) == AttentionKind::help,
          "Help kind round-trips through name and parser");
  require(parse_attention_kind(attention_kind_name(AttentionKind::review)) == AttentionKind::review,
          "Review kind round-trips through name and parser");
}

void test_effective_attention_kind() {
  // Explicit valid attentionKind takes precedence over attentionPersists.
  require(effective_attention_kind(Json{{"attentionKind", "help"}, {"attentionPersists", false}}) ==
              AttentionKind::help,
          "Explicit help with persists=false resolves to help");
  require(effective_attention_kind(Json{{"attentionKind", "help"}, {"attentionPersists", true}}) ==
              AttentionKind::help,
          "Explicit help overrides persists=true fallback");
  require(effective_attention_kind(Json{{"attentionKind", "review"}, {"attentionPersists", false}}) ==
              AttentionKind::review,
          "Explicit review overrides persists=false fallback");
  require(effective_attention_kind(Json{{"attentionKind", "review"}, {"attentionPersists", true}}) ==
              AttentionKind::review,
          "Explicit review with persists=true resolves to review");

  // Missing or invalid attentionKind falls back to attentionPersists.
  require(effective_attention_kind(Json{{"attentionPersists", true}}) == AttentionKind::review,
          "Missing attentionKind with persists=true falls back to review");
  require(effective_attention_kind(Json{{"attentionPersists", false}}) == AttentionKind::help,
          "Missing attentionKind with persists=false falls back to help");
  require(effective_attention_kind(Json::object()) == AttentionKind::help,
          "Empty object defaults to help");

  for (const Json& bad_kind :
       {Json(""), Json("REVIEW"), Json("other"), Json(nullptr), Json(42), Json(true), Json::array(), Json::object()}) {
    require(effective_attention_kind(Json{{"attentionKind", bad_kind}, {"attentionPersists", true}}) ==
                AttentionKind::review,
            "Invalid attentionKind falls back to review when attentionPersists is true");
    require(effective_attention_kind(Json{{"attentionKind", bad_kind}, {"attentionPersists", false}}) ==
                AttentionKind::help,
            "Invalid attentionKind falls back to help when attentionPersists is false");
  }

  // Non-object and malformed attentionPersists values fail closed to help.
  for (const Json& non_object : {Json(), Json(nullptr), Json::array(), Json("review"), Json(1), Json(true)}) {
    require(effective_attention_kind(non_object) == AttentionKind::help,
            "Non-object tab state fails closed to help");
  }
  require(effective_attention_kind(Json{{"attentionPersists", "true"}}) == AttentionKind::help,
          "Non-boolean attentionPersists fails closed to help");
  require(effective_attention_kind(Json{{"attentionPersists", 1}}) == AttentionKind::help,
          "Numeric attentionPersists fails closed to help");
}

void test_queue_filtering_fields_and_defaults() {
  const AttentionQueueSummary default_summary;
  require(default_summary.items.empty() && default_summary.total() == 0 && default_summary.unseen_total() == 0 &&
              default_summary.help_count == 0 && default_summary.review_count == 0 &&
              default_summary.unseen_help_count == 0 && default_summary.unseen_review_count == 0,
          "Default-constructed AttentionQueueSummary is zeroed");

  const AttentionQueueItem default_item;
  require(default_item.tab_id.empty() && default_item.workspace_id.empty() && default_item.reason.empty() &&
              default_item.session_id.empty() && default_item.kind == AttentionKind::help && !default_item.unseen &&
              !default_item.persisted && !default_item.human_help_hold && default_item.sequence == 0,
          "Default-constructed AttentionQueueItem has expected initial values");

  const auto empty = build_attention_queue({});
  require(empty.items.empty() && empty.total() == 0 && empty.unseen_total() == 0,
          "Empty tab map produces an empty queue");

  const uint64_t large_seq = std::numeric_limits<uint64_t>::max() - 7;
  std::map<std::string, Json> tabs{
      {"ignored-false", make_tab(false, "help", false, false, false, 1)},
      {"ignored-hold-only", make_tab(false, "help", true, false, true, 2)},
      {"ignored-empty", Json::object()},
      {"ignored-null", Json(nullptr)},
      {"ignored-array", Json::array()},
      {"ignored-bad-flag", Json{{"attentionRequested", "true"}}},
      {"minimal", Json{{"attentionRequested", true}}},
      {"full-help",
       make_tab(true, "help", false, true, true, large_seq, "ws-alpha", "Confirm 2FA code", "worker-alpha")},
      {"malformed-fields",
       Json{{"attentionRequested", true},
            {"attentionKind", 123},
            {"attentionSeen", "yes"},
            {"attentionPersists", "yes"},
            {"humanHelpHold", 1},
            {"attentionSequence", -5},
            {"attentionReason", nullptr},
            {"attentionSessionId", 99},
            {"workspaceId", true}}},
      {"signed-seq", Json{{"attentionRequested", true}, {"attentionSequence", 42}, {"attentionKind", "review"}}},
  };

  const auto queue = build_attention_queue(tabs);
  require(queue.total() == 4, "Includes only tabs with boolean attentionRequested=true");
  require(queue.help_count == 3 && queue.unseen_help_count == 3 && queue.review_count == 1 &&
              queue.unseen_review_count == 1 && queue.unseen_total() == 4,
          "Counts match filtered items across explicit, minimal and malformed entries");

  // Unseen help tier: sequence 0 ("malformed-fields" < "minimal"), then large_seq ("full-help").
  // Unseen review tier: sequence 42 ("signed-seq").
  require(queue.items[0].tab_id == "malformed-fields", "Malformed optional fields default sequence to 0");
  require(queue.items[0].workspace_id.empty() && queue.items[0].reason.empty() && queue.items[0].session_id.empty() &&
              queue.items[0].kind == AttentionKind::help && queue.items[0].unseen && !queue.items[0].persisted &&
              !queue.items[0].human_help_hold && queue.items[0].sequence == 0,
          "Malformed optional fields safely fall back to defaults without throwing");

  require(queue.items[1].tab_id == "minimal", "Minimal tab uses map key as tab_id and default fields");
  require(queue.items[1].workspace_id.empty() && queue.items[1].reason.empty() && queue.items[1].session_id.empty() &&
              queue.items[1].kind == AttentionKind::help && queue.items[1].unseen && !queue.items[1].persisted &&
              !queue.items[1].human_help_hold && queue.items[1].sequence == 0,
          "Minimal attention tab populates default values correctly");

  require(queue.items[2].tab_id == "full-help", "Populates full item with high uint64 sequence");
  require(queue.items[2].workspace_id == "ws-alpha" && queue.items[2].reason == "Confirm 2FA code" &&
              queue.items[2].session_id == "worker-alpha" && queue.items[2].kind == AttentionKind::help &&
              queue.items[2].unseen && queue.items[2].persisted && queue.items[2].human_help_hold &&
              queue.items[2].sequence == large_seq,
          "All fields on full attention item are preserved accurately");

  require(queue.items[3].tab_id == "signed-seq" && queue.items[3].kind == AttentionKind::review &&
              queue.items[3].sequence == 42,
          "Signed non-negative JSON integers are accepted as sequence numbers");
}

void test_queue_priority_sort_and_counts() {
  // Lower-priority tiers are intentionally given smaller sequence numbers and
  // earlier tab_ids so the test proves tier priority dominates sequence and id.
  std::map<std::string, Json> tabs{
      // Tier 4: seen review
      {"a-seen-rev-1", make_tab(true, "review", true, true, false, 1)},
      {"b-seen-rev-1", make_tab(true, nullptr, true, true, false, 1)},  // legacy persisted -> review
      {"a-seen-rev-2", make_tab(true, "review", true, false, false, 2)},

      // Tier 3: unseen review
      {"z-unseen-rev-5", make_tab(true, "review", false, false, false, 5)},
      {"a-unseen-rev-10", make_tab(true, nullptr, false, true, false, 10)},  // legacy persisted -> review
      {"m-unseen-rev-5", make_tab(true, "review", false, true, false, 5)},

      // Tier 2: seen help
      {"c-seen-help-3", make_tab(true, "help", true, true, false, 3)},  // explicit help despite persists=true
      {"a-seen-help-3", make_tab(true, "help", true, false, false, 3)},
      {"b-seen-help-9", make_tab(true, nullptr, true, false, false, 9)},

      // Tier 1: unseen help
      {"z-unseen-help-20", make_tab(true, "help", false, false, false, 20)},
      {"b-unseen-help-15", make_tab(true, nullptr, false, false, false, 15)},
      {"a-unseen-help-15", make_tab(true, "help", false, true, false, 15)},

      // Excluded noise
      {"0-inactive", make_tab(false, "help", false, false, false, 0)},
  };

  const auto queue = build_attention_queue(tabs);
  const std::vector<std::string> expected_order{
      // 1. Unseen help (ascending sequence, then ascending tab_id)
      "a-unseen-help-15",
      "b-unseen-help-15",
      "z-unseen-help-20",
      // 2. Seen help
      "a-seen-help-3",
      "c-seen-help-3",
      "b-seen-help-9",
      // 3. Unseen review
      "m-unseen-rev-5",
      "z-unseen-rev-5",
      "a-unseen-rev-10",
      // 4. Seen review
      "a-seen-rev-1",
      "b-seen-rev-1",
      "a-seen-rev-2",
  };

  require(queue.total() == expected_order.size(), "Queue contains all 12 active attention items");
  require(queue.help_count == 6, "Exactly 6 help items (3 unseen + 3 seen)");
  require(queue.review_count == 6, "Exactly 6 review items (3 unseen + 3 seen)");
  require(queue.unseen_help_count == 3, "Exactly 3 unseen help items");
  require(queue.unseen_review_count == 3, "Exactly 3 unseen review items");
  require(queue.unseen_total() == 6, "unseen_total equals unseen_help_count + unseen_review_count");

  for (size_t i = 0; i < expected_order.size(); ++i) {
    require(queue.items[i].tab_id == expected_order[i],
            "Queue items follow deterministic tier, sequence, and tab_id order");
  }
}

void test_next_attention_tab() {
  const AttentionQueueSummary empty;
  require(!next_attention_tab(empty, "").has_value(), "Empty queue returns nullopt for empty current tab");
  require(!next_attention_tab(empty, "tab-1").has_value(), "Empty queue returns nullopt for non-empty current tab");

  // Single-item queue.
  const auto single = build_attention_queue({{"only-tab", make_tab(true, "help", false, false, false, 1)}});
  require(next_attention_tab(single, "only-tab") == std::optional<std::string>("only-tab"),
          "Single-item queue wraps around to the same tab");
  require(next_attention_tab(single, "unrelated") == std::optional<std::string>("only-tab"),
          "Single-item queue returns front when current tab is absent");
  require(next_attention_tab(single, "") == std::optional<std::string>("only-tab"),
          "Single-item queue returns front for empty current tab");

  // Multi-item queue built through priority ordering.
  const auto multi = build_attention_queue({
      {"rev-seen", make_tab(true, "review", true, true, false, 1)},
      {"help-unseen", make_tab(true, "help", false, false, false, 10)},
      {"help-seen", make_tab(true, "help", true, false, false, 5)},
      {"rev-unseen", make_tab(true, "review", false, true, false, 2)},
  });
  require(multi.total() == 4, "Multi-item navigation fixture has 4 tabs");
  require(next_attention_tab(multi, "non-attention-tab") == std::optional<std::string>("help-unseen"),
          "Unknown current tab jumps to highest-priority front item");
  require(next_attention_tab(multi, "") == std::optional<std::string>("help-unseen"),
          "Empty current tab jumps to highest-priority front item");
  require(next_attention_tab(multi, "help-unseen") == std::optional<std::string>("help-seen"),
          "Advances from first to second priority item");
  require(next_attention_tab(multi, "help-seen") == std::optional<std::string>("rev-unseen"),
          "Advances from second to third priority item");
  require(next_attention_tab(multi, "rev-unseen") == std::optional<std::string>("rev-seen"),
          "Advances from third to fourth priority item");
  require(next_attention_tab(multi, "rev-seen") == std::optional<std::string>("help-unseen"),
          "Wraps around from last item back to front");
}

void test_tab_banner_mode() {
  // Non-objects always produce BannerMode::none.
  for (const Json& non_object : {Json(), Json(nullptr), Json::array(), Json("help"), Json(0), Json(true)}) {
    require(tab_banner_mode(non_object) == BannerMode::none, "Non-object tab state produces BannerMode::none");
  }
  require(tab_banner_mode(Json::object()) == BannerMode::none, "Empty object produces BannerMode::none");

  // Inactive tab with review/persists metadata still produces none.
  require(tab_banner_mode(make_tab(false, "review", false, true, false, 1)) == BannerMode::none,
          "Inactive tab without hold produces BannerMode::none");

  // humanHelpHold takes precedence regardless of attentionRequested or kind.
  require(tab_banner_mode(Json{{"humanHelpHold", true}}) == BannerMode::help_hold,
          "humanHelpHold alone produces BannerMode::help_hold");
  require(tab_banner_mode(make_tab(false, "help", false, false, true, 1)) == BannerMode::help_hold,
          "humanHelpHold with attentionRequested=false produces BannerMode::help_hold");
  require(tab_banner_mode(make_tab(true, "help", false, false, true, 1)) == BannerMode::help_hold,
          "humanHelpHold overrides help_waiting");
  require(tab_banner_mode(make_tab(true, "review", false, true, true, 1)) == BannerMode::help_hold,
          "humanHelpHold overrides review_ready");

  // Active help requests -> help_waiting (seen or unseen, explicit or fallback).
  require(tab_banner_mode(Json{{"attentionRequested", true}}) == BannerMode::help_waiting,
          "Default active attention request produces BannerMode::help_waiting");
  require(tab_banner_mode(make_tab(true, "help", false, false, false, 1)) == BannerMode::help_waiting,
          "Unseen help request produces BannerMode::help_waiting");
  require(tab_banner_mode(make_tab(true, "help", true, false, false, 1)) == BannerMode::help_waiting,
          "Seen help request still produces BannerMode::help_waiting");
  require(tab_banner_mode(make_tab(true, "help", false, true, false, 1)) == BannerMode::help_waiting,
          "Explicit help kind with persists=true produces BannerMode::help_waiting");
  require(tab_banner_mode(make_tab(true, "invalid", false, false, false, 1)) == BannerMode::help_waiting,
          "Invalid kind with persists=false falls back to BannerMode::help_waiting");

  // Active review requests -> review_ready (seen or unseen, explicit or fallback).
  require(tab_banner_mode(make_tab(true, "review", false, false, false, 1)) == BannerMode::review_ready,
          "Explicit review with persists=false produces BannerMode::review_ready");
  require(tab_banner_mode(make_tab(true, "review", true, true, false, 1)) == BannerMode::review_ready,
          "Seen review request still produces BannerMode::review_ready");
  require(tab_banner_mode(make_tab(true, nullptr, false, true, false, 1)) == BannerMode::review_ready,
          "Legacy persists=true without attentionKind produces BannerMode::review_ready");
  require(tab_banner_mode(make_tab(true, "invalid", false, true, false, 1)) == BannerMode::review_ready,
          "Invalid kind with persists=true falls back to BannerMode::review_ready");

  // Malformed boolean fields fail closed without throwing.
  require(tab_banner_mode(Json{{"humanHelpHold", "true"}, {"attentionRequested", 1}}) == BannerMode::none,
          "Malformed banner flags fail closed to BannerMode::none");
}

}  // namespace

int main() {
  try {
    test_attention_kind_parsing_and_names();
    test_effective_attention_kind();
    test_queue_filtering_fields_and_defaults();
    test_queue_priority_sort_and_counts();
    test_next_attention_tab();
    test_tab_banner_mode();
    std::cout << "Attention queue passed: kind parsing, fallback resolution, priority ordering, counts, cycling and banner modes\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
