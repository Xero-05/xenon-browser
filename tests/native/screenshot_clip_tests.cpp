#include "xenon/screenshot_clip.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace xenon;
using namespace xenon::screenshot;

void require(bool value, const char* message) {
  assert(value);
  if (!value) throw std::runtime_error(message);
}

bool approx(double a, double b, double eps = 1e-9) {
  return std::abs(a - b) <= eps;
}

Json visual_viewport(double width, double height, double page_x = 0.0, double page_y = 0.0) {
  return {{"pageX", page_x}, {"pageY", page_y}, {"clientWidth", width}, {"clientHeight", height}};
}

Json fallback_viewport(double width, double height, double page_x = 0.0, double page_y = 0.0) {
  return {{"pageX", page_x}, {"pageY", page_y}, {"width", width}, {"height", height}};
}

int main() {
  try {
    const auto vp = visual_viewport(917.0, 674.0, 120.0, 340.0);
    const auto vp_fallback = fallback_viewport(917.0, 674.0, 120.0, 340.0);

    // Unclipped requests return requested=false, ok=true.
    {
      const auto empty = resolve_clip(Json::object(), vp);
      require(!empty.requested && empty.ok && empty.error_code.empty(), "Empty params request no clip");
      const auto unrelated = resolve_clip({{"tabId", "tab-1"}}, vp);
      require(!unrelated.requested && unrelated.ok, "Params without clip or candidateId request no clip");
      const auto null_params = resolve_clip(Json(), vp);
      require(!null_params.requested && null_params.ok, "Non-object params request no clip");
    }

    // Valid explicit clips: clientWidth/clientHeight, width/height fallback, exact edges, fractional, epsilon clamp.
    {
      const auto res = resolve_clip({{"clip", {{"x", 10.0}, {"y", 20.5}, {"width", 300.25}, {"height", 150.5}}}}, vp);
      require(res.requested && res.ok, "Valid explicit clip resolves");
      require(res.css.source == "clip" && res.css.candidate_id.empty(), "Explicit clip sets source=clip and empty candidate_id");
      require(res.css.x == 10.0 && res.css.y == 20.5 && res.css.width == 300.25 && res.css.height == 150.5, "Exact clip coordinates are preserved");

      const auto fb = resolve_clip({{"clip", {{"x", 0.0}, {"y", 0.0}, {"width", 917.0}, {"height", 674.0}}}}, vp_fallback);
      require(fb.requested && fb.ok && fb.css.width == 917.0 && fb.css.height == 674.0, "Fallback width/height viewport accepts full-viewport clip");

      const auto minimal_vp = Json{{"width", 800.0}, {"height", 600.0}};
      const auto one_px = resolve_clip({{"clip", {{"x", 799.0}, {"y", 599.0}, {"width", 1.0}, {"height", 1.0}}}}, minimal_vp);
      require(one_px.requested && one_px.ok && one_px.css.x == 799.0 && one_px.css.width == 1.0, "Minimum 1x1 clip at far edge succeeds without pageX/pageY");

      // Tiny floating-point epsilon overshoot is tolerated and clamped to viewport dimensions.
      const auto frac_vp = visual_viewport(1052.8, 674.4);
      const auto eps = resolve_clip({{"clip", {{"x", 52.8}, {"y", 74.4}, {"width", 1000.0 + 5e-7}, {"height", 600.0 + 5e-7}}}}, frac_vp);
      require(eps.requested && eps.ok, "Tiny epsilon overshoot at right/bottom edge is allowed");
      require(eps.css.x == 52.8 && eps.css.y == 74.4, "Epsilon clamp preserves clip origin");
      require(eps.css.x + eps.css.width <= 1052.8 && approx(eps.css.width, 1000.0, 1e-12), "Epsilon overshoot on width clamps to viewport width");
      require(eps.css.y + eps.css.height <= 674.4 && approx(eps.css.height, 600.0, 1e-12), "Epsilon overshoot on height clamps to viewport height");

      // Negative zero normalizes to +0.0.
      const auto neg_zero = resolve_clip({{"clip", {{"x", -0.0}, {"y", -0.0}, {"width", 10.0}, {"height", 10.0}}}}, vp);
      require(neg_zero.ok && neg_zero.css.x == 0.0 && !std::signbit(neg_zero.css.x) && !std::signbit(neg_zero.css.y), "Negative zero origin normalizes cleanly");
    }

    // Both clip and candidateId specified at once -> invalid_clip.
    {
      const Json bounds{{"x", 200.0}, {"y", 400.0}, {"width", 40.0}, {"height", 20.0}};
      const auto both = resolve_clip(
          {{"clip", {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}}}, {"candidateId", "cand-1"}},
          vp, bounds);
      require(both.requested && !both.ok && both.error_code == "invalid_clip" && !both.error_message.empty(),
              "Providing both clip and candidateId fails with invalid_clip");
    }

    // Explicit clip rejections: malformed, negative, small, NaN/Inf, out-of-bounds, bad viewport.
    {
      for (const auto& bad_clip : {Json(nullptr), Json(42), Json("10,10"), Json::array({0, 0, 10, 10}), Json(true)}) {
        const auto res = resolve_clip({{"clip", bad_clip}}, vp);
        require(res.requested && !res.ok && res.error_code == "invalid_clip", "Non-object clip is rejected");
      }

      for (const auto* missing_key : {"x", "y", "width", "height"}) {
        Json c{{"x", 10.0}, {"y", 20.0}, {"width", 50.0}, {"height", 50.0}};
        c.erase(missing_key);
        const auto res = resolve_clip({{"clip", c}}, vp);
        require(res.requested && !res.ok && res.error_code == "invalid_clip", "Missing clip field is rejected");
      }

      for (const auto* key : {"x", "y", "width", "height"}) {
        for (const auto& bad_val : {Json("10"), Json(true), Json(nullptr), Json(std::nan("")),
                                    Json(std::numeric_limits<double>::infinity()),
                                    Json(-std::numeric_limits<double>::infinity())}) {
          Json c{{"x", 10.0}, {"y", 20.0}, {"width", 50.0}, {"height", 50.0}};
          c[key] = bad_val;
          const auto res = resolve_clip({{"clip", c}}, vp);
          require(res.requested && !res.ok && res.error_code == "invalid_clip", "Non-numeric or non-finite clip field is rejected");
        }
      }

      // Negative origin and sub-1px sizes.
      const auto neg_x = resolve_clip({{"clip", {{"x", -0.001}, {"y", 0.0}, {"width", 50.0}, {"height", 50.0}}}}, vp);
      require(!neg_x.ok && neg_x.error_code == "invalid_clip", "Negative clip x is rejected");
      const auto neg_y = resolve_clip({{"clip", {{"x", 0.0}, {"y", -1.0}, {"width", 50.0}, {"height", 50.0}}}}, vp);
      require(!neg_y.ok && neg_y.error_code == "invalid_clip", "Negative clip y is rejected");
      const auto small_w = resolve_clip({{"clip", {{"x", 0.0}, {"y", 0.0}, {"width", 0.999}, {"height", 50.0}}}}, vp);
      require(!small_w.ok && small_w.error_code == "invalid_clip", "Clip width < 1.0 is rejected");
      const auto small_h = resolve_clip({{"clip", {{"x", 0.0}, {"y", 0.0}, {"width", 50.0}, {"height", 0.0}}}}, vp);
      require(!small_h.ok && small_h.error_code == "invalid_clip", "Clip height < 1.0 is rejected");

      // Out-of-bounds beyond 1e-6 epsilon and overflow.
      const auto oob_x = resolve_clip({{"clip", {{"x", 17.0}, {"y", 0.0}, {"width", 900.0 + 2e-6}, {"height", 50.0}}}}, vp);
      require(!oob_x.ok && oob_x.error_code == "invalid_clip", "Clip exceeding viewport width beyond epsilon is rejected");
      const auto oob_y = resolve_clip({{"clip", {{"x", 0.0}, {"y", 600.0}, {"width", 50.0}, {"height", 75.0}}}}, vp);
      require(!oob_y.ok && oob_y.error_code == "invalid_clip", "Clip exceeding viewport height is rejected");
      const auto overflow = resolve_clip({{"clip", {{"x", 1e308}, {"y", 0.0}, {"width", 1e308}, {"height", 10.0}}}}, vp);
      require(!overflow.ok && overflow.error_code == "invalid_clip", "Overflowing x + width is rejected");

      // Unavailable or malformed visual viewport fails closed.
      const Json ok_clip{{"clip", {{"x", 10.0}, {"y", 10.0}, {"width", 20.0}, {"height", 20.0}}}};
      require(!resolve_clip(ok_clip, Json()).ok, "Null viewport fails closed");
      require(!resolve_clip(ok_clip, Json::object()).ok, "Empty viewport fails closed");
      require(!resolve_clip(ok_clip, visual_viewport(0.0, 600.0)).ok, "Zero-width viewport fails closed");
      require(!resolve_clip(ok_clip, visual_viewport(800.0, -10.0)).ok, "Negative-height viewport fails closed");
      require(!resolve_clip(ok_clip, {{"clientWidth", 800.0}, {"clientHeight", 600.0}, {"pageX", std::nan("")}}).ok, "Non-finite pageX fails closed");
    }

    // Candidate clipping: document->viewport transform, padding, edge clamping, pinch zoom, offscreen rejection.
    {
      // Interior candidate under scroll (pageX=120, pageY=340): document (220, 440, 80, 30) -> viewport (100, 100, 80, 30)
      // Expanded by default 24.0 CSS px on all 4 sides -> (76, 76, 128, 78).
      const Json interior{{"x", 220.0}, {"y", 440.0}, {"width", 80.0}, {"height", 30.0}};
      const auto res = resolve_clip({{"candidateId", "candidate-main-42"}}, vp, interior);
      require(res.requested && res.ok, "Interior candidate resolves");
      require(res.css.source == "candidate" && res.css.candidate_id == "candidate-main-42", "Candidate metadata fields are populated");
      require(approx(res.css.x, 76.0) && approx(res.css.y, 76.0) && approx(res.css.width, 128.0) && approx(res.css.height, 78.0),
              "Candidate bounds subtract pageX/pageY and expand by 24 CSS px on all sides");

      // Custom zero padding with width/height fallback viewport.
      const auto no_pad = resolve_clip({{"candidateId", "cand-zero"}}, vp_fallback, interior, 0.0);
      require(no_pad.ok && approx(no_pad.css.x, 100.0) && approx(no_pad.css.y, 100.0) &&
                  approx(no_pad.css.width, 80.0) && approx(no_pad.css.height, 30.0),
              "Zero padding preserves exact viewport-relative candidate bounds");

      // Top-left corner clamping: document (125, 345, 20, 10) -> viewport (5, 5, 20, 10) -> padded (-19, -19, 49, 39) -> clamped (0, 0, 49, 39).
      const Json top_left{{"x", 125.0}, {"y", 345.0}, {"width", 20.0}, {"height", 10.0}};
      const auto tl = resolve_clip({{"candidateId", "cand-tl"}}, vp, top_left);
      require(tl.ok && approx(tl.css.x, 0.0) && approx(tl.css.y, 0.0) && approx(tl.css.width, 49.0) && approx(tl.css.height, 39.0),
              "Candidate near top-left clamps to viewport origin without losing far padding");

      // Bottom-right corner clamping: viewport is 917 x 674 at page (120, 340).
      // Document (1027, 1004, 20, 20) -> viewport (907, 664, 20, 20) -> right=927, bottom=684 -> padded (883, 640) to (951, 708)
      // Clamped to [0, 917] x [0, 674] -> (883, 640, 34, 34).
      const Json bottom_right{{"x", 1027.0}, {"y", 1004.0}, {"width", 20.0}, {"height", 20.0}};
      const auto br = resolve_clip({{"candidateId", "cand-br"}}, vp, bottom_right);
      require(br.ok && approx(br.css.x, 883.0) && approx(br.css.y, 640.0) && approx(br.css.width, 34.0) && approx(br.css.height, 34.0),
              "Candidate straddling bottom-right edge clamps cleanly to viewport");

      // Oversized candidate clamps to the full viewport.
      const Json oversized{{"x", 0.0}, {"y", 0.0}, {"width", 3000.0}, {"height", 2000.0}};
      const auto full = resolve_clip({{"candidateId", "cand-huge"}}, vp, oversized);
      require(full.ok && approx(full.css.x, 0.0) && approx(full.css.y, 0.0) &&
                  approx(full.css.width, 917.0) && approx(full.css.height, 674.0),
              "Oversized candidate clamps to full visual viewport");

      // Pinched visual viewport (2x pinch matching input_viewport_tests.cpp).
      const auto pinched = visual_viewport(458.5, 337.0, 229.25, 168.5);
      const Json inside_pinch{{"x", 329.25}, {"y", 268.5}, {"width", 40.0}, {"height", 20.0}};
      const auto pinched_ok = resolve_clip({{"candidateId", "cand-pinch"}}, pinched, inside_pinch, 10.0);
      require(pinched_ok.ok && approx(pinched_ok.css.x, 90.0) && approx(pinched_ok.css.y, 90.0) &&
                  approx(pinched_ok.css.width, 60.0) && approx(pinched_ok.css.height, 40.0),
              "Candidate inside pinched visual viewport maps using pinched pageX/pageY");

      // Candidate inside layout viewport (917x674) but outside the pinched 2x view -> clip_outside_viewport.
      const Json outside_pinch{{"x", 750.0}, {"y", 550.0}, {"width", 40.0}, {"height", 20.0}};
      const auto pinched_off = resolve_clip({{"candidateId", "cand-pinch-off"}}, pinched, outside_pinch);
      require(pinched_off.requested && !pinched_off.ok && pinched_off.error_code == "clip_outside_viewport",
              "Candidate outside pinched visual viewport is rejected as clip_outside_viewport");

      // Offscreen in all 4 directions -> clip_outside_viewport.
      const Json off_left{{"x", 0.0}, {"y", 400.0}, {"width", 50.0}, {"height", 30.0}};
      const Json off_right{{"x", 1100.0}, {"y", 400.0}, {"width", 50.0}, {"height", 30.0}};
      const Json off_top{{"x", 200.0}, {"y", 100.0}, {"width", 50.0}, {"height", 30.0}};
      const Json off_bottom{{"x", 200.0}, {"y", 1100.0}, {"width", 50.0}, {"height", 30.0}};
      for (const auto& off : {off_left, off_right, off_top, off_bottom}) {
        const auto r = resolve_clip({{"candidateId", "cand-off"}}, vp, off);
        require(r.requested && !r.ok && r.error_code == "clip_outside_viewport", "Offscreen candidate is rejected");
      }

      // Clamped intersection smaller than 1.0 CSS px -> clip_outside_viewport.
      // pageX=120, candidate x=60, width=36.5 -> vx=-60, vx2=-23.5, expanded right=-23.5+24=0.5 < 1.0.
      const Json sliver{{"x", 60.0}, {"y", 400.0}, {"width", 36.5}, {"height", 30.0}};
      const auto sliver_res = resolve_clip({{"candidateId", "cand-sliver"}}, vp, sliver);
      require(sliver_res.requested && !sliver_res.ok && sliver_res.error_code == "clip_outside_viewport",
              "Sub-1px clamped candidate sliver is rejected as clip_outside_viewport");

      // Candidate ID length limits & validation.
      const std::string max_id(256, 'a');
      const auto max_ok = resolve_clip({{"candidateId", max_id}}, vp, interior);
      require(max_ok.ok && max_ok.css.candidate_id == max_id, "256-byte candidateId is accepted");

      const std::string too_long_id(257, 'a');
      require(!resolve_clip({{"candidateId", too_long_id}}, vp, interior).ok, "257-byte candidateId is rejected");
      require(resolve_clip({{"candidateId", too_long_id}}, vp, interior).error_code == "invalid_clip", "Overlong candidateId returns invalid_clip");
      require(!resolve_clip({{"candidateId", ""}}, vp, interior).ok, "Empty candidateId is rejected");
      for (const auto& bad_id : {Json(nullptr), Json(123), Json::object(), Json::array(), Json(false)}) {
        const auto r = resolve_clip({{"candidateId", bad_id}}, vp, interior);
        require(r.requested && !r.ok && r.error_code == "invalid_clip", "Non-string candidateId is rejected");
      }

      // Missing or malformed candidate_bounds and invalid padding.
      const auto missing_bounds = resolve_clip({{"candidateId", "cand-missing"}}, vp, std::nullopt);
      require(missing_bounds.requested && !missing_bounds.ok && !missing_bounds.error_code.empty(),
              "Missing candidate_bounds is rejected");
      const auto zero_bounds = resolve_clip({{"candidateId", "cand-zero-size"}}, vp, Json{{"x", 200.0}, {"y", 400.0}, {"width", 0.0}, {"height", 20.0}});
      require(!zero_bounds.ok && zero_bounds.error_code == "invalid_clip", "Zero-width candidate_bounds is rejected");
      const auto nan_bounds = resolve_clip({{"candidateId", "cand-nan"}}, vp, Json{{"x", std::nan("")}, {"y", 400.0}, {"width", 20.0}, {"height", 20.0}});
      require(!nan_bounds.ok && nan_bounds.error_code == "invalid_clip", "NaN candidate_bounds is rejected");
      const auto neg_pad = resolve_clip({{"candidateId", "cand-pad"}}, vp, interior, -1.0);
      require(!neg_pad.ok && neg_pad.error_code == "invalid_clip", "Negative padding is rejected");
      const auto nan_pad = resolve_clip({{"candidateId", "cand-pad"}}, vp, interior, std::nan(""));
      require(!nan_pad.ok && nan_pad.error_code == "invalid_clip", "NaN padding is rejected");
    }

    // DPI and pinch scaling to PixelRect, plus edge clamping and invalid-input guards.
    {
      // 100% DPI (1x).
      const auto p1 = to_pixel_rect(CssClip{10.0, 20.0, 100.0, 50.0, "clip", {}}, 1.0, 1.0, 1000, 800);
      require(p1.has_value() && p1->x == 10 && p1->y == 20 && p1->width == 100 && p1->height == 50,
              "1x scaling maps integer CSS clip directly to pixels");

      // 125% DPI with fractional CSS coordinates:
      // floor(10.5 * 1.25)=floor(13.125)=13, ceil(110.7 * 1.25)=ceil(138.375)=139 -> width 126
      // floor(20.3 * 1.25)=floor(25.375)=25, ceil(70.7 * 1.25)=ceil(88.375)=89 -> height 64
      const auto p125 = to_pixel_rect(CssClip{10.5, 20.3, 100.2, 50.4, "clip", {}}, 1.25, 1.25, 1250, 1000);
      require(p125.has_value() && p125->x == 13 && p125->y == 25 && p125->width == 126 && p125->height == 64,
              "125% DPI floors origin and ceils far edge");

      // 150% DPI * 2x pinch = 3.0x total scale, plus anisotropic scale check.
      const auto p_pinch = to_pixel_rect(CssClip{100.1, 50.2, 30.0, 20.0, "candidate", "c1"}, 3.0, 3.0, 1376, 1011);
      // floor(300.3)=300, ceil(390.3)=391 -> w=91; floor(150.6)=150, ceil(210.6)=211 -> h=61
      require(p_pinch.has_value() && p_pinch->x == 300 && p_pinch->y == 150 && p_pinch->width == 91 && p_pinch->height == 61,
              "3x DPI+pinch scale converts CSS clip accurately");

      const auto p_aniso = to_pixel_rect(CssClip{10.0, 10.0, 20.0, 20.0, "clip", {}}, 1.5, 2.0, 500, 500);
      require(p_aniso.has_value() && p_aniso->x == 15 && p_aniso->y == 20 && p_aniso->width == 30 && p_aniso->height == 40,
              "Independent scale_x and scale_y are honored");

      // Edge clamping against image bounds.
      const auto edge = to_pixel_rect(CssClip{299.5, 199.5, 5.0, 5.0, "clip", {}}, 3.0, 3.0, 900, 600);
      require(edge.has_value() && edge->x == 898 && edge->y == 598 && edge->width == 2 && edge->height == 2,
              "Far edges clamp to image_width and image_height");

      const auto beyond = to_pixel_rect(CssClip{500.0, 400.0, 50.0, 50.0, "clip", {}}, 2.0, 2.0, 800, 600);
      require(beyond.has_value() && beyond->x == 799 && beyond->y == 599 && beyond->width == 1 && beyond->height == 1,
              "Origin past image edge clamps to last pixel with 1x1 size");

      const auto one_by_one = to_pixel_rect(CssClip{0.0, 0.0, 10.0, 10.0, "clip", {}}, 2.0, 2.0, 1, 1);
      require(one_by_one.has_value() && one_by_one->x == 0 && one_by_one->y == 0 && one_by_one->width == 1 && one_by_one->height == 1,
              "1x1 image clamps cleanly without unsigned underflow");

      const auto subpixel = to_pixel_rect(CssClip{10.0, 20.0, 1e-12, 1e-12, "clip", {}}, 1.0, 1.0, 100, 100);
      require(subpixel.has_value() && subpixel->x == 10 && subpixel->y == 20 && subpixel->width == 1 && subpixel->height == 1,
              "Subpixel clip guarantees at least 1x1 pixel rect");

      // Invalid scales, image sizes, and CSS clips return std::nullopt.
      const CssClip good{10.0, 10.0, 20.0, 20.0, "clip", {}};
      require(!to_pixel_rect(good, 0.0, 1.0, 100, 100).has_value(), "Zero scale_x returns nullopt");
      require(!to_pixel_rect(good, 1.0, -1.0, 100, 100).has_value(), "Negative scale_y returns nullopt");
      require(!to_pixel_rect(good, std::nan(""), 1.0, 100, 100).has_value(), "NaN scale_x returns nullopt");
      require(!to_pixel_rect(good, 1.0, std::numeric_limits<double>::infinity(), 100, 100).has_value(), "Inf scale_y returns nullopt");
      require(!to_pixel_rect(good, 1.0, 1.0, 0, 100).has_value(), "Zero image_width returns nullopt");
      require(!to_pixel_rect(good, 1.0, 1.0, 100, 0).has_value(), "Zero image_height returns nullopt");
      require(!to_pixel_rect(CssClip{-1.0, 0.0, 10.0, 10.0, "clip", {}}, 1.0, 1.0, 100, 100).has_value(), "Negative css.x returns nullopt");
      require(!to_pixel_rect(CssClip{0.0, 0.0, 0.0, 10.0, "clip", {}}, 1.0, 1.0, 100, 100).has_value(), "Zero css.width returns nullopt");
      require(!to_pixel_rect(CssClip{0.0, 0.0, 10.0, std::nan(""), "clip", {}}, 1.0, 1.0, 100, 100).has_value(), "NaN css.height returns nullopt");
      require(!to_pixel_rect(CssClip{1e308, 0.0, 10.0, 10.0, "clip", {}}, 2.0, 1.0, 100, 100).has_value(), "Overflowing pixel projection returns nullopt");
    }

    // Metadata generation and pixel-to-CSS coordinate mapping accuracy.
    {
      const CssClip css_clip{10.5, 20.3, 100.2, 50.4, "clip", {}};
      const auto px = to_pixel_rect(css_clip, 1.25, 1.25, 1250, 1000);
      require(px.has_value(), "Expected valid pixel rect for metadata check");
      const auto meta = clip_metadata(css_clip, *px, 1.25, 1.25);

      require(approx(meta.at("x").get<double>(), 13.0 / 1.25), "clip_metadata x matches exact pixel origin in viewport CSS");
      require(approx(meta.at("y").get<double>(), 25.0 / 1.25), "clip_metadata y matches exact pixel origin in viewport CSS");
      require(approx(meta.at("width").get<double>(), 126.0 / 1.25), "clip_metadata width matches exact cropped CSS width");
      require(approx(meta.at("height").get<double>(), 64.0 / 1.25), "clip_metadata height matches exact cropped CSS height");
      require(meta.at("source") == "clip", "clip_metadata preserves source");
      require(!meta.contains("candidateId"), "clip_metadata omits candidateId for explicit clips");
      require(meta.at("coordinateSpace") == "viewport-css", "clip_metadata declares viewport-css coordinate space");
      require(meta.at("coordinateMapping") == "CSS x = clip.x + image x / scaleX; CSS y = clip.y + image y / scaleY",
              "clip_metadata provides exact pixel-to-CSS mapping formula");
      require(meta.at("imageOriginX").get<uint32_t>() == 13u && meta.at("imageOriginY").get<uint32_t>() == 25u,
              "clip_metadata reports exact imageOriginX and imageOriginY");

      // Verify round-trip coordinate formula: clip.x + image_x / scaleX == (px.x + image_x) / scaleX.
      const double sample_img_x = 47.0;
      const double sample_img_y = 19.0;
      const double mapped_x = meta.at("x").get<double>() + sample_img_x / 1.25;
      const double mapped_y = meta.at("y").get<double>() + sample_img_y / 1.25;
      require(approx(mapped_x, (static_cast<double>(px->x) + sample_img_x) / 1.25),
              "Cropped image X maps identically to full-screenshot viewport CSS X");
      require(approx(mapped_y, (static_cast<double>(px->y) + sample_img_y) / 1.25),
              "Cropped image Y maps identically to full-screenshot viewport CSS Y");

      // Candidate clip metadata includes candidateId.
      const CssClip cand_css{76.0, 76.0, 128.0, 78.0, "candidate", "candidate-main-42"};
      const auto cand_px = to_pixel_rect(cand_css, 2.0, 2.0, 1834, 1348);
      require(cand_px.has_value(), "Expected valid candidate pixel rect");
      const auto cand_meta = clip_metadata(cand_css, *cand_px, 2.0, 2.0);
      require(cand_meta.at("source") == "candidate" && cand_meta.at("candidateId") == "candidate-main-42",
              "Candidate clip_metadata includes source=candidate and candidateId");
      require(cand_meta.at("imageOriginX").get<uint32_t>() == 152u && cand_meta.at("imageOriginY").get<uint32_t>() == 152u,
              "Candidate clip_metadata reports scaled image origin");
      require(approx(cand_meta.at("x").get<double>(), 76.0) && approx(cand_meta.at("width").get<double>(), 128.0),
              "Integer-aligned scaled candidate metadata matches CSS clip");
    }

    std::cout << "Screenshot clip passed: explicit clips, validation, candidate padding/clamping, DPI/pinch pixel rects and metadata\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
