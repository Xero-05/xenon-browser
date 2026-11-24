#include "xenon/observation_format.hpp"

#include <cassert>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace xenon;
namespace obs = xenon::observation;

namespace {

Json make_verified_button(const std::string& name, const std::string& ref,
                          const std::string& frame_id = "frame-main",
                          double x = 10.0, double y = 20.0,
                          double width = 120.0, double height = 36.0) {
  return {
      {"role", "button"},
      {"name", name},
      {"frameId", frame_id},
      {"ref", ref},
      {"tag", "BUTTON"},
      {"provenance", "chromium-rendered-text-with-accessibility-role"},
      {"nameProvenance", "filtered_rendered_text"},
      {"bounds",
       {{"x", x},
        {"y", y},
        {"width", width},
        {"height", height},
        {"coordinateSpace", "frame-document-css"}}},
      {"boundsProvenance", "chromium-dom-snapshot"},
      {"geometryEvidence", "unoccluded_center_patch"},
      {"visibilityStatus", "verified"},
      {"screenshotRequired", false},
  };
}

}  // namespace

int main() {
  auto test = [](const char* name, const std::function<void()>& run) {
    run();
    std::cout << "PASS " << name << '\n';
  };

  test("ascii_lower handles ASCII and preserves UTF-8 bytes", [] {
    assert(obs::ascii_lower("").empty());
    assert(obs::ascii_lower("abc123") == "abc123");
    assert(obs::ascii_lower("Hello, WORLD! 09_AZ") == "hello, world! 09_az");
    // UTF-8 multi-byte sequence for U+00C9 ('\xC3\x89') must remain untouched.
    const std::string utf8 = "CAF\xC3\x89-Menu";
    assert(obs::ascii_lower(utf8) == "caf\xC3\x89-menu");
  });

  test("is_interactive_role matches exact allowlist case-insensitively", [] {
    const std::vector<std::string> interactive = {
        "button",           "link",          "textbox",   "searchbox",
        "combobox",         "checkbox",      "radio",     "switch",
        "menuitem",         "menuitemcheckbox", "menuitemradio", "tab",
        "option",           "slider",        "spinbutton", "listbox",
        "treeitem",
    };
    assert(interactive.size() == 17);
    for (const auto& role : interactive) {
      assert(obs::is_interactive_role(role));
    }
    assert(obs::is_interactive_role("Button"));
    assert(obs::is_interactive_role("LINK"));
    assert(obs::is_interactive_role("MenuItemCheckbox"));

    for (const char* non_interactive :
         {"", "StaticText", "heading", "paragraph", "img", "generic", "none",
          "RootWebArea", "WebArea", "InlineTextBox", "navigation", "buttons",
          "a", "superlongrolenamethatdoesnotexist"}) {
      assert(!obs::is_interactive_role(non_interactive));
    }
  });

  test("parse_filter and matches_filter handle query, roles, interactiveOnly and caps", [] {
    const auto empty_filter = obs::parse_filter(Json::object());
    assert(empty_filter.query_lower.empty());
    assert(empty_filter.roles_lower.empty());
    assert(!empty_filter.interactive_only);
    assert(obs::matches_filter(empty_filter, "StaticText", "Anything"));
    assert(obs::matches_filter(empty_filter, "button", ""));

    const auto non_object = obs::parse_filter(Json::array({"bad"}));
    assert(non_object.query_lower.empty());
    assert(non_object.roles_lower.empty());
    assert(!non_object.interactive_only);

    const auto parsed = obs::parse_filter({
        {"query", "Save"},
        {"roles", Json::array({"Button", "LINK", 42, ""})},
        {"interactiveOnly", true},
    });
    assert(parsed.query_lower == "save");
    assert(parsed.roles_lower.size() == 2);
    assert(parsed.roles_lower.contains("button"));
    assert(parsed.roles_lower.contains("link"));
    assert(parsed.interactive_only);

    assert(obs::matches_filter(parsed, "button", "Save Changes"));
    assert(obs::matches_filter(parsed, "LINK", "auto-save draft"));
    assert(!obs::matches_filter(parsed, "textbox", "Save Changes"));
    assert(!obs::matches_filter(parsed, "button", "Cancel"));
    assert(!obs::matches_filter(parsed, "StaticText", "Save Changes"));

    // interactiveOnly rejects non-interactive roles even when listed in roles.
    const auto combined = obs::parse_filter({
        {"roles", Json::array({"button", "StaticText"})},
        {"interactiveOnly", true},
    });
    assert(obs::matches_filter(combined, "button", "OK"));
    assert(!obs::matches_filter(combined, "StaticText", "OK"));

    // Roles array is bounded to at most 32 entries.
    Json many_roles = Json::array();
    for (int i = 0; i < 40; ++i) {
      many_roles.push_back("Role" + std::to_string(i));
    }
    const auto capped = obs::parse_filter({{"roles", many_roles}, {"interactiveOnly", "yes"}});
    assert(capped.roles_lower.size() == 32);
    assert(capped.roles_lower.contains("role0"));
    assert(capped.roles_lower.contains("role31"));
    assert(!capped.roles_lower.contains("role32"));
    assert(!capped.interactive_only);
  });

  test("compact_nodes omits matching defaults and retains overrides and essential fields", [] {
    Json verified = make_verified_button("Submit", "ref-1");
    verified["inputType"] = "submit";
    verified["states"] = {{"disabled", false}, {"focused", true}};
    verified["protected"] = true;

    Json uncertain = {
        {"role", "button"},
        {"name", ""},
        {"frameId", "frame-child"},
        {"provenance", "chromium-rendered-text-with-accessibility-role"},
        {"nameProvenance", "withheld_or_unlabeled"},
        {"bounds",
         {{"x", 40},
          {"y", 50},
          {"width", 80},
          {"height", 24},
          {"coordinateSpace", "frame-document-css"}}},
        {"boundsProvenance", "chromium-dom-snapshot"},
        {"geometryEvidence", "unverified_snapshot_bounds"},
        {"visibilityStatus", "uncertain"},
        {"screenshotRequired", true},
        {"visibilityReason", "clipping_or_occlusion_unproven"},
        {"candidateId", "candidate-frame-child-99"},
        {"diagnosticId", "candidate-frame-child-99"},
    };

    Json text_node = {
        {"role", "StaticText"},
        {"name", "Rendered body"},
        {"frameId", "frame-main"},
        {"ref", "ref-2"},
        {"provenance", "chromium-rendered-text-node"},
        {"roleProvenance", "native-dom-text-type"},
        {"nameProvenance", "filtered_rendered_text"},
        {"bounds",
         {{"x", 0},
          {"y", 0},
          {"width", 300},
          {"height", 18},
          {"coordinateSpace", "visual-viewport-css"}}},
        {"boundsProvenance", "chromium-dom-snapshot"},
        {"geometryEvidence", "full_box"},
        {"visibilityStatus", "verified"},
        {"screenshotRequired", false},
    };

    const Json input_nodes = Json::array({verified, uncertain, text_node});
    const auto compacted = obs::compact_nodes(input_nodes, "frame-main");

    // Verify exact node_defaults contract.
    assert(compacted.node_defaults.at("frameId") == "frame-main");
    assert(compacted.node_defaults.at("provenance") ==
           "chromium-rendered-text-with-accessibility-role");
    assert(compacted.node_defaults.at("nameProvenance") == "filtered_rendered_text");
    assert(compacted.node_defaults.at("boundsProvenance") == "chromium-dom-snapshot");
    assert(compacted.node_defaults.at("geometryEvidence") == "unoccluded_center_patch");
    assert(compacted.node_defaults.at("visibilityStatus") == "verified");
    assert(compacted.node_defaults.at("screenshotRequired") == false);
    assert(compacted.node_defaults.at("boundsCoordinateSpace") == "frame-document-css");

    assert(compacted.nodes.size() == 3);

    // Verified main-frame button omits all 7 default fields and bounds.coordinateSpace.
    const auto& out0 = compacted.nodes[0];
    for (const char* omitted :
         {"frameId", "provenance", "nameProvenance", "boundsProvenance",
          "geometryEvidence", "visibilityStatus", "screenshotRequired"}) {
      assert(!out0.contains(omitted));
    }
    assert(!out0.at("bounds").contains("coordinateSpace"));
    assert(out0.at("role") == "button");
    assert(out0.at("name") == "Submit");
    assert(out0.at("ref") == "ref-1");
    assert(out0.at("tag") == "BUTTON");
    assert(out0.at("inputType") == "submit");
    assert(out0.at("states") == Json({{"disabled", false}, {"focused", true}}));
    assert(out0.at("protected") == true);
    assert(out0.at("bounds").at("x") == 10.0);
    assert(out0.at("bounds").at("width") == 120.0);

    // Uncertain child-frame node keeps non-default values and candidate metadata.
    const auto& out1 = compacted.nodes[1];
    assert(out1.at("frameId") == "frame-child");
    assert(!out1.contains("provenance"));
    assert(out1.at("nameProvenance") == "withheld_or_unlabeled");
    assert(!out1.contains("boundsProvenance"));
    assert(out1.at("geometryEvidence") == "unverified_snapshot_bounds");
    assert(out1.at("visibilityStatus") == "uncertain");
    assert(out1.at("screenshotRequired") == true);
    assert(out1.at("visibilityReason") == "clipping_or_occlusion_unproven");
    assert(out1.at("candidateId") == "candidate-frame-child-99");
    assert(out1.at("diagnosticId") == "candidate-frame-child-99");
    assert(!out1.at("bounds").contains("coordinateSpace"));

    // Rendered text node keeps distinct provenance, roleProvenance, geometryEvidence and non-default coordinateSpace.
    const auto& out2 = compacted.nodes[2];
    assert(!out2.contains("frameId"));
    assert(out2.at("provenance") == "chromium-rendered-text-node");
    assert(out2.at("roleProvenance") == "native-dom-text-type");
    assert(!out2.contains("nameProvenance"));
    assert(!out2.contains("boundsProvenance"));
    assert(out2.at("geometryEvidence") == "full_box");
    assert(!out2.contains("visibilityStatus"));
    assert(!out2.contains("screenshotRequired"));
    assert(out2.at("bounds").at("coordinateSpace") == "visual-viewport-css");

    // Empty default_frame_id must NOT omit frameId even if node frameId is "".
    const Json empty_frame_nodes = Json::array({
        {{"role", "button"}, {"name", "A"}, {"frameId", ""}, {"visibilityStatus", "verified"}},
        {{"role", "button"}, {"name", "B"}, {"frameId", "frame-1"}, {"visibilityStatus", "verified"}},
    });
    const auto empty_default = obs::compact_nodes(empty_frame_nodes, "");
    assert(empty_default.node_defaults.at("frameId") == "");
    assert(empty_default.nodes[0].contains("frameId"));
    assert(empty_default.nodes[0].at("frameId") == "");
    assert(!empty_default.nodes[0].contains("visibilityStatus"));
    assert(empty_default.nodes[1].at("frameId") == "frame-1");

    // Non-array input fails safely to empty array.
    const auto non_array = obs::compact_nodes(Json::object(), "frame-main");
    assert(non_array.nodes.is_array() && non_array.nodes.empty());
  });

  test("comparable_node_signature and node_identity_key stability", [] {
    Json node = make_verified_button("Checkout", "ref-abc");
    node["diagnosticId"] = "diag-1";
    const Json sig = obs::comparable_node_signature(node);
    assert(!sig.contains("ref"));
    assert(!sig.contains("diagnosticId"));
    assert(sig.at("name") == "Checkout");
    assert(node.contains("ref"));
    assert(node.contains("diagnosticId"));

    // Non-object signature passes through without throwing.
    assert(obs::comparable_node_signature(Json(42)) == Json(42));

    // Identity key distinguishes occurrences, frames, roles, tags, inputTypes.
    const std::string k0 = obs::node_identity_key(node, 0);
    const std::string k1 = obs::node_identity_key(node, 1);
    assert(k0 != k1);

    // Delimiter safety: pipe characters inside fields cannot collide across boundaries.
    const Json pipe_a = {{"role", "a|b"}, {"tag", ""}, {"name", "X"}};
    const Json pipe_b = {{"role", "a"}, {"tag", "b"}, {"name", "X"}};
    assert(obs::node_identity_key(pipe_a, 0) != obs::node_identity_key(pipe_b, 0));

    // Unnamed controls fall back to rounded bounds.
    const Json unnamed_a = {
        {"frameId", "f1"},
        {"role", "textbox"},
        {"tag", "INPUT"},
        {"inputType", "text"},
        {"name", ""},
        {"bounds", {{"x", 10.2}, {"y", 20.6}, {"width", 199.8}, {"height", 30.4}}},
    };
    const Json unnamed_b = {
        {"frameId", "f1"},
        {"role", "textbox"},
        {"tag", "INPUT"},
        {"inputType", "text"},
        {"name", ""},
        {"bounds", {{"x", 10}, {"y", 21}, {"width", 200}, {"height", 30}}},
    };
    const Json unnamed_c = {
        {"frameId", "f1"},
        {"role", "textbox"},
        {"tag", "INPUT"},
        {"inputType", "text"},
        {"name", ""},
        {"bounds", {{"x", 10}, {"y", 80}, {"width", 200}, {"height", 30}}},
    };
    assert(obs::node_identity_key(unnamed_a, 0) == obs::node_identity_key(unnamed_b, 0));
    assert(obs::node_identity_key(unnamed_a, 0) != obs::node_identity_key(unnamed_c, 0));

    // candidateId takes precedence over name/bounds.
    const Json cand_a = {{"frameId", "f1"}, {"role", "button"}, {"candidateId", "c-1"}, {"name", "Old"}};
    const Json cand_b = {{"frameId", "f1"}, {"role", "button"}, {"candidateId", "c-1"}, {"name", "New"}};
    assert(obs::node_identity_key(cand_a, 0) == obs::node_identity_key(cand_b, 0));

    // Non-finite or missing bounds handled safely.
    const Json bad_bounds = {
        {"role", "textbox"},
        {"name", ""},
        {"bounds", {{"x", std::numeric_limits<double>::infinity()}, {"y", 0}, {"width", 10}, {"height", 10}}},
    };
    assert(!obs::node_identity_key(bad_bounds, 0).empty());
  });

  test("compute_delta detects unchanged, added, removed, changed and duplicate rows", [] {
    // Both empty -> unchanged.
    const auto empty_delta = obs::compute_delta(Json::array(), Json::array());
    assert(empty_delta.unchanged);
    assert(empty_delta.added.empty());
    assert(empty_delta.removed.empty());
    assert(empty_delta.changed.empty());

    // Same nodes with rotated refs and diagnosticIds -> unchanged.
    Json prev_same = Json::array({
        make_verified_button("Save", "ref-old-1"),
        make_verified_button("Cancel", "ref-old-2"),
    });
    prev_same[0]["diagnosticId"] = "diag-old";
    Json curr_same = Json::array({
        make_verified_button("Save", "ref-new-1"),
        make_verified_button("Cancel", "ref-new-2"),
    });
    curr_same[0]["diagnosticId"] = "diag-new";
    const auto same_delta = obs::compute_delta(prev_same, curr_same);
    assert(same_delta.unchanged);
    assert(same_delta.added.empty());
    assert(same_delta.removed.empty());
    assert(same_delta.changed.empty());

    // Combined add, remove, change, and unchanged node.
    Json save_prev = make_verified_button("Save", "ref-1");
    save_prev["states"] = {{"disabled", false}};
    Json save_curr = make_verified_button("Save", "ref-10");
    save_curr["states"] = {{"disabled", true}};

    const Json prev = Json::array({
        save_prev,
        make_verified_button("Cancel", "ref-2"),
        make_verified_button("Old Action", "ref-3"),
    });
    const Json curr = Json::array({
        save_curr,
        make_verified_button("Cancel", "ref-11"),
        make_verified_button("New Action", "ref-12"),
    });

    const auto mixed = obs::compute_delta(prev, curr);
    assert(!mixed.unchanged);
    assert(mixed.added.size() == 1);
    assert(mixed.added[0].at("name") == "New Action");
    assert(mixed.added[0].at("ref") == "ref-12");

    assert(mixed.removed.size() == 1);
    assert(mixed.removed[0].at("name") == "Old Action");
    assert(!mixed.removed[0].contains("ref"));

    assert(mixed.changed.size() == 1);
    assert(mixed.changed[0].at("name") == "Save");
    assert(mixed.changed[0].at("ref") == "ref-10");
    assert(mixed.changed[0].at("states").at("disabled") == true);

    // Duplicate identical rows do not collide.
    Json row1_prev = make_verified_button("Delete", "r1", "frame-main", 10, 100);
    Json row2_prev = make_verified_button("Delete", "r2", "frame-main", 10, 140);
    Json row3_prev = make_verified_button("Delete", "r3", "frame-main", 10, 180);

    Json row1_curr = make_verified_button("Delete", "r10", "frame-main", 10, 100);
    Json row2_curr = make_verified_button("Delete", "r11", "frame-main", 10, 140);
    row2_curr["states"] = {{"disabled", true}};
    Json row3_curr = make_verified_button("Delete", "r12", "frame-main", 10, 180);
    Json row4_curr = make_verified_button("Delete", "r13", "frame-main", 10, 220);

    const auto dup_delta = obs::compute_delta(
        Json::array({row1_prev, row2_prev, row3_prev}),
        Json::array({make_verified_button("Notice", "r0", "frame-main", 10, 20),
                     row1_curr, row2_curr, row3_curr, row4_curr}));
    assert(!dup_delta.unchanged);
    assert(dup_delta.removed.empty());
    assert(dup_delta.added.size() == 2);
    assert(dup_delta.added[0].at("name") == "Notice");
    assert(dup_delta.added[0].at("ref") == "r0");
    assert(dup_delta.added[1].at("name") == "Delete");
    assert(dup_delta.added[1].at("ref") == "r13");
    assert(dup_delta.changed.size() == 1);
    assert(dup_delta.changed[0].at("ref") == "r11");
    assert(dup_delta.changed[0].at("states").at("disabled") == true);

    // Removing one duplicate row removes only the excess occurrence without ref.
    const auto shrink_delta = obs::compute_delta(
        Json::array({row1_prev, row2_prev, row3_prev}),
        Json::array({row1_curr, make_verified_button("Delete", "r21", "frame-main", 10, 140)}));
    assert(!shrink_delta.unchanged);
    assert(shrink_delta.added.empty());
    assert(shrink_delta.changed.empty());
    assert(shrink_delta.removed.size() == 1);
    assert(!shrink_delta.removed[0].contains("ref"));
    assert(shrink_delta.removed[0].at("bounds").at("y") == 180.0);
  });

  test("parse_wait_query handles text, anyOf, deduplication, bounds, absent and returnMode", [] {
    const auto empty_q = obs::parse_wait_query(Json::object());
    assert(empty_q.needles.empty());
    assert(empty_q.needles_lower.empty());
    assert(!empty_q.absent);
    assert(empty_q.return_mode == "full");

    const auto dedup = obs::parse_wait_query({
        {"text", "Order Confirmed"},
        {"anyOf", Json::array({"order confirmed", "ORDER CONFIRMED", "Payment Failed",
                               "", 123, std::string(1001, 'x'), std::string(1000, 'y')})},
        {"absent", true},
        {"returnMode", "matching"},
    });
    assert(dedup.needles.size() == 3);
    assert(dedup.needles[0] == "Order Confirmed");
    assert(dedup.needles_lower[0] == "order confirmed");
    assert(dedup.needles[1] == "Payment Failed");
    assert(dedup.needles_lower[1] == "payment failed");
    assert(dedup.needles[2] == std::string(1000, 'y'));
    assert(dedup.absent);
    assert(dedup.return_mode == "matching");

    const auto none_mode = obs::parse_wait_query({{"text", "Done"}, {"returnMode", "none"}});
    assert(none_mode.return_mode == "none");

    const auto invalid_mode = obs::parse_wait_query({{"text", "Done"}, {"returnMode", "bogus"}});
    assert(invalid_mode.return_mode == "full");

    // Cap at 16 needles.
    Json twenty = Json::array();
    for (int i = 0; i < 20; ++i) {
      twenty.push_back("Needle-" + std::to_string(i));
    }
    const auto capped = obs::parse_wait_query({{"anyOf", twenty}});
    assert(capped.needles.size() == 16);
    assert(capped.needles_lower.size() == 16);
    assert(capped.needles.front() == "Needle-0");
    assert(capped.needles.back() == "Needle-15");
  });

  test("evaluate_wait handles presence, priority order, deduplicated matches and absence", [] {
    const Json nodes = Json::array({
        make_verified_button("Processing Payment", "r1"),
        make_verified_button("Order Confirmed - Receipt", "r2"),
        make_verified_button("Error: Retry or Confirmed", "r3"),
    });

    // Matches FIRST needle in query order ("Error") even though node 1 matched "Confirmed" earlier.
    const auto q_multi = obs::parse_wait_query({
        {"anyOf", Json::array({"Error", "Confirmed"})},
    });
    const auto eval_multi = obs::evaluate_wait(q_multi, nodes);
    assert(eval_multi.matched);
    assert(eval_multi.matched_text == "Error");
    // Both node 1 and node 2 match; node 2 matches both needles but must appear only once.
    assert(eval_multi.matching_nodes.size() == 2);
    assert(eval_multi.matching_nodes[0].at("ref") == "r2");
    assert(eval_multi.matching_nodes[1].at("ref") == "r3");

    // No match when absent == false.
    const auto q_miss = obs::parse_wait_query({{"text", "Shipping Label"}});
    const auto eval_miss = obs::evaluate_wait(q_miss, nodes);
    assert(!eval_miss.matched);
    assert(eval_miss.matched_text.empty());
    assert(eval_miss.matching_nodes.is_array() && eval_miss.matching_nodes.empty());

    // absent == true fails when any needle matches.
    const auto q_absent_present = obs::parse_wait_query({
        {"anyOf", Json::array({"Missing", "processing"})},
        {"absent", true},
    });
    const auto eval_absent_present = obs::evaluate_wait(q_absent_present, nodes);
    assert(!eval_absent_present.matched);
    assert(eval_absent_present.matched_text.empty());
    assert(eval_absent_present.matching_nodes.is_array() &&
           eval_absent_present.matching_nodes.empty());

    // absent == true succeeds when zero nodes match any needle.
    const auto q_absent_gone = obs::parse_wait_query({
        {"anyOf", Json::array({"Loading...", "Spinner"})},
        {"absent", true},
    });
    const auto eval_absent_gone = obs::evaluate_wait(q_absent_gone, nodes);
    assert(eval_absent_gone.matched);
    assert(eval_absent_gone.matched_text.empty());
    assert(eval_absent_gone.matching_nodes.is_array() &&
           eval_absent_gone.matching_nodes.empty());

    // absent == true succeeds on empty nodes array.
    const auto eval_absent_empty = obs::evaluate_wait(q_absent_gone, Json::array());
    assert(eval_absent_empty.matched);

    // Empty wait query fails closed even when absent == true.
    const auto q_empty_absent = obs::parse_wait_query({{"absent", true}});
    const auto eval_empty_absent = obs::evaluate_wait(q_empty_absent, nodes);
    assert(!eval_empty_absent.matched);
  });

  test("normalize_stale_reason allows exact allowlist and defaults unknown reasons", [] {
    const std::vector<std::string> allowed = {
        "document_navigated", "node_detached",    "out_of_viewport",
        "occluded",           "human_activity",   "observation_superseded",
        "identity_changed",   "visibility_changed", "frame_changed",
    };
    assert(allowed.size() == 9);
    for (const auto& reason : allowed) {
      assert(obs::normalize_stale_reason(reason) == reason);
    }

    // Returned string_view must point to static storage even for temporary inputs.
    const std::string_view stable = obs::normalize_stale_reason(std::string("occluded"));
    assert(stable == "occluded");

    for (const char* unknown :
         {"", "OCCLUDED", "document_navigated ", "stale_element", "arbitrary website text"}) {
      assert(obs::normalize_stale_reason(unknown) == "observation_superseded");
    }
  });

  std::cout << "All observation_format_tests passed\n";
  return 0;
}
