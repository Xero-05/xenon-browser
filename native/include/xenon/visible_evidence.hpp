#pragma once

#include "xenon/contracts.hpp"

namespace xenon {
// The ordering is part of the snapshot contract. Request these styles plus
// includeDOMRects, includePaintOrder, includeBlendedBackgroundColors and
// includeTextColorOpacities. No page script or accessibility strings are used.
Json visible_snapshot_styles();

// Converts a Page.getLayoutMetrics result into the session root's viewport
// entry for visible_snapshot: {x,y,width,height,scale}. The rectangle is the
// rendered part of the layout viewport, which a pinch zoom shrinks to the
// visual viewport. Returns an empty object when the CSS and layout-unit
// metrics are missing or inconsistent.
Json visible_snapshot_viewport(const Json& layout_metrics);

// viewports maps frame IDs to {x,y,width,height,scale?}. The rectangle uses the
// snapshot's layout units; scale is layout units per CSS pixel (display scale
// times browser zoom, 1 when omitted) and must match across entries. Snapshot
// geometry is converted with it, so bounds and viewports are returned in
// frame-document CSS pixels, the units of computed styles and coordinate input.
// Supply at least each CDP session's root viewport. Same-process child viewports
// are derived only from a proven, untransformed iframe's content rectangle.
// Returns {frames:{frameId:{nodes:{backendId:{bounds,tag,inputType?,name,
// textVisible,geometryEvidence}},viewport,parentFrameId?,ownerBackendNodeId?}},omitted,partial}.
// Names contain verified rendered text only. Missing/uncertain evidence is
// omitted, never replaced with aria labels, DOM textContent or other metadata.
// Ordinary element presence proves an unoccluded 2x2 CSS-pixel center patch;
// names independently require full text-box proof, and frame embeddings require
// their entire border box to be visible. Bounds alone do not assert every pixel
// of an ordinary element is unobscured.
// Human diagnostics optionally include at most eight short, untrusted computed
// transform and paint-effect examples. Agent observations and action revalidation leave this off.
// Optional uncertainNodes contain structural candidates and independently
// verified text only, never raw names/values or action references. Guarded
// action revalidation must leave this disabled.
Json visible_snapshot(const Json& snapshot, const Json& viewports, bool human_diagnostics=false, bool include_uncertain=false);
}
