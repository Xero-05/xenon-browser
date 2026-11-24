#pragma once

#include "xenon/contracts.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xenon::observation {

inline std::string ascii_lower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    const auto byte = static_cast<unsigned char>(ch);
    out.push_back((byte >= 'A' && byte <= 'Z')
                      ? static_cast<char>(byte + ('a' - 'A'))
                      : ch);
  }
  return out;
}

inline bool is_interactive_role(std::string_view role) {
  if (role.size() < 3 || role.size() > 16) return false;
  const std::string lower = ascii_lower(role);
  return lower == "button" || lower == "link" || lower == "textbox" ||
         lower == "searchbox" || lower == "combobox" || lower == "checkbox" ||
         lower == "radio" || lower == "switch" || lower == "menuitem" ||
         lower == "menuitemcheckbox" || lower == "menuitemradio" ||
         lower == "tab" || lower == "option" || lower == "slider" ||
         lower == "spinbutton" || lower == "listbox" || lower == "treeitem";
}

struct Filter {
  std::string query_lower;
  std::set<std::string> roles_lower;
  bool interactive_only{false};
};

inline Filter parse_filter(const Json& params) {
  Filter filter;
  if (!params.is_object()) return filter;

  if (const auto it = params.find("query"); it != params.end() && it->is_string()) {
    filter.query_lower = ascii_lower(it->get_ref<const std::string&>());
  }

  if (const auto it = params.find("roles"); it != params.end() && it->is_array()) {
    constexpr size_t kMaxRoles = 32;
    size_t seen = 0;
    for (const auto& entry : *it) {
      if (seen >= kMaxRoles) break;
      ++seen;
      if (!entry.is_string()) continue;
      std::string lower = ascii_lower(entry.get_ref<const std::string&>());
      if (!lower.empty()) {
        filter.roles_lower.insert(std::move(lower));
      }
    }
  }

  if (const auto it = params.find("interactiveOnly");
      it != params.end() && it->is_boolean()) {
    filter.interactive_only = it->get<bool>();
  }

  return filter;
}

inline bool matches_filter(const Filter& filter, std::string_view role,
                           std::string_view name) {
  if (filter.interactive_only || !filter.roles_lower.empty()) {
    const std::string role_lower = ascii_lower(role);
    if (filter.interactive_only && !is_interactive_role(role_lower)) {
      return false;
    }
    if (!filter.roles_lower.empty() && !filter.roles_lower.contains(role_lower)) {
      return false;
    }
  }

  if (!filter.query_lower.empty()) {
    if (ascii_lower(name).find(filter.query_lower) == std::string::npos) {
      return false;
    }
  }

  return true;
}

struct CompactResult {
  Json nodes = Json::array();
  Json node_defaults = Json::object();
};

inline CompactResult compact_nodes(const Json& nodes,
                                   const std::string& default_frame_id) {
  CompactResult result;
  result.node_defaults = {
      {"frameId", default_frame_id},
      {"provenance", "chromium-rendered-text-with-accessibility-role"},
      {"nameProvenance", "filtered_rendered_text"},
      {"boundsProvenance", "chromium-dom-snapshot"},
      {"geometryEvidence", "unoccluded_center_patch"},
      {"visibilityStatus", "verified"},
      {"screenshotRequired", false},
      {"boundsCoordinateSpace", "frame-document-css"},
  };
  result.nodes = Json::array();

  if (!nodes.is_array()) return result;

  static constexpr const char* kDefaultKeys[] = {
      "provenance",       "nameProvenance",   "boundsProvenance",
      "geometryEvidence", "visibilityStatus", "screenshotRequired",
  };
  const auto& default_coord_space = result.node_defaults.at("boundsCoordinateSpace");

  for (const auto& node : nodes) {
    if (!node.is_object()) {
      result.nodes.push_back(node);
      continue;
    }

    Json copy = node;
    if (!default_frame_id.empty()) {
      if (const auto it = copy.find("frameId");
          it != copy.end() && *it == result.node_defaults.at("frameId")) {
        copy.erase(it);
      }
    }

    for (const char* key : kDefaultKeys) {
      if (const auto it = copy.find(key);
          it != copy.end() && *it == result.node_defaults.at(key)) {
        copy.erase(it);
      }
    }

    if (const auto bounds_it = copy.find("bounds");
        bounds_it != copy.end() && bounds_it->is_object()) {
      if (const auto cs_it = bounds_it->find("coordinateSpace");
          cs_it != bounds_it->end() && *cs_it == default_coord_space) {
        bounds_it->erase(cs_it);
      }
    }

    result.nodes.push_back(std::move(copy));
  }

  return result;
}

namespace detail {

inline std::string_view string_field(const Json& obj, const char* key) {
  if (!obj.is_object()) return {};
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_string()) return {};
  return it->get_ref<const std::string&>();
}

