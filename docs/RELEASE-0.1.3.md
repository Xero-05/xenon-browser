# Xenon Browser 0.1.3

This release adds opt-in native developer tools for inspecting agent traffic and the rendered controls on a page. The elements panel supports selection, page picking, hover matching, resizeable panes and approximate highlighting for controls whose visibility cannot be verified. Unsupported paint and transform diagnostics help explain omitted evidence. Optional uncertain-click collection records bounded, redacted diagnostics and defaults to off.

Agent observations distinguish verified controls from uncertain viewport candidates. Uncertain candidates do not carry actionable element references; coordinate input still requires fresh screenshot evidence and native authorization. Native inspector labels derived from accessibility metadata are identified as unverified and are not promoted into MCP evidence. Credential protection, ownership and human-input pauses continue to apply.

The release also includes broker, adapter and native UI regression coverage for developer tools, evidence filtering and diagnostic collection. Dependency versions remain pinned to the existing lockfile. The Windows x64 ZIP and setup executable are unsigned and include SHA-256 sidecars.

The [CEF index](https://cef-builds.spotifycdn.com/index.json), checked October 12, still lists the pinned CEF 154.0.34 / Chromium 154.0.8037.98 as its newest stable Windows x64 build. Chrome has since released [155.0.8059.39/.40 with additional security fixes](https://chromereleases.googleblog.com/2026/10/stable-channel-update-for-desktop_086471744.html). This package therefore retains the upstream CEF security-update lag and does not claim parity with current Chrome.

See the [validation record](test-results/release-0.1.3-validation.json) for the tests and exact binaries. Native message drivers and synthesized Windows input do not establish physical-device, high-contrast, multi-monitor/DPI, assistive-technology or sustained-load acceptance. Authentication tests use the separate, unshipped fixture target and public synthetic credentials.
