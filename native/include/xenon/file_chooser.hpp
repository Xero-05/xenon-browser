#pragma once

#include "xenon/contracts.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace xenon::file_chooser {

enum class ChooserMode { open, open_multiple, unsupported };

inline std::string_view mode_name(ChooserMode mode) {
  switch (mode) {
    case ChooserMode::open:
      return "open";
    case ChooserMode::open_multiple:
      return "openMultiple";
    case ChooserMode::unsupported:
    default:
      return "unsupported";
  }
}

inline bool is_ascii_alnum(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

// Strict allowlist for website-supplied accept_filters:
// - file extension: .[A-Za-z0-9+-]+
// - MIME type:      [A-Za-z0-9.+_-]+/[A-Za-z0-9.*+_-]+
// Bounded to 1..64 printable ASCII bytes. Never admits paths, spaces, quotes or descriptions.
inline bool is_safe_accept_filter(std::string_view value) {
  constexpr size_t kMaxFilterBytes = 64;
  if (value.empty() || value.size() > kMaxFilterBytes) return false;
  for (const char ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte <= 0x20 || byte >= 0x7f) return false;
  }
  if (value.front() == '.') {
    if (value.size() < 2) return false;
    for (size_t i = 1; i < value.size(); ++i) {
      const auto byte = static_cast<unsigned char>(value[i]);
      if (!is_ascii_alnum(byte) && byte != '+' && byte != '-') return false;
    }
    return true;
  }
  const size_t slash = value.find('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= value.size()) return false;
  if (value.find('/', slash + 1) != std::string_view::npos) return false;
  for (size_t i = 0; i < slash; ++i) {
    const auto byte = static_cast<unsigned char>(value[i]);
    if (!is_ascii_alnum(byte) && byte != '.' && byte != '+' && byte != '_' && byte != '-') {
      return false;
    }
  }
  for (size_t i = slash + 1; i < value.size(); ++i) {
    const auto byte = static_cast<unsigned char>(value[i]);
    if (!is_ascii_alnum(byte) && byte != '.' && byte != '*' && byte != '+' && byte != '_' &&
        byte != '-') {
      return false;
    }
  }
  return true;
}

inline std::vector<std::string> sanitize_accept_filters(const std::vector<std::string>& raw) {
  constexpr size_t kMaxFilters = 16;
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const auto& entry : raw) {
    if (out.size() >= kMaxFilters) break;
    if (!is_safe_accept_filter(entry)) continue;
    if (!seen.insert(entry).second) continue;
    out.push_back(entry);
  }
  return out;
}

inline Json make_pending_chooser_json(const std::string& chooser_id, ChooserMode mode,
                                      const std::vector<std::string>& accept_filters,
                                      bool include_status = true) {
  Json out{
      {"chooserId", chooser_id},
      {"mode", std::string(mode_name(mode))},
      {"acceptFilters", accept_filters},
  };
  if (include_status) out["status"] = "pending";
  return out;
}

inline Json make_rejected_chooser_json(std::string_view status, std::string_view reason) {
  return Json{
      {"status", std::string(status)},
      {"reason", std::string(reason)},
  };
}

// Validates the three mutually exclusive files.upload modes at the native broker:
// - Direct:              no chooserId, no cancel (observationId enforced by target_commands)
// - Fulfill intercepted: chooserId + fileId (no observationId, elementRef, or cancel)
// - Cancel intercepted:  chooserId + cancel: true (no observationId, elementRef, or fileId)
inline bool valid_upload_params(const Json& params) {
  if (!params.is_object()) return false;
  const bool has_chooser = params.contains("chooserId");
  const bool has_observation = params.contains("observationId");
  const bool has_element = params.contains("elementRef");
  const bool has_file = params.contains("fileId");
  const bool has_cancel = params.contains("cancel");
  if (has_chooser) {
    if (has_observation || has_element) return false;
    if (has_cancel) {
      if (has_file) return false;
      return params["cancel"].is_boolean() && params["cancel"].get<bool>();
    }
    return has_file;
  }
  if (has_cancel) return false;
  return true;
}

}  // namespace xenon::file_chooser