inline void append_tagged_field(std::string& out, char tag, std::string_view value) {
  out.push_back(tag);
  out.push_back('=');
  out.append(std::to_string(value.size()));
  out.push_back(':');
  out.append(value);
  out.push_back('|');
}

inline bool read_rounded_coord(const Json& obj, const char* key, long long& out) {
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_number()) return false;
  const double value = it->get<double>();
  constexpr double kMaxSafeCoord = 1e15;
  if (!std::isfinite(value) || value < -kMaxSafeCoord || value > kMaxSafeCoord) {
    return false;
  }
  out = std::llround(value);
  return true;
}

inline std::string node_identity_base(const Json& node) {
  std::string key;
  key.reserve(96);

  append_tagged_field(key, 'f', string_field(node, "frameId"));
  append_tagged_field(key, 'r', string_field(node, "role"));
  append_tagged_field(key, 't', string_field(node, "tag"));
  append_tagged_field(key, 'i', string_field(node, "inputType"));

  const std::string_view candidate_id = string_field(node, "candidateId");
  if (!candidate_id.empty()) {
    append_tagged_field(key, 'c', candidate_id);
  } else {
    const std::string_view name = string_field(node, "name");
    if (!name.empty()) {
      append_tagged_field(key, 'n', name);
    } else {
      long long rx = 0;
      long long ry = 0;
      long long rw = 0;
      long long rh = 0;
      const auto bounds_it = node.is_object() ? node.find("bounds") : node.end();
      if (node.is_object() && bounds_it != node.end() && bounds_it->is_object() &&
          read_rounded_coord(*bounds_it, "x", rx) &&
          read_rounded_coord(*bounds_it, "y", ry) &&
          read_rounded_coord(*bounds_it, "width", rw) &&
          read_rounded_coord(*bounds_it, "height", rh)) {
        key.append("b=");
        key.append(std::to_string(rx));
        key.push_back(',');
        key.append(std::to_string(ry));
        key.push_back(',');
        key.append(std::to_string(rw));
        key.push_back(',');
        key.append(std::to_string(rh));
        key.push_back('|');
      } else {
        key.append("b=none|");
      }
    }
  }

  return key;
}

inline std::string format_identity_key(const std::string& base, size_t occurrence) {
  std::string key = base;
  key.push_back('#');
  key.append(std::to_string(occurrence));
  return key;
}

}  // namespace detail

struct DeltaResult {
  bool unchanged{false};
  Json added = Json::array();
  Json removed = Json::array();
  Json changed = Json::array();
};

inline Json comparable_node_signature(const Json& node) {
  Json copy = node;
  if (copy.is_object()) {
    copy.erase("ref");
    copy.erase("diagnosticId");
  }
  return copy;
}

inline std::string node_identity_key(const Json& node, size_t occurrence) {
  return detail::format_identity_key(detail::node_identity_base(node), occurrence);
}

inline DeltaResult compute_delta(const Json& previous_nodes,
                                 const Json& current_nodes) {
  DeltaResult result;
  const Json empty = Json::array();
  const Json& prev = previous_nodes.is_array() ? previous_nodes : empty;
  const Json& curr = current_nodes.is_array() ? current_nodes : empty;

  if (prev.size() == curr.size()) {
    bool all_equal = true;
    for (size_t i = 0; i < prev.size(); ++i) {
      if (comparable_node_signature(prev[i]) != comparable_node_signature(curr[i])) {
        all_equal = false;
        break;
      }
    }
    if (all_equal) {
      result.unchanged = true;
      return result;
    }
  }

  struct PreviousEntry {
    Json signature;
    size_t index{0};
    bool matched{false};
  };

  std::vector<PreviousEntry> prev_entries;
  prev_entries.reserve(prev.size());
  std::unordered_map<std::string, size_t> prev_counts;
  std::unordered_map<std::string, size_t> prev_by_key;
  prev_by_key.reserve(prev.size());

  for (size_t i = 0; i < prev.size(); ++i) {
    const std::string base = detail::node_identity_base(prev[i]);
    const size_t occurrence = prev_counts[base]++;
    std::string key = detail::format_identity_key(base, occurrence);
    const size_t entry_index = prev_entries.size();
    prev_entries.push_back({comparable_node_signature(prev[i]), i, false});
    prev_by_key.emplace(std::move(key), entry_index);
  }

  std::unordered_map<std::string, size_t> curr_counts;
  for (size_t i = 0; i < curr.size(); ++i) {
    const std::string base = detail::node_identity_base(curr[i]);
    const size_t occurrence = curr_counts[base]++;
    const std::string key = detail::format_identity_key(base, occurrence);

    const auto found = prev_by_key.find(key);
    if (found == prev_by_key.end()) {
      result.added.push_back(curr[i]);
      continue;
    }

    auto& entry = prev_entries[found->second];
    entry.matched = true;
    if (comparable_node_signature(curr[i]) != entry.signature) {
      result.changed.push_back(curr[i]);
    }
  }

  for (const auto& entry : prev_entries) {
    if (entry.matched) continue;
    Json removed_node = prev[entry.index];
    if (removed_node.is_object()) {
      removed_node.erase("ref");
    }
    result.removed.push_back(std::move(removed_node));
  }

  return result;
}

