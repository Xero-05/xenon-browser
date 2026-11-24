#pragma once

#include "xenon/contracts.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace xenon::screenshot {

struct CssClip {
  double x{};
  double y{};
  double width{};
  double height{};
  std::string source;  // "clip" or "candidate"
  std::string candidate_id;
};

struct PixelRect {
  uint32_t x{};
  uint32_t y{};
  uint32_t width{};
  uint32_t height{};
};

struct ClipResolution {
  bool requested{false};
  bool ok{true};
  std::string error_code;
  std::string error_message;
  CssClip css;
};

namespace detail {

inline constexpr double kViewportEpsilon = 1e-6;
inline constexpr std::string::size_type kMaxCandidateIdBytes = 256;

inline ClipResolution clip_error(std::string code, std::string message) {
  ClipResolution result;
  result.requested = true;
  result.ok = false;
  result.error_code = std::move(code);
  result.error_message = std::move(message);
  return result;
}

inline std::optional<double> finite_number(const Json& obj, const char* key) {
  if (!obj.is_object() || !obj.contains(key)) return std::nullopt;
  const auto& value = obj.at(key);
  if (!value.is_number()) return std::nullopt;
  const double number = value.get<double>();
  if (!std::isfinite(number)) return std::nullopt;
  return number;
}

inline double normalize_zero(double value) {
  return value == 0.0 ? 0.0 : value;
}

struct ViewportMetrics {
  double page_x{};
  double page_y{};
  double width{};
  double height{};
};

inline std::optional<ViewportMetrics> parse_viewport(const Json& visual_viewport) {
  if (!visual_viewport.is_object()) return std::nullopt;
  const auto width = visual_viewport.contains("clientWidth")
                         ? finite_number(visual_viewport, "clientWidth")
                         : finite_number(visual_viewport, "width");
  const auto height = visual_viewport.contains("clientHeight")
                          ? finite_number(visual_viewport, "clientHeight")
                          : finite_number(visual_viewport, "height");
  if (!width || !height || *width <= 0.0 || *height <= 0.0) return std::nullopt;

  double page_x = 0.0;
  if (visual_viewport.contains("pageX")) {
    const auto parsed = finite_number(visual_viewport, "pageX");
    if (!parsed) return std::nullopt;
    page_x = *parsed;
  }
  double page_y = 0.0;
  if (visual_viewport.contains("pageY")) {
    const auto parsed = finite_number(visual_viewport, "pageY");
    if (!parsed) return std::nullopt;
    page_y = *parsed;
  }
  return ViewportMetrics{page_x, page_y, *width, *height};
}

}  // namespace detail

inline ClipResolution resolve_clip(
    const Json& params,
    const Json& visual_viewport,
    const std::optional<Json>& candidate_bounds = std::nullopt,
    double candidate_padding_css = 24.0) {
  if (!params.is_object()) return ClipResolution{};
  const bool has_clip = params.contains("clip");
  const bool has_candidate = params.contains("candidateId");
  if (!has_clip && !has_candidate) return ClipResolution{};
  if (has_clip && has_candidate) {
    return detail::clip_error("invalid_clip", "Specify either clip or candidateId, not both.");
  }

  if (has_clip) {
    const auto& clip = params.at("clip");
    if (!clip.is_object()) {
      return detail::clip_error("invalid_clip", "clip must be a JSON object with finite numeric x, y, width and height.");
    }
    const auto x = detail::finite_number(clip, "x");
    const auto y = detail::finite_number(clip, "y");
    const auto width = detail::finite_number(clip, "width");
    const auto height = detail::finite_number(clip, "height");
    if (!x || !y || !width || !height) {
      return detail::clip_error("invalid_clip", "clip requires finite numeric x, y, width and height.");
    }
    if (*x < 0.0 || *y < 0.0 || *width < 1.0 || *height < 1.0) {
      return detail::clip_error("invalid_clip", "clip requires x >= 0, y >= 0, width >= 1 and height >= 1.");
    }

    const auto viewport = detail::parse_viewport(visual_viewport);
    if (!viewport) {
      return detail::clip_error("viewport_unavailable", "The visual viewport is unavailable.");
    }

    const double right = *x + *width;
    const double bottom = *y + *height;
    if (!std::isfinite(right) || !std::isfinite(bottom) ||
        right > viewport->width + detail::kViewportEpsilon ||
        bottom > viewport->height + detail::kViewportEpsilon) {
      return detail::clip_error("invalid_clip", "clip exceeds the visual viewport bounds.");
    }

    const double clamped_x = detail::normalize_zero(*x);
    const double clamped_y = detail::normalize_zero(*y);
    const double clamped_width = detail::normalize_zero(
        right > viewport->width ? viewport->width - clamped_x : *width);
    const double clamped_height = detail::normalize_zero(
        bottom > viewport->height ? viewport->height - clamped_y : *height);
    if (clamped_width <= 0.0 || clamped_height <= 0.0) {
      return detail::clip_error("invalid_clip", "clip exceeds the visual viewport bounds.");
    }

    ClipResolution result;
    result.requested = true;
    result.ok = true;
    result.css = CssClip{clamped_x, clamped_y, clamped_width, clamped_height, "clip", {}};
    return result;
  }

  const auto& candidate_value = params.at("candidateId");
  if (!candidate_value.is_string()) {
    return detail::clip_error("invalid_clip", "candidateId must be a non-empty string of at most 256 bytes.");
  }
  const auto& candidate_id = candidate_value.get_ref<const std::string&>();
  if (candidate_id.empty() || candidate_id.size() > detail::kMaxCandidateIdBytes) {
    return detail::clip_error("invalid_clip", "candidateId must be a non-empty string of at most 256 bytes.");
  }
  if (!std::isfinite(candidate_padding_css) || candidate_padding_css < 0.0) {
    return detail::clip_error("invalid_clip", "Candidate clip padding must be a finite non-negative number.");
  }
  if (!candidate_bounds.has_value()) {
    return detail::clip_error("stale_candidate", "The requested candidate is not available in the current observation. Observe again.");
  }
  const auto& bounds = *candidate_bounds;
  if (!bounds.is_object()) {
    return detail::clip_error("invalid_clip", "Candidate bounds must be a JSON object with finite numeric x, y, width and height.");
  }
  const auto bx = detail::finite_number(bounds, "x");
  const auto by = detail::finite_number(bounds, "y");
  const auto bw = detail::finite_number(bounds, "width");
  const auto bh = detail::finite_number(bounds, "height");
  if (!bx || !by || !bw || !bh || *bw <= 0.0 || *bh <= 0.0) {
    return detail::clip_error("invalid_clip", "Candidate bounds require finite numeric x, y and positive width and height.");
  }

  const auto viewport = detail::parse_viewport(visual_viewport);
  if (!viewport) {
    return detail::clip_error("viewport_unavailable", "The visual viewport is unavailable.");
  }

  const double vx = *bx - viewport->page_x;
  const double vy = *by - viewport->page_y;
  const double left = vx - candidate_padding_css;
  const double top = vy - candidate_padding_css;
  const double right = vx + *bw + candidate_padding_css;
  const double bottom = vy + *bh + candidate_padding_css;
  if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom)) {
    return detail::clip_error("invalid_clip", "Candidate bounds produced non-finite viewport coordinates.");
  }

  const double clamped_left = detail::normalize_zero(std::clamp(left, 0.0, viewport->width));
  const double clamped_top = detail::normalize_zero(std::clamp(top, 0.0, viewport->height));
  const double clamped_right = std::clamp(right, 0.0, viewport->width);
  const double clamped_bottom = std::clamp(bottom, 0.0, viewport->height);
  const double clamped_width = detail::normalize_zero(clamped_right - clamped_left);
  const double clamped_height = detail::normalize_zero(clamped_bottom - clamped_top);
  if (clamped_width < 1.0 || clamped_height < 1.0) {
    return detail::clip_error("clip_outside_viewport", "The requested candidate lies outside the visual viewport.");
  }

  ClipResolution result;
  result.requested = true;
  result.ok = true;
  result.css = CssClip{clamped_left, clamped_top, clamped_width, clamped_height, "candidate", candidate_id};
  return result;
}

