# Xenon Browser 0.1.4

This release improves the visibility filter for controls inside simple rounded clips and for pages containing distant text shadows. Control geometry now applies clipping to the same center patch used for occlusion; rendered text and frame embeddings still require their complete boxes. Unsupported paint and native form/SVG glyph sources absent from snapshots remain conservative. Uncertain-click collection includes additional allowlisted boolean diagnostics without recording website strings, input values or images.

The bookmark and saved-password button surfaces now fit within the rounded address bar. Developer-mode element selection uses the browser's theme colors. Clicking an already selected tab hides the extra keyboard focus ring, while keyboard navigation restores focus cues.

The Windows x64 ZIP and setup executable are unsigned and include SHA-256 sidecars. Dependency pins remain unchanged. The CEF Windows x64 index was checked for this release and still lists the pinned CEF 154.0.34 / Chromium 154.0.8037.98 as its highest stable build. Chrome's [155.0.8059.39/.40 security update](https://chromereleases.googleblog.com/2026/10/stable-channel-update-for-desktop_086471744.html) includes fixes beyond this runtime, so this release retains the upstream CEF security-update lag and does not claim parity with current Chrome.

Validation uses disposable profiles, synthetic pages, native messages and PrintWindow captures. Physical-device input, high contrast, other DPI, assistive technology and real-account acceptance remain manual coverage. The full developer divider/hover suite previously could not acquire Windows foreground activation; focused selection checks cover the changed UI behavior.

The [release validation record](test-results/release-0.1.4-validation.json) binds the rebuilt binaries to all 15 native groups, 111 adapter/fixed-script checks, 32 downloader/pointer checks, 47 isolated installer checks and five live suites. Package verification and extracted-archive smoke checks run after assembly and are recorded separately; this source record does not claim publication.
