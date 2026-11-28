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

    // Rendered non-interactive text node keeps distinct provenance & roleProvenance and omits bounds/geometry metadata in compact mode.
    const auto& out2 = compacted.nodes[2];
    assert(!out2.contains("frameId"));
    assert(out2.at("provenance") == "chromium-rendered-text-node");
    assert(out2.at("roleProvenance") == "native-dom-text-type");
    assert(!out2.contains("nameProvenance"));
    assert(!out2.contains("bounds"));
    assert(!out2.contains("boundsProvenance"));
    assert(!out2.contains("geometryEvidence"));
    assert(!out2.contains("visibilityStatus"));
    assert(!out2.contains("screenshotRequired"));
    assert(out2.at("ref") == "ref-2");

    // Interactive control with non-default coordinateSpace retains its bounds and non-default coordinateSpace.
    Json alt_space_btn = make_verified_button("Zoomed", "ref-3");
    alt_space_btn["bounds"]["coordinateSpace"] = "visual-viewport-css";
    alt_space_btn["geometryEvidence"] = "unoccluded_visible_patch";
    const auto alt_compacted = obs::compact_nodes(Json::array({alt_space_btn}), "frame-main");
    assert(alt_compacted.nodes.size() == 1);
    assert(alt_compacted.nodes[0].at("bounds").at("coordinateSpace") == "visual-viewport-css");
    assert(alt_compacted.nodes[0].at("geometryEvidence") == "unoccluded_visible_patch");

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

  test("compact_limitations condenses static prose while preserving all security signals", [] {
    const Json full_obs = obs::full_limitations(false);
    const Json compact_obs = obs::compact_limitations(false);
    assert(full_obs.is_array() && full_obs.size() == 5);
    assert(compact_obs.is_array() && compact_obs.size() == 5);
    const size_t full_bytes = full_obs.dump().size();
    const size_t compact_bytes = compact_obs.dump().size();
    assert(compact_bytes < full_bytes / 4);

    const Json full_insp = obs::full_limitations(true);
    const Json compact_insp = obs::compact_limitations(true);
    assert(full_insp.size() == 6);
    assert(compact_insp.size() == 6);
    assert(compact_insp.back() == "validated_capture_interval_not_atomic");
    assert(compact_insp.dump().size() < full_insp.dump().size() / 4);
  });

  test("compact_nodes rounds bounds, deduplicates covered container text, preserves controls/StaticText/headings, and shrinks synthetic payload", [] {
    auto make_node = [](const std::string& role, const std::string& name, const std::string& ref,
                        double x, double y, double w, double h, const std::string& frame = "frame-main") {
      return Json{
          {"role", role},
          {"name", name},
          {"frameId", frame},
          {"ref", ref},
          {"provenance", "chromium-rendered-text-with-accessibility-role"},
          {"nameProvenance", "filtered_rendered_text"},
          {"bounds", {{"x", x}, {"y", y}, {"width", w}, {"height", h}, {"coordinateSpace", "frame-document-css"}}},
          {"boundsProvenance", "chromium-dom-snapshot"},
          {"geometryEvidence", "unoccluded_center_patch"},
          {"visibilityStatus", "verified"},
          {"screenshotRequired", false},
      };
    };

    const std::string h1 = "Software Engineer Intern";
    const std::string p1 = "Build reliable browser automation systems in C++20 and strict TypeScript.";
    const std::string p2 = "Collaborate across security, rendering, and MCP protocol boundaries.";
    const std::string li1 = "Experience with Chromium or Win32 internals";
    const std::string li2 = "Strong testing discipline with synthetic fixtures";
    const std::string btn = "Apply Now";
    const std::string upload_txt = "Upload from Device";
    const std::string full_article = h1 + " " + p1 + " " + p2 + " " + li1 + " " + li2 + " " + btn + " " + upload_txt;

    Json upload_node = make_node("StaticText", upload_txt, "ref-upload", 24.33333587646484, 340.6666564941406, 142.140625, 19.999996185302734);
    upload_node["provenance"] = "chromium-rendered-text-node";
    upload_node["roleProvenance"] = "native-dom-text-type";
    upload_node["geometryEvidence"] = "full_box";

    Json dialog_node = make_node("dialog", "Confirm action Continue", "ref-dialog", 200.0, 420.0, 320.0, 120.0);
    Json dialog_text = make_node("StaticText", "Confirm action", "ref-dtxt", 216.0, 436.0, 110.0, 20.0);
    Json dialog_btn = make_verified_button("Continue", "ref-dbtn", "frame-main", 216.25, 472.75, 96.5, 32.0);

    Json synthetic_nodes = Json::array({
        make_node("main", full_article, "ref-main", 0.0, 0.0, 900.0, 600.0),
        make_node("article", full_article, "ref-article", 16.0, 16.0, 860.0, 400.0),
        make_node("heading", h1, "ref-h1", 24.123456789, 24.987654321, 320.4444444, 32.000001),
        make_node("StaticText", h1, "ref-h1-t", 24.123456789, 24.987654321, 310.0, 32.0),
        make_node("paragraph", p1, "ref-p1", 24.0, 68.3333333333, 760.0, 24.0),
        make_node("StaticText", p1, "ref-p1-t", 24.0, 68.3333333333, 520.0, 24.0),
        make_node("paragraph", p2, "ref-p2", 24.0, 100.6666666667, 760.0, 24.0),
        make_node("StaticText", p2, "ref-p2-t", 24.0, 100.6666666667, 500.0, 24.0),
        make_node("list", li1 + " " + li2, "ref-ul", 24.0, 140.0, 760.0, 80.0),
        make_node("listitem", li1, "ref-li1", 32.0, 144.0, 720.0, 24.0),
        make_node("StaticText", li1, "ref-li1-t", 32.0, 144.0, 380.0, 24.0),
        make_node("listitem", li2, "ref-li2", 32.0, 176.0, 720.0, 24.0),
        make_node("StaticText", li2, "ref-li2-t", 32.0, 176.0, 410.0, 24.0),
        make_verified_button(btn, "ref-apply", "frame-main", 24.33333587646484, 280.6666564941406, 120.140625, 35.999996185302734),
        make_node("StaticText", btn, "ref-apply-t", 36.0, 288.0, 88.0, 20.0),
        upload_node,
        dialog_node,
        dialog_text,
        dialog_btn,
    });

    const auto compacted = obs::compact_nodes(synthetic_nodes, "frame-main");

    // Pure structural wrappers (main, article, paragraph x2, list, listitem x2 = 7)
    // plus duplicate StaticText children inside heading and button (ref-h1-t, ref-apply-t = 2) omitted.
    assert(compacted.nodes.size() == synthetic_nodes.size() - 9);
    // Semantic dialog kept with empty name and nameProvenance == "in_children".
    bool saw_dialog = false, saw_upload = false, saw_apply = false, saw_heading = false;
    for (const auto& n : compacted.nodes) {
      const auto role = n.at("role").get<std::string>();
      const auto ref = n.value("ref", "");
      assert(role != "main" && role != "article" && role != "paragraph" && role != "list" && role != "listitem");
      assert(ref != "ref-h1-t" && ref != "ref-apply-t");
      if (role == "dialog") {
        saw_dialog = true;
        assert(n.at("name") == "");
        assert(n.at("nameProvenance") == "in_children");
        assert(!n.contains("bounds"));
      }
      if (ref == "ref-upload") {
        saw_upload = true;
        assert(n.at("role") == "StaticText");
        assert(n.at("name") == upload_txt);
        assert(n.at("provenance") == "chromium-rendered-text-node");
        assert(n.at("roleProvenance") == "native-dom-text-type");
        // Non-interactive verified StaticText omits bounds while retaining ref.
        assert(!n.contains("bounds"));
        assert(!n.contains("geometryEvidence"));
      }
      if (ref == "ref-apply") {
        saw_apply = true;
        assert(n.at("name") == btn);
        // Interactive control retains rounded bounds (integer when exact, 1 decimal otherwise).
        assert(n.at("bounds").at("x") == 24.3);
        assert(n.at("bounds").at("y") == 280.7);
        assert(n.at("bounds").at("width") == 120.1);
        assert(n.at("bounds").at("height").is_number_integer() && n.at("bounds").at("height") == 36);
      }
      if (role == "heading") {
        saw_heading = true;
        assert(n.at("name") == h1);
        assert(ref == "ref-h1");
        assert(!n.contains("bounds"));
      }
    }
    assert(saw_dialog && saw_upload && saw_apply && saw_heading);

    // Filtered observation with roles=["paragraph"] has no child StaticText -> paragraph MUST NOT be omitted or emptied.
    const Json only_paragraph = Json::array({make_node("paragraph", p1, "ref-p-only", 24.0, 68.0, 760.0, 24.0)});
    const auto keep_paragraph = obs::compact_nodes(only_paragraph, "frame-main");
    assert(keep_paragraph.nodes.size() == 1);
    assert(keep_paragraph.nodes[0].at("role") == "paragraph");
    assert(keep_paragraph.nodes[0].at("name") == p1);

    // Measure full vs compact synthetic observation payload bytes.
    const Json full_payload = {
        {"visibility", {{"policy", "rendered_viewport"}, {"limitations", obs::full_limitations(false)}}},
        {"nodes", synthetic_nodes},
    };
    const Json compact_payload = {
        {"visibility", {{"policy", "rendered_viewport"}, {"limitations", obs::compact_limitations(false)}}},
        {"nodeDefaults", compacted.node_defaults},
        {"nodes", compacted.nodes},
    };
    const size_t full_bytes = full_payload.dump().size();
    const size_t compact_bytes = compact_payload.dump().size();
    const double reduction_pct = (1.0 - static_cast<double>(compact_bytes) / static_cast<double>(full_bytes)) * 100.0;
    std::cout << "  Synthetic payload compaction: full=" << full_bytes
              << "B -> compact=" << compact_bytes << "B (" << static_cast<int>(std::round(reduction_pct))
              << "% smaller)\n";
    assert(compact_bytes < full_bytes * 0.25);
  });

  test("compact_nodes dramatically shrinks realistic dense real-page DOM while preserving unnamed controls, uncertain candidates and upload StaticText", [] {
    int ref_seq = 0;
    auto make_ref = [&]() {
      std::string hex = "00000000000000000000000000000000";
      const std::string digits = std::to_string(++ref_seq);
      hex.replace(hex.size() - digits.size(), digits.size(), digits);
      return hex;
    };
    auto make_ax = [&](const std::string& role, const std::string& name,
                       double x, double y, double w, double h,
                       const std::string& tag = "",
                       const std::string& geom = "unoccluded_center_patch") {
      Json n = {
          {"role", role},
          {"name", name},
          {"frameId", "frame-main"},
          {"ref", make_ref()},
          {"provenance", "chromium-rendered-text-with-accessibility-role"},
          {"nameProvenance", name.empty() ? "withheld_or_unlabeled" : "filtered_rendered_text"},
          {"bounds", {{"x", x}, {"y", y}, {"width", w}, {"height", h}, {"coordinateSpace", "frame-document-css"}}},
          {"boundsProvenance", "chromium-dom-snapshot"},
          {"geometryEvidence", geom},
          {"visibilityStatus", "verified"},
          {"screenshotRequired", false},
      };
      if (!tag.empty()) n["tag"] = tag;
      return n;
    };

    Json dense = Json::array();
    // 1. Top nav banner & search region with empty structural wrappers + unnamed icon buttons + links with duplicate StaticText.
    dense.push_back(make_ax("banner", "", 0, 0, 1280, 64));
    dense.push_back(make_ax("navigation", "", 16, 8, 900, 48));
    dense.push_back(make_ax("search", "", 200, 12, 320, 36));
    dense.push_back(make_ax("list", "", 540, 12, 400, 36));
    const std::vector<std::string> nav_links = {"Dashboard", "Job Postings", "Applications", "Interviews", "Messages"};
    for (size_t i = 0; i < nav_links.size(); ++i) {
      const double lx = 540.0 + static_cast<double>(i) * 85.0;
      dense.push_back(make_ax("listitem", "", lx, 14, 80, 32));
      dense.push_back(make_ax("link", nav_links[i], lx + 2, 16, 76, 28, "A"));
      dense.push_back(make_ax("StaticText", nav_links[i], lx + 6, 20, 68, 18, "", "full_box"));
    }
    // Unnamed search textbox + unnamed icon buttons (must NEVER be dropped!).
    Json search_box = make_ax("searchbox", "", 210, 14, 260, 32, "INPUT");
    search_box["inputType"] = "search";
    dense.push_back(search_box);
    dense.push_back(make_ax("button", "", 1180, 14, 36, 36, "BUTTON")); // notification bell icon
    dense.push_back(make_ax("button", "", 1224, 14, 36, 36, "BUTTON")); // profile avatar icon
    dense.push_back(make_ax("image", "", 1228, 18, 28, 28, "IMG"));     // decorative avatar img inside chrome
    dense.push_back(make_ax("separator", "", 0, 64, 1280, 1));

    // 2. Section heading + duplicate StaticText + View All button + duplicate StaticText.
    dense.push_back(make_ax("main", "", 0, 72, 1280, 720));
    dense.push_back(make_ax("region", "", 24, 80, 1232, 680));
    dense.push_back(make_ax("heading", "Announcements", 32, 88, 220, 28));
    dense.push_back(make_ax("StaticText", "Announcements", 32, 90, 180, 22, "", "full_box"));
    dense.push_back(make_ax("button", "View All", 1140, 88, 96, 32, "BUTTON"));
    dense.push_back(make_ax("StaticText", "View All", 1154, 94, 68, 18, "", "full_box"));

    // 3. Dense 10-row job portal table (empty rows/cells/strong/emphasis wrappers + cell text + action buttons + duplicate StaticText).
    dense.push_back(make_ax("table", "", 32, 132, 1216, 480));
    dense.push_back(make_ax("rowgroup", "", 32, 132, 1216, 40));
    dense.push_back(make_ax("row", "", 32, 132, 1216, 40));
    for (int col = 0; col < 5; ++col) {
      dense.push_back(make_ax("columnheader", "", 32.0 + col * 240.0, 136, 220, 32));
    }
    dense.push_back(make_ax("rowgroup", "", 32, 172, 1216, 440));
    for (int row = 0; row < 10; ++row) {
      const double ry = 176.0 + row * 42.0;
      dense.push_back(make_ax("row", "", 32, ry, 1216, 40));
      dense.push_back(make_ax("cell", "", 32, ry, 340, 40));
      dense.push_back(make_ax("strong", "", 36, ry + 8, 260, 22));
      const std::string title = "Software Intern Role " + std::to_string(row + 1);
      const std::string company = "Example Corp " + std::to_string(row + 1);
      // Composite multi-span link card (title + company inside one <a>).
      dense.push_back(make_ax("link", title + " " + company, 36, ry + 4, 320, 32, "A"));
      dense.push_back(make_ax("StaticText", title, 38, ry + 6, 170, 18, "", "full_box"));
      dense.push_back(make_ax("StaticText", company, 212, ry + 6, 130, 18, "", "full_box"));

      // Location cell (standalone StaticText inside non-interactive cell + emphasis wrapper).
      dense.push_back(make_ax("cell", "", 380, ry, 220, 40));
      dense.push_back(make_ax("emphasis", "", 384, ry + 10, 140, 20));
      dense.push_back(make_ax("StaticText", "Remote / Hybrid", 384, ry + 10, 130, 18, "", "full_box"));

      // Empty metadata cells + decorative icon img + labeltext wrappers (matching real portal DOM).
      dense.push_back(make_ax("cell", "", 610, ry, 180, 40));
      dense.push_back(make_ax("LabelText", "", 614, ry + 10, 120, 20, "LABEL"));
      dense.push_back(make_ax("image", "", 750, ry + 10, 20, 20, "IMG"));
      dense.push_back(make_ax("cell", "", 800, ry, 180, 40));
      dense.push_back(make_ax("StaticText", "Open", 806, ry + 10, 44, 18, "", "full_box"));

      // Action cell with button + duplicate StaticText.
      dense.push_back(make_ax("cell", "", 990, ry, 250, 40));
      dense.push_back(make_ax("button", "Apply", 1000, ry + 4, 88, 32, "BUTTON"));
      dense.push_back(make_ax("StaticText", "Apply", 1018, ry + 10, 46, 18, "", "full_box"));
    }

    // 4. Custom upload popover with standalone StaticText 'Upload from Device' (inside non-interactive container),
    // plus an uncertain candidate and a canvas node (both must retain bounds!).
    dense.push_back(make_ax("group", "", 820, 620, 240, 120));
    Json popover_upload = make_ax("StaticText", "Upload from Device", 832, 634, 150, 20, "", "full_box");
    const std::string popover_upload_ref = popover_upload.at("ref").get<std::string>();
    dense.push_back(popover_upload);
    dense.push_back(make_ax("StaticText", "Upload from Dropbox", 832, 666, 160, 20, "", "full_box"));
    Json uncertain_icon = {
        {"role", "button"},
        {"name", ""},
        {"frameId", "frame-main"},
        {"tag", "BUTTON"},
        {"provenance", "chromium-rendered-text-with-accessibility-role"},
        {"nameProvenance", "withheld_or_unlabeled"},
        {"bounds", {{"x", 1100.4}, {"y", 630.6}, {"width", 32.0}, {"height", 32.0}, {"coordinateSpace", "frame-document-css"}}},
        {"boundsProvenance", "chromium-dom-snapshot"},
        {"geometryEvidence", "unverified_snapshot_bounds"},
        {"visibilityStatus", "uncertain"},
        {"screenshotRequired", true},
        {"visibilityReason", "clipping_or_occlusion_unproven"},
        {"candidateId", "candidate-frame-main-777"},
    };
    dense.push_back(uncertain_icon);
    dense.push_back(make_ax("canvas", "", 40, 620, 300, 100, "CANVAS"));

    // Compute simulated legacy 0.1.13 compact size (default hoisting + bound rounding only, before empty-structural / dup-StaticText / non-interactive-bounds fixes)
    // alongside full size and new compact size.
    Json legacy_compact_nodes = Json::array();
    for (const auto& n : dense) {
      Json copy = n;
      for (const char* k : {"frameId", "provenance", "boundsProvenance", "visibilityStatus", "screenshotRequired"}) {
        if (copy.contains(k) && copy.at(k) == (std::string(k) == "frameId" ? Json("frame-main")
                                               : std::string(k) == "provenance" ? Json("chromium-rendered-text-with-accessibility-role")
                                               : std::string(k) == "boundsProvenance" ? Json("chromium-dom-snapshot")
                                               : std::string(k) == "visibilityStatus" ? Json("verified")
                                               : Json(false))) {
          copy.erase(k);
        }
      }
      if (copy.value("nameProvenance", "") == "filtered_rendered_text") copy.erase("nameProvenance");
      if (copy.value("geometryEvidence", "") == "unoccluded_center_patch") copy.erase("geometryEvidence");
      if (copy.contains("bounds") && copy["bounds"].is_object()) copy["bounds"].erase("coordinateSpace");
      legacy_compact_nodes.push_back(std::move(copy));
    }

    const auto compacted = obs::compact_nodes(dense, "frame-main");
    const size_t full_nodes_bytes = dense.dump().size();
    const size_t legacy_compact_bytes = legacy_compact_nodes.dump().size();
    const size_t new_compact_bytes = compacted.nodes.dump().size();
    const double vs_full_pct = (1.0 - static_cast<double>(new_compact_bytes) / static_cast<double>(full_nodes_bytes)) * 100.0;
    const double vs_legacy_pct = (1.0 - static_cast<double>(new_compact_bytes) / static_cast<double>(legacy_compact_bytes)) * 100.0;
    std::cout << "  Dense real-page DOM (" << dense.size() << " -> " << compacted.nodes.size()
              << " nodes): full=" << full_nodes_bytes << "B, oldCompact=" << legacy_compact_bytes
              << "B -> newCompact=" << new_compact_bytes << "B ("
              << static_cast<int>(std::round(vs_full_pct)) << "% vs full, "
              << static_cast<int>(std::round(vs_legacy_pct)) << "% vs 0.1.13 compact)\n";

    // Must achieve >=60% reduction even vs 0.1.13 compact, and >=80% vs full!
    assert(new_compact_bytes <= legacy_compact_bytes * 0.40);
    assert(new_compact_bytes <= full_nodes_bytes * 0.20);

    // Verify ALL invariants on compacted nodes.
    int unnamed_buttons = 0, searchboxes = 0, uncertain_seen = 0, canvases = 0;
    int headings = 0, apply_buttons = 0, nav_links_seen = 0, job_links_seen = 0;
    int upload_device_seen = 0, remote_texts = 0;
    for (const auto& n : compacted.nodes) {
      const auto role = n.at("role").get<std::string>();
      const auto name = n.at("name").get<std::string>();
      // Empty structural/formatting/decorative wrappers must be completely gone.
      assert(role != "banner" && role != "navigation" && role != "search" && role != "main" &&
             role != "region" && role != "table" && role != "rowgroup" && role != "row" &&
             role != "cell" && role != "columnheader" && role != "list" && role != "listitem" &&
             role != "strong" && role != "emphasis" && role != "separator" && role != "image" &&
             role != "LabelText" && role != "group");
      // Duplicate StaticText inside heading/button/link must be omitted.
      if (role == "StaticText") {
        assert(name != "Announcements" && name != "View All" && name != "Apply" &&
               name != "Dashboard" && name != "Software Intern Role 1" && name != "Example Corp 1");
        assert(!n.contains("bounds"));
        assert(!n.contains("geometryEvidence"));
        assert(n.contains("ref"));
        if (name == "Upload from Device") {
          ++upload_device_seen;
          assert(n.at("ref") == popover_upload_ref);
        }
        if (name == "Remote / Hybrid") ++remote_texts;
      }
      if (role == "heading") {
        ++headings;
        assert(name == "Announcements");
        assert(!n.contains("bounds"));
        assert(n.contains("ref"));
      }
      if (role == "button" && n.value("visibilityStatus", "verified") == "uncertain") {
        ++uncertain_seen;
        assert(n.at("candidateId") == "candidate-frame-main-777");
        assert(n.at("screenshotRequired") == true);
        assert(n.contains("bounds"));
        assert(!n.contains("ref"));
      } else if (role == "button" && name.empty()) {
        ++unnamed_buttons;
        assert(n.at("nameProvenance") == "withheld_or_unlabeled");
        assert(n.contains("bounds") && n.contains("ref"));
      } else if (role == "button" && name == "Apply") {
        ++apply_buttons;
        assert(n.contains("bounds") && n.contains("ref"));
      } else if (role == "searchbox") {
        ++searchboxes;
        assert(name.empty() && n.at("inputType") == "search" && n.contains("bounds") && n.contains("ref"));
      } else if (role == "canvas") {
        ++canvases;
        assert(n.contains("bounds"));
      } else if (role == "link") {
        assert(n.contains("bounds") && n.contains("ref"));
        if (name.starts_with("Software Intern Role")) ++job_links_seen;
        else ++nav_links_seen;
      }
    }
    assert(unnamed_buttons == 2);
    assert(searchboxes == 1);
    assert(uncertain_seen == 1);
    assert(canvases == 1);
    assert(headings == 1);
    assert(apply_buttons == 10);
    assert(nav_links_seen == 5);
    assert(job_links_seen == 10);
    assert(upload_device_seen == 1);
    assert(remote_texts == 10);

    // Compact delta consistency: adding/jittering empty wrappers or StaticText bounds must stay unchanged=true;
    // mutating a control must emit only the compacted delta without resurrecting omitted nodes.
    Json dense_jitter = dense;
    dense_jitter.push_back(make_ax("cell", "", 500, 500, 40, 20)); // omitted empty cell added
    for (auto& n : dense_jitter) {
      if (n.value("role", "") == "StaticText" && n.value("name", "") == "Upload from Device") {
        n["bounds"]["y"] = 634.4; // non-interactive bounds jitter
      }
    }
    const auto compacted_jitter = obs::compact_nodes(dense_jitter, "frame-main");
    const auto idle_compact_delta = obs::compute_delta(compacted.nodes, compacted_jitter.nodes);
    assert(idle_compact_delta.unchanged);
    assert(idle_compact_delta.added.empty() && idle_compact_delta.removed.empty() && idle_compact_delta.changed.empty());

    Json dense_mutated = dense;
    for (auto& n : dense_mutated) {
      if (n.value("name", "") == "View All") n["name"] = "Show Less";
    }
    const auto compacted_mutated = obs::compact_nodes(dense_mutated, "frame-main");
    const auto mutated_delta = obs::compute_delta(compacted.nodes, compacted_mutated.nodes);
    assert(!mutated_delta.unchanged);
    assert(mutated_delta.added.size() == 1);
    assert(mutated_delta.added[0].at("role") == "button" && mutated_delta.added[0].at("name") == "Show Less");
    assert(mutated_delta.removed.size() == 1);
    assert(mutated_delta.removed[0].at("role") == "button" && mutated_delta.removed[0].at("name") == "View All");
    assert(!mutated_delta.removed[0].contains("ref"));
    assert(mutated_delta.changed.empty());
  });

  test("should_retry_transient_capture retries only clean transient inspect/guard races once", [] {
    obs::TransientCaptureState base;
    base.attempt = 0;
    base.error_code = "inspection_changed";
    base.start_epoch = base.current_epoch = 3;
    base.start_human_input = base.current_human_input = 1;
    base.start_document = base.current_document = "doc-1";
    base.ownership_valid = true;

    assert(obs::should_retry_transient_capture(base));

    auto guard_init = base;
    guard_init.error_code = "privacy_guard_initializing";
    assert(obs::should_retry_transient_capture(guard_init));

    // Second attempt must fail closed.
    auto second = base;
    second.attempt = 1;
    assert(!obs::should_retry_transient_capture(second));

    // Non-transient or protected codes must never retry.
    for (const char* non_retryable : {"screenshot_protected", "protected_auth", "screen_sharing",
                                      "HUMAN_INPUT_PAUSED", "OWNERSHIP_CHANGED", "page_changed",
                                      "screenshot_unverified", "tab_closed"}) {
      auto s = base;
      s.error_code = non_retryable;
      assert(!obs::should_retry_transient_capture(s));
    }

    // Any state invalidation blocks retry.
    { auto s = base; s.closed = true; assert(!obs::should_retry_transient_capture(s)); }
    { auto s = base; s.protected_auth = true; assert(!obs::should_retry_transient_capture(s)); }
    { auto s = base; s.screen_sharing = true; assert(!obs::should_retry_transient_capture(s)); }
    { auto s = base; s.human_busy = true; assert(!obs::should_retry_transient_capture(s)); }
    { auto s = base; s.ownership_valid = false; assert(!obs::should_retry_transient_capture(s)); }
    { auto s = base; s.current_epoch = 4; assert(!obs::should_retry_transient_capture(s)); }
    { auto s = base; s.current_human_input = 2; assert(!obs::should_retry_transient_capture(s)); }
    { auto s = base; s.current_document = "doc-2"; assert(!obs::should_retry_transient_capture(s)); }
  });

  std::cout << "All observation_format_tests passed\n";
  return 0;
}
