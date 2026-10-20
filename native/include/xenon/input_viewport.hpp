#pragma once
#include "xenon/contracts.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
namespace xenon {
// Screenshot coordinates address the visual viewport. CDP input multiplies CSS
// coordinates by browser zoom and pinch scale (Chromium InputHandler::
// ScaleFactor), and the unclipped capture shows that same view. A person's
// touchscreen or precision-touchpad pinch, which Chromium enables by default,
// shrinks the visual viewport inside the unchanged layout viewport. Panning
// while pinched moves it without scrolling the layout viewport.
//
// The identity keeps both viewports from Page.getLayoutMetrics, so any scroll,
// resize, zoom, pinch or pan changes it. Empty when the metrics are unusable.
inline Json input_viewport(const Json& metrics) {
  const auto layout = metrics.value("cssLayoutViewport", Json::object()), visual = metrics.value("cssVisualViewport", Json::object());
  Json result{{"layout", Json::object()}, {"visual", Json::object()}};
  for (const auto* key : {"pageX", "pageY", "clientWidth", "clientHeight"}) {
    if (!layout.contains(key) || !layout.at(key).is_number() || !std::isfinite(layout.at(key).get<double>())) return Json::object();
    result["layout"][key] = layout.at(key);
  }
  for (const auto* key : {"offsetX", "offsetY", "pageX", "pageY", "clientWidth", "clientHeight", "scale", "zoom"}) {
    if (!visual.contains(key) || !visual.at(key).is_number() || !std::isfinite(visual.at(key).get<double>())) return Json::object();
    result["visual"][key] = visual.at(key);
  }
  const auto& v = result["visual"];
  if (v["clientWidth"].get<double>() <= 0 || v["clientHeight"].get<double>() <= 0 || v["scale"].get<double>() <= 0) return Json::object();
  return result;
}
// The visible page area: the visual viewport without the root scrollbars.
// Chromium 154 keeps the main frame's vertical scrollbar on the right for
// right-to-left documents too (LayoutView::ShouldPlaceBlockDirectionScrollbar-
// OnLogicalLeft is false unless a web-test-only setting is on), so the area
// starts at x = 0.
inline bool inside_input_viewport(const Json& viewport, double x, double y) {
  if (!viewport.is_object() || !viewport.contains("visual")) return false;
  const auto& visual = viewport.at("visual");
  return std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 &&
         x < visual.value("clientWidth", 0.0) && y < visual.value("clientHeight", 0.0);
}
// Pointer moves stay inside the visual viewport, just short of its far edges.
// The last pointer position can predate a pinch and lie outside the view.
inline std::pair<double, double> input_viewport_clamp(const Json& viewport, double x, double y) {
  const auto visual = viewport.is_object() ? viewport.value("visual", Json::object()) : Json::object();
  const double width = std::max(0.0, visual.value("clientWidth", 0.0) - .01), height = std::max(0.0, visual.value("clientHeight", 0.0) - .01);
  return {std::clamp(std::isfinite(x) ? x : 0.0, 0.0, width), std::clamp(std::isfinite(y) ? y : 0.0, 0.0, height)};
}
// Window coordinates are the page window's client pixels divided by display
// scale and browser zoom: a person's pointer, the inspector's hover and pick
// points and the agent pointer overlay use them. Both they and input
// coordinates start at the window's top-left page corner, where the visual
// viewport is drawn, so only the pinch scale separates them; no pan offset.
inline double input_viewport_scale(const Json& viewport) {
  const auto visual = viewport.is_object() ? viewport.value("visual", Json::object()) : Json::object();
  const auto scale = visual.value("scale", Json());
  return scale.is_number() && std::isfinite(scale.get<double>()) && scale.get<double>() > 0 ? scale.get<double>() : 1.0;
}
inline std::pair<double, double> input_viewport_from_window(const Json& viewport, double x, double y) {
  const double scale = input_viewport_scale(viewport);
  return {x / scale, y / scale};
}
// DOM.getNodeForLocation takes document coordinates: the frame point plus the
// layout viewport's scroll offset. The visual viewport's pageX/pageY are that
// offset plus the pinch pan offset.
inline std::pair<double, double> input_viewport_document_point(const Json& viewport, double x, double y) {
  const auto visual = viewport.is_object() ? viewport.value("visual", Json::object()) : Json::object();
  return {x + visual.value("pageX", 0.0), y + visual.value("pageY", 0.0)};
}
}
