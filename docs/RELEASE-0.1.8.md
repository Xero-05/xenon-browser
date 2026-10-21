# Xenon Browser 0.1.8

0.1.8 includes all changes merged since 0.1.7: Chromium extension management and action surfaces, more reliable uploads and screenshots, correct coordinates under zoom, and background popup containment.

- **Extensions.** Manage extensions in Chromium's actual manager and install directly from the Chrome Web Store with native permission confirmation. The compact Extensions panel provides site access, pinning and individual menus. Pinned actions open real Chromium popups; supported extensions can open docked side panels. Installation completion no longer crashes the browser. Management tabs remain human-only. Broader Chrome window/tab API compatibility remains limited.
- **Uploads.** Agents can activate verified text entries such as **Upload from Device**, including entries that open hidden file inputs. `xenon_files` can resolve an exact approved `relativePath` without depending on recursive enumeration limits. Existing file grants and identity checks still apply.
- **Screenshots.** Transient frame verification failures receive a bounded full rescan, while unverifiable content still prevents capture. Screenshot scales correctly map image pixels to CSS pixels beside scrollbars and at browser zoom. Pinching or panning invalidates old coordinates; bounds use the magnified visible area.
- **Pinch zoom and human activity.** Rendered evidence, element actions, inspection, pointer movement and scrolling use the visual viewport. The visible agent pointer and Elements inspector convert through the pinch scale and pan. Touch, pen and precision-touchpad gestures now pause agent input and invalidate old evidence.
- **Background popups.** Covered tabs cannot surface native dropdown/suggestion windows or capture the pointer. Foreground dropdowns can reopen after a tab switch; minimization and loss of foreground dismiss their native popups.

The Windows x64 setup executable and portable ZIP are unsigned, with SHA-256 sidecars. Close Xenon and reconnect the MCP adapter after upgrading. Profiles and pairing configuration remain separate from application files.

Packages retain Windows H.264/AAC decoding and covered-tab frame pacing from 0.1.7. The pinned Windows-decoder engine adds the extension-action patch; it contains no bundled H.264/AAC decoder or OpenH264. H.264 requires supported Windows GPU decoding, AAC requires Windows Media Foundation, and Widevine DRM remains unsupported. Hosted CI uses the stock engine; the published files are built locally from this release source with the audited Windows-decoder distribution.

CEF 154.0.34 / Chromium 154.0.8037.98 remains the highest stable Windows x64 entry in the [CEF build index](https://cef-builds.spotifycdn.com/index.json), checked October 20, 2026. [Chrome's Windows stable release](https://chromereleases.googleblog.com/search/label/Stable%20updates) is 156.0.8078.47/.48. This release retains the CEF security-update lag and does not claim parity with current Chrome. Dependency revisions are unchanged from the merged source.

Release validation is recorded in [TESTING](TESTING.md). Synthetic fixtures and native test drivers do not establish physical touchscreen, pen or precision-touchpad coverage, real-account acceptance, arbitrary extension compatibility, or every GPU/display-scale combination. Unsigned hashes establish byte integrity, not publisher identity.