struct WaitQuery {
  std::vector<std::string> needles;
  std::vector<std::string> needles_lower;
  bool absent{false};
  std::string return_mode{"full"};
};

inline WaitQuery parse_wait_query(const Json& params) {
  WaitQuery query;
  if (!params.is_object()) return query;

  constexpr size_t kMaxNeedles = 16;
  constexpr size_t kMaxNeedleBytes = 1000;
  std::set<std::string> seen_lower;

  auto add_needle = [&](const Json& value) {
    if (query.needles.size() >= kMaxNeedles) return;
    if (!value.is_string()) return;
    const auto& raw = value.get_ref<const std::string&>();
    if (raw.empty() || raw.size() > kMaxNeedleBytes) return;
    std::string lower = ascii_lower(raw);
    if (!seen_lower.insert(lower).second) return;
    query.needles.push_back(raw);
    query.needles_lower.push_back(std::move(lower));
  };

  if (const auto it = params.find("text"); it != params.end()) {
    add_needle(*it);
  }

  if (const auto it = params.find("anyOf"); it != params.end() && it->is_array()) {
    constexpr size_t kMaxInspect = 64;
    size_t inspected = 0;
    for (const auto& item : *it) {
      if (query.needles.size() >= kMaxNeedles || inspected >= kMaxInspect) break;
      ++inspected;
      add_needle(item);
    }
  }

  if (const auto it = params.find("absent"); it != params.end() && it->is_boolean()) {
    query.absent = it->get<bool>();
  }

  if (const auto it = params.find("returnMode");
      it != params.end() && it->is_string()) {
    const auto& mode = it->get_ref<const std::string&>();
    if (mode == "full" || mode == "matching" || mode == "none") {
      query.return_mode = mode;
    }
  }

  return query;
}

struct WaitEvaluation {
  bool matched{false};
  std::string matched_text;
  Json matching_nodes = Json::array();
};

inline WaitEvaluation evaluate_wait(const WaitQuery& query, const Json& nodes) {
  WaitEvaluation result;
  result.matching_nodes = Json::array();

  if (query.needles.empty()) {
    return result;
  }

  std::vector<std::string> fallback_lower;
  const std::vector<std::string>* needles_lower = &query.needles_lower;
  if (query.needles_lower.size() != query.needles.size()) {
    fallback_lower.reserve(query.needles.size());
    for (const auto& needle : query.needles) {
      fallback_lower.push_back(ascii_lower(needle));
    }
    needles_lower = &fallback_lower;
  }

  bool any_non_empty_needle = false;
  for (const auto& lower : *needles_lower) {
    if (!lower.empty()) {
      any_non_empty_needle = true;
      break;
    }
  }
  if (!any_non_empty_needle) {
    return result;
  }

  constexpr size_t kNoMatch = std::string::npos;
  size_t first_needle_index = kNoMatch;
  Json matches = Json::array();

  if (nodes.is_array()) {
    for (const auto& node : nodes) {
      if (!node.is_object()) continue;
      const auto name_it = node.find("name");
      if (name_it == node.end() || !name_it->is_string()) continue;
      const std::string lower_name = ascii_lower(name_it->get_ref<const std::string&>());

      bool node_matched = false;
      for (size_t i = 0; i < needles_lower->size(); ++i) {
        const auto& needle_lower = (*needles_lower)[i];
        if (needle_lower.empty()) continue;
        if (lower_name.find(needle_lower) != std::string::npos) {
          if (i < first_needle_index) {
            first_needle_index = i;
          }
          node_matched = true;
          break;
        }
      }

      if (node_matched) {
        matches.push_back(node);
      }
    }
  }

  if (query.absent) {
    result.matched = (first_needle_index == kNoMatch);
    result.matched_text.clear();
    result.matching_nodes = Json::array();
    return result;
  }

  if (first_needle_index != kNoMatch) {
    result.matched = true;
    result.matched_text = query.needles[first_needle_index];
    result.matching_nodes = std::move(matches);
  }
  return result;
}

inline std::string_view normalize_stale_reason(std::string_view reason) {
  static constexpr std::string_view kAllowed[] = {
      "document_navigated",     "node_detached",     "out_of_viewport",
      "occluded",               "human_activity",    "observation_superseded",
      "identity_changed",       "visibility_changed", "frame_changed",
  };
  for (const std::string_view allowed : kAllowed) {
    if (reason == allowed) {
      return allowed;
    }
  }
  return "observation_superseded";
}

}  // namespace xenon::observation
