#pragma once

#include "xenon/contracts.hpp"

namespace xenon {
// The ordering is part of the snapshot contract. Request these styles plus
// includeDOMRects, includePaintOrder, includeBlendedBackgroundColors and
// includeTextColorOpacities. No page script or accessibility strings are used.
Json visible_snapshot_styles();

// viewports maps frame IDs to {x,y,width,height}, in frame-document CSS pixels.
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
Json visible_snapshot(const Json& snapshot, const Json& viewports);
}
