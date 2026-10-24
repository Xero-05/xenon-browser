# Xenon Browser 0.1.9

0.1.9 includes every change merged since 0.1.8, through pull request #60.

- **Minimized and background tabs.** Minimizing a window preserves its last usable page dimensions, including for new tabs created while minimized. Agents can continue observing controls and taking verified screenshots. Regression checks cover background dropdown keyboard selection, foreground screenshot-bound selection, restoration and resize. A focused select's outline can still obscure its rendered label; fresh screenshot-bound input remains available.
- **Earlier feedback for invalid input.** The MCP adapter rejects missing references and values for fill/select/check actions, empty select values and malformed key strings before dispatch. Native ownership, stale-reference and input protections still apply.
- **Finished work stays marked.** Agents can persist an attention mark after they finish, disconnect or release a tab, until the person dismisses it or control changes. Only marked tabs remain highlighted.
- **Keep agent tabs open.** Controls has an **Agents can close tabs in this workspace** setting. New human-created workspaces start with it off; existing and automatic agent workspaces retain their previous close behavior.
- **Sidebar polish.** Hovered tabs no longer flash their icon under New tab.
- **MCP SDK 2.3.1.** The adapter and test clients use the updated SDK. Its HTTP transport/authentication advisories did not expose Xenon's stdio-only adapter.

The Windows x64 setup executable and portable ZIP are unsigned, with SHA-256 sidecars. Close Xenon and reconnect the MCP adapter after upgrading. Profiles and pairing configuration remain separate from application files.

Packages retain the audited Windows-decoder engine and its extension-action and covered-tab pacing patches from 0.1.8. There is no bundled H.264/AAC decoder or OpenH264: H.264 requires supported Windows GPU decoding, AAC requires Windows Media Foundation, and Widevine DRM remains unsupported. Hosted CI uses the stock engine; the published packages are built locally from this release source with the pinned Windows-decoder distribution.

The pinned engine remains CEF 154.0.34 / Chromium 154.0.8037.98. On October 24, 2026, the [CEF build index](https://cef-builds.spotifycdn.com/index.json) lists CEF 156.0.8 / Chromium 156.0.8078.48 as stable, and [Chrome's Windows stable version history](https://versionhistory.googleapis.com/v1/chrome/platforms/win/channels/stable/versions?pageSize=3) lists 157.0.8103.15. This patch release retains the merged engine pins and their security-update gap; it does not claim current Chrome security parity. Moving to the newer engine requires rebuilding and validating the Windows media and extension patches.

Release checks and their limits are recorded in [TESTING](TESTING.md). Synthetic fixtures do not establish physical input, real-account acceptance, arbitrary extension compatibility or every GPU/display-scale combination. Unsigned hashes establish byte integrity, not publisher identity.