inline std::optional<PixelRect> to_pixel_rect(
    const CssClip& css,
    double scale_x,
    double scale_y,
    uint32_t image_width,
    uint32_t image_height) {
  if (!std::isfinite(scale_x) || !std::isfinite(scale_y) || scale_x <= 0.0 || scale_y <= 0.0) {
    return std::nullopt;
  }
  if (image_width == 0 || image_height == 0) {
    return std::nullopt;
  }
  if (!std::isfinite(css.x) || !std::isfinite(css.y) ||
      !std::isfinite(css.width) || !std::isfinite(css.height) ||
      css.x < 0.0 || css.y < 0.0 || css.width <= 0.0 || css.height <= 0.0) {
    return std::nullopt;
  }

  const double raw_left = std::floor(css.x * scale_x);
  const double raw_top = std::floor(css.y * scale_y);
  const double raw_right = std::ceil((css.x + css.width) * scale_x);
  const double raw_bottom = std::ceil((css.y + css.height) * scale_y);
  if (!std::isfinite(raw_left) || !std::isfinite(raw_top) ||
      !std::isfinite(raw_right) || !std::isfinite(raw_bottom)) {
    return std::nullopt;
  }

  const double max_left = static_cast<double>(image_width - 1u);
  const double max_top = static_cast<double>(image_height - 1u);
  const double max_right = static_cast<double>(image_width);
  const double max_bottom = static_cast<double>(image_height);

  const uint32_t left = static_cast<uint32_t>(std::clamp(raw_left, 0.0, max_left));
  const uint32_t top = static_cast<uint32_t>(std::clamp(raw_top, 0.0, max_top));
  const uint32_t right = static_cast<uint32_t>(std::clamp(raw_right, static_cast<double>(left + 1u), max_right));
  const uint32_t bottom = static_cast<uint32_t>(std::clamp(raw_bottom, static_cast<double>(top + 1u), max_bottom));
  if (right <= left || bottom <= top) return std::nullopt;

  return PixelRect{left, top, right - left, bottom - top};
}

inline Json clip_metadata(
    const CssClip& css,
    const PixelRect& px,
    double scale_x,
    double scale_y) {
  const bool valid_x = std::isfinite(scale_x) && scale_x > 0.0;
  const bool valid_y = std::isfinite(scale_y) && scale_y > 0.0;
  const double x = detail::normalize_zero(valid_x ? static_cast<double>(px.x) / scale_x : css.x);
  const double y = detail::normalize_zero(valid_y ? static_cast<double>(px.y) / scale_y : css.y);
  const double width = detail::normalize_zero(valid_x ? static_cast<double>(px.width) / scale_x : css.width);
  const double height = detail::normalize_zero(valid_y ? static_cast<double>(px.height) / scale_y : css.height);

  Json meta{
      {"x", x},
      {"y", y},
      {"width", width},
      {"height", height},
      {"source", css.source},
      {"coordinateSpace", "viewport-css"},
      {"coordinateMapping", "CSS x = clip.x + image x / scaleX; CSS y = clip.y + image y / scaleY"},
      {"imageOriginX", px.x},
      {"imageOriginY", px.y},
  };
  if (!css.candidate_id.empty()) {
    meta["candidateId"] = css.candidate_id;
  }
  return meta;
}

}  // namespace xenon::screenshot
