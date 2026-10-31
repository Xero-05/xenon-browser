# Xenon Browser 0.1.11

0.1.11 includes every change merged since 0.1.10, through pull request #65.

- **Lower-page references on pages with propagated overflow.** Pages that set overflow on the root or `body` (for example `html,body{height:100%}` with `body{overflow-x:hidden}`) previously returned verified references only on the first screen; after scrolling, observations were empty with `unprovenGeometry` omissions. Xenon now identifies the viewport-defining element as Chromium does and skips only that element's box clip. The viewport bound, ordinary container clips, occlusion, opacity, filter, transform and text checks are unchanged. The exemption fails closed for `contain`, size `container-type`, non-visible `content-visibility`, scaling or reflecting transforms and incomplete styles. No MCP schema or contract change. Synthetic root and body fixtures reproduce the reported signature; the original application form's DOM was not inspected, so real-site acceptance is not claimed.

The unsigned Windows x64 installer and portable ZIP were built locally from this release source. Close Xenon and reconnect the MCP adapter before upgrading. Profiles and pairing configuration remain separate from application files.

The release retains the exact audited Windows-decoder engine used by 0.1.9 and 0.1.10: CEF 154.0.34 / Chromium 154.0.8037.98. It is reused without rebuilding or re-auditing its source tree. Newer stable CEF/Chromium releases exist; this release retains that security-update gap and does not claim current Chrome security parity.

H.264 requires supported Windows GPU decoding, AAC requires Windows Media Foundation, and Widevine DRM remains unsupported. There is no bundled H.264/AAC software decoder or OpenH264. Hosted CI uses the stock engine; the local release uses the Windows-decoder distribution.

Validation was scoped to the changed visibility code and to packaging; see [TESTING](TESTING.md#0111-release-validation) and the [packaging record](test-results/release-0.1.11-packaging.json). Synthetic tests do not establish real-account acceptance, arbitrary extension compatibility, physical input or every display configuration. Unsigned hashes establish byte integrity, not publisher identity.
