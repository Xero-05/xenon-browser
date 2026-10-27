# Validation

## 0.1.10 release validation

The isolated 0.1.10 source starts at merged PR #63 (`d6f7971`), with only release version and documentation changes. It reuses the complete audited Windows-decoder distribution from 0.1.9 after checking its recipe, provenance and listed file hashes. The engine source was not rebuilt or re-audited. Hosted CI's stock-engine results remain separate from this local build.

The [release validation record](test-results/release-0.1.10-validation.json) binds the current local results to the application and engine hashes. Raw fixture profiles, screenshots, call histories and logs remain local. Earlier candidate and failed-run evidence below retains its original identity and version.

All 24 native groups, 172 npm checks, five branding checks, 16 production-browser suites and 51 isolated installer checks passed. The ZIP passed all 1,360 runtime-file hashes and six extracted-package smoke checks. The [packaging record](test-results/release-0.1.10-packaging.json) binds the four unsigned assets to these results. A first viewport run was interrupted by `HUMAN_INPUT_PAUSED`; the new disposable full-suite run passed without changing the guard. Both attempts remain recorded. The initial restricted-runner codec audit failed on temporary-directory permissions; all ten checks passed under ordinary Windows permissions. This does not replace the prior failed output.

## Rendered upload text excluded from accessibility (local candidate, 2026-10-26)

A synthetic `UPLOAD RESUME` text entry inside an `aria-hidden` wrapper reproduced an empty observation despite a plainly visible screenshot, valid rendered geometry and no visibility omissions. Removing only `aria-hidden` restored the target. Observation had required an unignored AX node even when the rendered filter independently verified the DOM Text node. This reproduces the reported upload-reference symptom; the original Oracle application DOM and incident binary remain unverified.

Observation now admits those verified Text nodes with separate native text-type provenance. Their refs revalidate node type, rendered label/geometry, frame identity and guarded pointer targeting before effects. The rendered filter, opaque file grants, one-activation chooser interception and exact chooser/document checks remain enforced. No coordinate upload, hidden-input search or OS-picker interface was added.

An isolated PR checkout based on `445b3cc` was rebuilt as 0.1.9 against the pinned stock release engine with `-RequireReleaseEngine`. The complete build passed 24 native suites, 145 adapter/fixed-script checks and five branding checks; typechecking and `git diff --check` passed. Serial production-browser validation passed seven new upload-observation cases, all 17 existing upload cases, 16 adversarial visibility cases, ten screenshot/authentication cases and 17 input/ownership cases. The [binary-bound record](test-results/oracle-upload-observation.json) separates the earlier working-tree reproduction from this PR's build and 67 browser checks.

Run `node tests/upload-entry-integration.mjs --oracle-observation` for the focused regression. It covers full and targeted reads, normal/AX-excluded text and a child-frame upload, hidden/covered text withholding, disabled ancestors and stale labels. Uploads transfer only the synthetic approved contents after one trusted activation. Explicit transient read invalidations may be retried while frames settle; mutations are never replayed. Windows CI runs this case against the packaged DLLs alongside its existing serial suites.

No personal profile, applicant screenshot, live application or installed browser was changed. The original portal cause, physical input and real Oracle acceptance are not claimed. Raw fixture profiles/calls and local logs remain excluded; published evidence contains sanitized synthetic results only.

## Reload confirmations and long dropdowns (local native candidate, 2026-10-25)

`node tests/native-page-controls-integration.mjs` reproduces both failures on generated loopback pages in a disposable production-browser profile. Before the fix, normal reload worked, but an armed unsaved-changes warning left reload waiting without an answerable native confirmation. A long native select menu also extended to screen x=2920 beyond a work area ending at x=2560. A native Win32 regression separately failed when a popup was positioned beyond the left edge.

The rebuilt browser passed all four cases: ordinary reload; cancellation of an unsaved-changes reload followed by one accepted F5 reload; long LTR, RTL and oversized dropdown bounds with website-confirmed selection changes; and human-pause/ownership protection of reload confirmations. An agent cannot answer a human-triggered confirmation, ownership and generation stay unchanged, and a later agent-triggered warning remains available through the existing `confirm`/`xenon_dialog` path. `--case=dropdown`, `--case=ownership` and `--case=reload` select matching cases. The external [fixture helper](../tests/fixtures/native-page-controls.ps1) validates its exact generated profile and process, targets the frontmost page host, reads only the synthetic native dialog message, and measures/selects each popup within one foreground helper call. It does not add a production native-window endpoint or test-driver bypass.

`PagePopupGuard` fits eligible transient windows to the source monitor's native work area during placement, preserving dimensions when they fit and limiting oversized menus. Native tests cover left, right and bottom overflow, oversized bounds, move-only and show-only updates, as well as the existing suppression/dismissal lifecycle. CEF's separate before-unload callback now uses the nonmodal dialog record and clears retained state on closure/reset; accepting continues the pending navigation and dismissing cancels it.

PR revalidation on an isolated checkout of `5ec8d3b` (0.1.9) used the pinned stock release engine with `-RequireReleaseEngine`. The complete build passed all 24 native suites, 145 adapter/fixed-script tests and five branding checks; TypeScript and `git diff --check` also passed. The new four-case suite and background popup containment passed serially on that exact production build. The [sanitized binary-bound record](test-results/native-page-controls.json) separates those PR results from the earlier custom-engine candidate, whose workflow/browser runs and failed intermediate reports are retained. The large CEF integration source uses `/bigobj` in both browser targets. No dependency pins or release version were changed.

Coverage uses external Win32 messages and supported MCP calls, not physical input or real portal pages. Live dropdown checks used one monitor at a fixed DPI; physical mixed-DPI and multi-monitor acceptance remain unvalidated. Foreground popup cases require an uninterrupted interactive desktop. No personal profile was used, and no historical incident report or failed evidence was relabeled.

## 0.1.9 release candidate

The local 0.1.9 Windows-decoder build passed all 24 native test groups, 172 checks in the full npm suite, typechecking, five bootstrap branding checks and a dependency audit with zero findings after updating Zod to 4.6.7. The MIT license is unchanged. Three codec dependency regressions, 17 engine audit regressions, 25 recipe checks and all 51 isolated installer checks passed.

The ZIP passed all 1,355 runtime hashes, and its extracted bytes passed all six package smoke checks. All 14 serial production suites passed on the packaged DLL: integration, batch, visibility, worker lifecycle, upload entry, persistence, theme, passkey, screenshot, background popups, Windows H.264/AAC, WebM, covered-tab pacing and the full six-case viewport/dropdown workflow. [Binary-bound validation](test-results/release-0.1.9-validation.json) records the source, artifacts and individual checks. Historical failures from earlier workflow investigations remain in their original records.

The complete reused Windows-decoder distribution matches the merged recipe and the 0.1.8 engine hash. The modified local CEF source tree was not rebuilt or re-audited. This patch release retains CEF 154.0.34 / Chromium 154.0.8037.98 although CEF 156 is now stable; see the [release notes](RELEASE-0.1.9.md) for the checked versions and security-update gap. Hosted CI uses stock CEF and its artifacts are not the published packages. Physical input, real accounts, arbitrary extensions, foreground extension surfaces, high contrast and other display scales remain unverified. A focused select outline can still obscure its rendered label; the visibility guard remains conservative.

## Workflow viewport and interaction validation (2026-10-24)

Minimizing a shell window must preserve the last nonempty page dimensions for existing tabs and tabs created while minimized. The synthetic `tests/workflow-viewport-integration.mjs` fixture covers viewport/control/screenshot availability, background dropdown keyboard selection, foreground screenshot-bound dropdown input, restoration, resize, covered-tab creation and settled page popups. CI runs its isolated `--case=minimize` and `--case=background` cases; the full run also requires foreground activation on an interactive desktop.

The adapter rejects missing refs for `fill`/`select`/`check`, missing action values, empty selections and malformed key strings before native dispatch. `adapter/test/interact.test.ts` exercises real legacy and modern SDK stdio clients with a counting synthetic broker, including valid forwarding and preservation of native stale-ref outcomes. These checks do not replace native authorization or safe-input enforcement.

Current main already contained source-browser dismissal for suppressed native dropdowns; this change preserves it and adds keyboard regression coverage. A focused select can still lose its rendered label when its outline overlaps the whitespace-inclusive label rectangle, although the control retains a ref. Fresh screenshot-bound input is supported; visibility filtering is unchanged. The original portal, physical input, complete foreground datalist behavior, full-screen minimization, mixed DPI and multiple monitors are outside this fixture's coverage.

The full PR-branch build passed 24 native groups, 48 adapter tests, 97 fixed-script tests and five branding checks; typechecking passed. All six workflow cases and the popup-containment, 11 visibility, 17 input/ownership and nine screenshot-protection checks passed on the same DLL. A first workflow run withheld a post-restore image whose measured viewport and surface differed; that failure is retained. The fixture now permits bounded fresh read-only captures for that specific transient mismatch, and two such recaptures recovered in the passing run without changing the safety guard. [Binary-bound results](test-results/workflow-pr-validation.json) record both runs. Earlier results from the original dirty checkout are separate preparation evidence.

## 0.1.8 release

0.1.8 was published as the latest regular release on October 20, 2026. The
[publication record](test-results/release-0.1.8-publication.json) binds the four
asset sizes and GitHub SHA-256 digests to the source commit. A fresh public ZIP
download passed all 1,351 file hashes again. The release source Windows CI
passed; its stock-engine artifacts were not published.

The local Windows-decoder package passed six extracted-package smoke checks
and all 13 production live suites: integration, batch, visibility, worker
lifecycle, upload entry, persistence, theme, passkey, screenshot, background
popup containment, Windows H.264/AAC, WebM and covered-tab pacing. The
[validation record](test-results/release-0.1.8-validation.json) binds every
production report to the packaged DLL. The native-driver extension manager
and quick-list suite passed 14 checks. The separate foreground extension
popup/side-panel attempt timed out because Windows refused activation and no
operator was available; it remains unverified for this release. No full Store
installation or physical touch/pen/touchpad acceptance is claimed.

The release source includes every commit merged through `b2c810d`. Its local
Windows-decoder build passed all 24 native test groups, 146 adapter/fixed-script
checks, typechecking and five bootstrap branding checks. The codec audit (10),
engine audit (17) and recipe migration (25) checks passed. All 51 isolated
installer checks and all 13 synthetic pinch-zoom checks passed. The pinch suite
uses the unshipped `XenonAuthTest` driver, including native-message inspector and
pointer checks; it does not exercise physical touch or touchpad input.

The engine matches the pinned Windows-decoder recipe with the extension-action
patch. CEF and Chromium revisions remain unchanged. Hosted CI uses the stock
engine; its artifacts are not the locally built release assets. Package,
production live-suite and publication evidence is recorded separately after
packaging, bound to the actual uploaded binaries. Earlier feature-specific
records below retain their original scope and failures.

## Pointer overlay and inspector under pinch zoom (local candidate, 2026-10-20)

The [validation record](test-results/pinch-pointer-overlay.json) covers the
remaining window-pixel paths under pinch zoom. A person's pointer is kept in
window coordinates (client pixels over display scale and browser zoom), but
agent input is in visual-viewport CSS pixels, which a pinch divides by its
scale. On the unfixed `XenonAuthTest` DLL with a 2x synthetic pinch, the
next agent movement after a person's hover began at twice the person's
position, and the agent pointer overlay was drawn at half the agent's
distance from the page corner. Elements inspector page picking matched against the unpinched
layout viewport and left another row selected, and the highlight outline was
drawn at half size and position. The 9 earlier pinch checks passed and the 4
new ones failed.

The last pointer stays in window coordinates, and each agent gesture converts
through the pinch scale from its own layout-metrics read. Inspector hover and
picking convert through the current visual viewport, and highlights carry
their `pageScale`. The fixed DLL passed all 13 pinch checks. The rebuilt
production DLL passed 16 visibility and 17 integration checks, and the full
build passed 24 native groups and the script suites. The person's hover was
recorded by the test-only driver without input, and the inspector was driven
with posted messages. Physical pointer, touch and touchpad input, pinch with
display scaling or browser zoom, a real-cursor inspector hover and the full
developer-elements suite (which moves the real cursor) were not tested.

## Rendered evidence and element actions under pinch zoom (local candidate, 2026-10-20)

The [validation record](test-results/pinch-evidence-actions.json) covers what
the pinch-zoom coordinate fix left out. Observation, element revalidation, the
in-page pointer check and agent pointer movement still used the unpinched layout
viewport. Under the same 2x synthetic touchpad pinch, on the unfixed
`XenonAuthTest` DLL, a control outside the 458.5 x 344.5 view was reported
verified with an action reference, and the observation viewport stayed 917 x
689. A reference observed before the pinch clicked that control, and the page
saw mouse events outside the view. `xenon_inspect` returned
`inspection_changed` on all five attempts. A pointer move starting from a
pre-pinch position sent seven mousemove events outside the view. A 3x pinch
also showed that scrolling put the wheel at the layout viewport center, outside
the view.

The rendered viewport is now the visual viewport. Observations report it as
`viewport`, the same rectangle as screenshots. Element points must fall inside
it, and pointer movement and the scroll wheel stay inside it. The fixed DLL
passed all nine pinch checks. Rebased onto main, the production DLL passed 16
visibility checks (including the 120% browser-zoom pass), 17 integration, 17
upload-entry and 10 screenshot checks, and the full build passed 24 native
groups. A person's pinch now also pauses the tab; the test driver's pinch does
not, so it still reaches these native checks. The stale reference was rejected
by the rendered-evidence recheck, so the new final-point guard was not
exercised by a rejection. The pinch came from the test-only driver on one 100%
monitor; physical touch input, display scaling or browser zoom combined with
pinch, and out-of-process frames while pinched were not live-tested.

## Background native popups (isolated PR validation, 2026-10-20)

`node tests/background-popup-integration.mjs --foreground` uses a disposable
production browser and a loopback page. The final isolated PR build passed the
complete sequence twice on the same binaries: covered select/suggestion actions
cannot display extra windows or capture the pointer; background selections and
button clicks reach the website; foreground dropdowns and datalist suggestions
open and reopen; tab switching, minimization and another synthetic window taking
foreground dismiss native popups. The foreground mode needs an uninterrupted
interactive desktop and focuses the selected CEF page through a native driver
before checking datalist suggestions.

Validation caught and fixed a renderer lifecycle regression: closing the HWND
alone hid a dropdown but prevented reopening it after a tab switch. The guard
now notifies CEF before closing any remaining native widget. Native tests cover
deferred dismissal, stale queued closes, source destruction, synchronous widget
destruction during CEF dismissal, original child ownership and unrelated shell
popups.

The first isolated full build passed all 24 native suites, 119 adapter/script
tests and five branding checks. On the final build, the native UI regressions
passed; an unrelated storage-cleanup test returned Windows error 145 (directory
not empty), then passed on a focused rerun of the unchanged binary. Final
branding checks passed. The [binary-bound record](test-results/background-popups.json)
retains the original shared-checkout reproduction, isolated reopening failure,
fixture-focus failures, storage failure and final passing runs. These are MCP
and native-message fixture checks, not physical-input or real PD Portal
acceptance. Installed browser files and personal profiles were not modified.

## Direct file lookup (local candidate, 2026-10-20)

The [validation record](test-results/upload-direct-path.json) covers
`xenon_files` with an approved `folderId` and exact `relativePath`. The native
regression first reproduced a known file missing beyond 5,000 entries with no
continuation, then passed direct lookup plus path, scope, selected-resource,
protected-file, link, replacement and revocation checks. Both MCP SDK versions
accept and forward the new parameter and reject invalid combinations.

The isolated production browser build passed all 17 upload-entry checks. The
live suite resolved a file beyond the recursive depth limit and verified its
contents at the synthetic upload server. Typechecking and all 146 adapter and
fixed-script tests passed. Initial reproduction, sandbox build/cleanup failures
and a corrected test-fixture interference failure remain recorded. This covers
the current working tree, including pre-existing changes, with synthetic grants
and disposable profiles; it does not establish real portal, physical UI or
installed-package behavior. The running user browser was preserved. Restart
with the updated browser and reconnect the adapter before using direct lookup.

## Touch, pen and touchpad human input (local candidate, 2026-10-20)

A source review of Chromium 154 found that touchscreen, pen and
precision-touchpad input did not pause agents or invalidate their evidence.
Touch and pen arrive as posted `WM_POINTER` messages to the page window, and
Xenon's message hook classified only mouse buttons, wheel, keys and IME.
Chromium hands a precision-touchpad scroll or pinch to DirectManipulation after
a sent `DM_POINTERHITTEST`, so no queued message follows. Touch and pen
contacts now hold the pause until they lift. A thread `WH_CALLWNDPROC` hook
detects the touchpad hit test and holds that gesture while Windows reports the
contact, for at most five seconds.

The full browser build passed 23 native groups, including new
`native_input_policy_tests` cases for contacts, pinch, separate pointer and key
IDs, reused pointer IDs, reconciliation and the touchpad bound. Adapter (22),
login capture (42), human autofill (31), protected login (10), form fill (6)
and passkey guard (8) tests and the branding check passed. No live suite ran,
and no touchscreen, pen or precision touchpad was used. Message classification
on real hardware is unverified. So is whether `GetPointerInfo` keeps reporting
a DirectManipulation-owned touchpad contact until it lifts; the five-second
bound covers that case.

## Screenshot coordinates under pinch zoom and right-to-left pages (local candidate, 2026-10-20)

The [validation record](test-results/screenshot-pinch-rtl.json) covers two
suspected gaps in screenshot-coordinate checks. Chromium enables pinch zoom
from a touchscreen or precision touchpad. Xenon's human-input hooks handle mouse
buttons, wheel, keys and IME, not that gesture. The checks used the unpinched layout viewport. A 2x pinch kept
that viewport at 917 x 689 CSS px while the visible part shrank to 458.5 x
344.5 at offset 229, 172. On the unfixed `XenonAuthTest` DLL, coordinates from
a screenshot taken before the pinch were accepted, and a coordinate at 644, 473
outside the pinched view clicked a target the screenshot did not show. Checks
now use the visual viewport for bounds and both viewports for staleness. The
fixed DLL passed all four pinch checks. The pinch came from a compile-time-gated
`XenonAuthTest` driver requesting Chromium's synthetic touchpad pinch, not from
physical touch input.

A right-to-left page did not need a fix. Chromium 154 keeps the main frame's
vertical scrollbar on the right (a left-side setting exists only for web
tests), and both the unfixed and fixed production DLLs passed the new
right-to-left check. The fixed production DLL also passed all 13 visibility
checks and all 17 production integration checks. The full build passed 23
native groups. Only one 100% monitor was used. Pinch combined with display
scaling or browser zoom, overlay scrollbars, rendered evidence and element
references while pinched, and `xenon_inspect` while pinched were not
live-tested.

## Screenshot scale beside scrollbars (local candidate, 2026-10-20)

The [validation record](test-results/screenshot-scale.json) covers screenshot
coordinates near the right and bottom edges. Chromium's unclipped surface
capture includes the scrollbars, so dividing the image by the CSS viewport gave
`scaleX` 1.0164 and `scaleY` 1.0223 at 100% with 15 CSS px scrollbars. The
pre-fix production DLL clicked at x 896.3 for a 12 px target at 905–917, and
the page recorded no click. The rebuilt DLL reports a scale of exactly 1. It
passed all 12 visibility checks, which include hits on targets beside both
scrollbars and rejection of a scrollbar pixel without input, and all 17
production integration checks. The full build passed 22 native groups.

The first run of the new check on the fixed DLL failed. Its fixed-position
readout overlapped the heading block and was withheld from rendered evidence,
so no click could be confirmed. That failure is retained in the record; the
readout now sits in an in-flow row. Only one 100% monitor was available, so
display scaling, browser zoom, pinch zoom, right-to-left scrollbars and overlay
scrollbars were not live-tested.

## Device upload text entries (local candidate, 2026-10-19)

The [validation record](test-results/upload-device-text.json) covers a verified
StaticText **Upload from Device** entry with a non-semantic wrapper and hidden
file input. The pre-fix production DLL reproduced `unsupported_upload_entry`
without activation or selection. The rebuilt production DLL passed all 17
upload checks, including exact approved contents, a trusted click on the text
inside a larger container, cross-origin frame uploads, stale text, disabled
ancestors, invalid/cross-workspace grants, absent choosers and different-frame
delegation denial. No OS picker automation or hidden-input search was used.

The same DLL passed 11 visibility checks and seven batch checks. The full build
passed 22 native groups and branding checks; typechecking and all 146 adapter
and fixed-script tests passed. Initial restricted-runner startup, build and ACL
cleanup failures remain recorded separately from successful unrestricted runs.
These results cover the local working tree, including pre-existing extension
manager changes, with synthetic loopback pages/files and disposable profiles.
They do not establish real SAP upload success, physical UI coverage, installed
package validation or publication. The browser and adapter must restart with
the rebuilt files before an existing agent session can use this fix.

## 0.1.7 release

0.1.7 was published on October 18 as a regular GitHub release, marked latest. The [publication record](test-results/release-0.1.7-publication.json) binds the `v0.1.7` tag and all four uploaded asset digests to the local build described below. The files were built from `bf38be4`, whose tree equals the merged source commit `6b61e80`. Every live, video and pacing report records the published `Xenon.dll`, and the video and pacing reports also record its libcef.dll. The downloaded ZIP passed `verify-package.ps1` again after publication.

0.1.7 replaces the withdrawn 0.1.6. Its packages use the Windows-decoder engine from `cef.mediaSourceBuild` with all three pinned patches, so its files were built, packaged and tested on a maintainer's Windows 11 machine rather than taken from hosted CI. CI's run on the release pull request uses the stock engine, and its artifacts are not published.

The engine's existing source build was updated with `build-cef-media.ps1 -UpdateVideoScheduling -BuildOnly`. Its audit passed with all three patches applied, the expected GN arguments, no FFmpeg H.264/AAC decoders or OpenH264, and the distribution matching the build output. Its libcef.dll (SHA-256 `44ef0602…831601`) is the engine of the covered-tab pacing record below. Xenon was then built against it from the release source with `build.ps1 -CefRoot <distribution> -Test`. It passed 20 native groups; 22 adapter, 42 login-capture, 31 human-autofill, 10 protected-login, 6 form-fill and 8 passkey-guard checks; and 5 branding checks. Package verification covered all 1,331 manifest hashes, 51 isolated installer checks passed, and the extracted ZIP passed 6 smoke checks.

On the packaged DLLs, the eight live suites hosted CI runs passed (61 checks), with the theme suite in Windows dark mode. `media-integration.mjs --windows-codecs` passed: H.264/AAC MP4 and Media Source playback, nonblank decoded pictures, measured AAC PCM, pause, seek/resume and volume, with the engine's provenance bound to the tested DLL. The VP8 WebM mode passed too. `frame-pacing-integration.mjs` measured about 1 fps in an idle covered agent tab and full rate after an observation and while selected, and an agent click on the idle covered tab passed the input guard. The publication record binds these results to the published files' hashes.

One failure is kept. On a preliminary package of the same source, the first `persistence-integration.mjs` run failed its first check because the second workspace's tab reported `HUMAN_INPUT_PAUSED`, with five human-activity events and held input. This most likely came from real keyboard or mouse input reaching the visible test window while the desktop was in use; the cause was not confirmed. The unchanged binaries passed all three checks on the next run.

Not covered: hosted CI cannot reproduce these binaries. Video was checked only with synthetic loopback fixtures, with no real website, audible output, physical player input or DRM. The address-suggestion, site-information, browser-essentials and agent-gate suites were not rerun; their earlier results are below. Full screen, screen sharing, the Import window and Default apps registration were not exercised, and help requests have no live suite. Native update discovery of 0.1.7 can be checked only after publication. Physical input, high contrast, other DPI, assistive technology and real accounts remain unverified. Downloads remain unsigned: published hashes establish byte integrity, not publisher identity.

## 0.1.6 release

0.1.6 was published on October 17 as a regular GitHub release, marked latest, and switched back to a draft the same day because codec licensing for distributing its built-in H.264 and AAC decoders had not been confirmed; v0.1.5 is the latest published release again. It was the first release whose packages used the codec-enabled CEF engine (H.264/AAC), so its files were built, packaged and tested locally rather than taken from hosted CI, which builds only the stock engine.

The [publication record](test-results/release-0.1.6-publication.json) binds the `v0.1.6` tag and all four uploaded asset digests to that local build. The files were built from `24e41d5`, whose tree equals the merged source commit `4ef7617`. The build passed 19 native groups; 22 adapter, 42 login-capture, 31 human-autofill, 10 protected-login, 6 form-fill and 8 passkey-guard checks; and 5 branding checks. Package verification covered all 1,325 manifest hashes, and 50 isolated installer checks passed. The extracted ZIP passed 6 smoke checks. The packaged DLL and engine then passed the eight live suites hosted CI runs (61 checks, theme suite in Windows dark mode) and both playback suites: H.264/AAC MP4 with decoded frames, non-silent AAC and Media Source streaming, and VP8 WebM. The downloaded ZIP passed `verify-package.ps1` again after publication.

Two failures are kept in the record. The first package was rejected by `verify-package.ps1` because Windows PowerShell 5.1 wrote backslash ZIP entry names; `scripts/package.ps1` now turns that off. The first smoke run failed once with `controller_unavailable` on its first `tab_create`; the unchanged package passed on the next run, and the cause was not determined.

Not covered: hosted CI cannot reproduce these binaries. Video was checked only with synthetic loopback fixtures, with no real website, audible output, physical player input or DRM. Full screen, screen sharing, the Import window and Default apps registration were not exercised, and help requests have no live suite. Native update discovery of 0.1.6 was not exercised. Physical input, high contrast, other DPI, assistive technology and real accounts remain unverified. Downloads remain unsigned: published hashes establish byte integrity, not publisher identity.

## Agent cursor and connected glow

The 2026-10-17 local Release build gives the agent arrow a status-colored outline
and outward alpha glow, with one continuous inward glow around the selected tab
and page. Gray frames have no glow, and the cursor hides during the human-input
pause. The overlays are transparent to input and do not resize website content.

The full native build and focused `native_ui_tests`, `pointer_motion_tests` and
`native_input_policy_tests` passed. All 17 production integration checks passed
on the connected-glow build, including website clicks, drag handoff and retained page state;
the [validation record](test-results/agent-overlay-2026-10-17.json) binds these
results to the binary and retains earlier harness limitations.
Pixel tests at 100% and 200% scale check the
opposite fade directions, transparent page center, cursor fill, and absence of
a vertical glow seam across the tab opening. Visibility tests cover available,
paused and unavailable agents. Computer Use on a disposable loopback fixture
confirmed the connected teal glow, gray frame without glow, and cursor hiding
with the orange pause and returning after idle with ownership retained. These
were synthesized Windows clicks, not physical-device tests. High contrast,
multi-monitor/DPI interaction, IME and held-input visual acceptance were not run.

Later visual refinements remove the cursor stem and give workspace **+**, **…**
and tab-close actions a distinct borderless gray hover fill. Each refinement
passed a full Release build and `native_ui_tests` and was inspected in the
disposable demo using Computer Use. The record retains each build hash; the
17-check integration run precedes these paint-only refinements.
Before opening the PR, all 19 native suites and all 137 adapter/fixed-script
tests passed on the final source, and `git diff --check` passed.

## Browser essentials candidate

This candidate adds full screen, site icons, links for other apps, screen sharing, default-browser registration and launch links, the bookmark manager with folders, history search and deletion, Clear browsing data, bookmark and history import, and Settings with a search-engine choice.

The [validation record](test-results/browser-essentials-2026-10-16.json) binds a local Release build (the media-enabled CEF distribution of the website video record) to all 19 native groups, including the new `browser_import_tests` and the search-engine checks added to `native_ui_tests`, and 51 isolated installer checks, including four for the new browser registration. On that same DLL, `browser-essentials-integration.ps1` passed 9 checks and `agent-gates-integration.mjs` passed 2, and the affected existing suites passed unchanged: address suggestions (10), site information (7), browser language (6), passkey gate (4), production `integration.mjs` (17) and branding (5).

Earlier failures, now fixed and retained in the record: a link handed to an already-running browser never opened, because CEF forwards only its own switches when command-line arguments are disabled (Xenon now hands links over itself); a relaunch link with spaces could have opened twice; and two test-script defects.

Not covered: full screen was not entered and screen sharing was not allowed, since either would cover or capture the real screen, so the full-screen layout, notice, **Esc**, agent outline, sharing indicator and screenshot withholding during capture are unverified in a running browser. The Import window was not opened, because it lists the real browser profiles on this computer; import is covered only by synthetic native tests. Registering as the default browser was checked in the installer fixture's isolated key, not with Windows' Default apps. **Move to…**, renaming folders in the tree, Chinese text in the new windows, high contrast, other DPI, physical input, assistive technology and real websites remain manual checks.

## 0.1.5 release

0.1.5 is a regular GitHub release, marked latest. It adds Default and Sensitive password groups, the passkey gate and the resize repaint fix for toolbar controls. Dependency pins are unchanged.

The [publication record](test-results/release-0.1.5-publication.json) binds the `v0.1.5` tag and all four uploaded asset digests to the artifacts of the push CI run for merged commit `4304b06d96f845015f1f420c4ddcc72f3e6d6bf8`. That run passed 16 native groups; 22 adapter, 42 login-capture, 31 human-autofill, 10 protected-login, 6 form-fill and 8 passkey-guard checks; 5 branding checks; and 47 isolated installer checks. It verified all 1,319 ZIP manifest hashes, passed 6 package smoke checks on the extracted ZIP, and then ran eight live suites on the packaged DLL: 61 checks, including the passkey gate, with the theme suite in Windows light mode. The downloaded ZIP passed `verify-package.ps1` again locally. The CEF index checked October 15 still lists the pinned 154.0.34 as its highest stable Windows x64 build, with CEF 156 only in beta, while Chrome's Windows stable channel is at 156.0.8078.36.

The record covers that hosted run and the local package re-verification only. The password-group controls have never been displayed or clicked, and no suite runs a real WebAuthn ceremony. Native update discovery of 0.1.5 was not exercised. Physical input, high contrast, other DPI, assistive technology and real accounts remain unverified. Downloads remain unsigned: published hashes establish byte integrity, not publisher identity.

## Website video regression

The [2026-10-17 Windows-codec validation record](test-results/windows-codecs-2026-10-17.json)
binds the Windows-decoder engine to passing MP4 and Media Source video/audio,
seek/resume, VP8 WebM, 19 native tests, adapter/fixed-script checks, five branding
checks and 17 production browser integration checks. Seven codec-audit tests
also passed. An initial streamed-AAC silence failure is preserved in the record;
passing raw AAC access units to Media Foundation corrected it without changing
the playback assertions. Audio was measured as decoded PCM, not through speakers.

The [2026-10-16 validation record](test-results/website-video-2026-10-16.json)
binds the earlier FFmpeg-based Release build to passing H.264/AAC MP4,
Media Source streaming and VP8 playback, all 18 native tests, adapter/fixed-script
checks, bootstrap branding integrity and the production browser integration
suite. The MP4 fixture decoded video frames and non-silent AAC samples; pause,
seek and volume checks passed. Earlier setup failures are retained separately.

`python tests/cef-media-deps-tests.py` checks the source-build manifest filter:
only the six branded updater test archives are omitted, and changed dependency
structure fails closed. It does not require a Chromium checkout.
`python tests/cef-windows-codecs-tests.py` checks that the codec audit rejects
software fallback configurations and alternate bundled codecs, and binds its
manifest to artifact hashes. These are audit tests, not playback evidence.
`./tests/cef-media-recipe-tests.ps1` exercises the source-build marker gate:
exact previous recipes require their migration switch, backups preserve the
old marker, and unrelated or missing markers are rejected without modification.
It also covers the [engine recipe](BUILD.md#xenon-engine-releases): its separate
marker, `-AddPatches` only for markers that lack some current patches, and
rejection of codec markers, edited patches and other source pins.
`python tests/cef-engine-build-tests.py` checks that the pinned engine recipe
shares the codec recipe's source and engine-patch pins without the codec patch or
proprietary codecs. It also checks that lock edits enabling codecs fail. The
engine audit must reject effective codec GN values, bundled H.264/AAC, OpenH264,
a checkout carrying the codec patch, missing patches and stale distributions.
`./tests/cef-engine-release-tests.ps1` checks provenance acceptance in bootstrap,
build and packaging on synthetic distributions. The stock engine is accepted for
releases only while no archive is pinned, and only the pinned engine afterwards.
The codec engine, replaced or unlisted files, edited flags and both manifests are
refused. It checks the fixed release-asset URL and pin fields. It then packages a
synthetic engine with the real tar/7-Zip archive builder and extracts it with
bootstrap's extractor and checks. The size pin's download behavior is covered
by `tests/bootstrap-download-tests.mjs`. None of these tests builds CEF, downloads
an engine archive or exercises a real engine build's audit.

### Background animation and video frame recycling

The Windows engine patch raises fenced video frame-release waits to the same
GPU scheduling priority as windowed compositors. In the local reproduction,
two background CSS animations previously caused 390–440 ms video freezes about
every five seconds while audio continued. The patched production candidate
recorded no frame callback gaps over 80 ms during three 30-second 1080p60 phases.
It still dropped 29/1800, 30/1803 and 30/1802 frames: the sustained diagnostic
**failed** its unchanged threshold of less than 1% dropped frames. This is an
improvement to long freezes, not complete smooth-playback acceptance.

The [sanitized validation record](test-results/video-frame-release-priority.json)
binds the production media and bootstrap checks to the tested engine and
application hashes. MP4/MSE pictures, decoded audio PCM, pause, seek/resume and
volume passed. Separate diagnostic `XenonAuthTest` site checks measured YouTube
1080p30 at about 0.68% cumulative dropped frames and Bilibili 360p30 at about
0.24% additional dropped frames during foreground measurement; these do not
establish production-site acceptance or higher-quality Bilibili playback.

Use `--sustained-dir <fixture-directory> --animated-background` with
`--require-h264` or `--windows-codecs` to reproduce the synthetic workload.
The [fixture README](../tests/fixtures/media/README.md) explains file generation
and the manual foreground step. `--browser-dir <complete-browser-directory>`
allows an isolated production candidate. No personal profile is required.

### Driver frame limit and covered-tab pacing

The remaining frame loss above was local to the test PC's GPU settings. Its
NVIDIA global profile set **Max Frame Rate = 144** (read through NVAPI, not
changed by the test). That limiter paces presents per D3D device, and Chromium
presents every Xenon window on one device. A standalone D3D11 program, with no
Chromium involved, confirmed it. Its second and third back-to-back
`Present(0)` calls on a shared device each waited 6.94 ms, one 144 Hz frame,
in 200 of 200 rounds. With one device per window they did not wait. A local
`XenonAuthTest` trace showed the same in the browser. During each background
animation burst, every present issued within the same frame interval as
another blocked the GPU main thread for about one frame. The selected tab's
video then reached the display at about 48 Hz, dropping one frame in five.
Triple-buffered root swap chains, interval-0 video presents, a higher device
frame latency and `DXGI_PRESENT_DO_NOT_WAIT` did not change it. The trace hook
was a local test-build change and is not part of the repository.

With the limit turned off, the unchanged #36 production candidate dropped 0
frames in local diagnostic runs (two 30-second phases each, muted video, no PCM
measurement): 1806 and 1803 frames with two animated background tabs; 1805 in
every phase with two side-by-side visible windows playing the clip at once; and
1805 and 1803 in one phase with both videos and the animated tabs. These used a local variant of the sustained page with an
MCP click, not the unchanged sustained suite. One sustained-suite attempt on that
candidate timed out waiting for its manual start and measured nothing; its
failure report was kept locally.

Covered tabs now paint at 1 frame per second unless an agent works in them (see
[BUILD](BUILD.md)), which removes most of their presents. `node
tests/frame-pacing-integration.mjs` checks this with a synthetic page that
counts animation frames in an agent tab covered by the selected tab. With Xenon's
engine build it expects 1 fps after each 15-second agent lease and full rate
after an observation and while the tab is selected. It also expects an agent
click on an idle covered tab to pass the two-frame input guard. Against the stock
engine, where pacing is ignored, it expects full rate throughout. The [sanitized
validation record](test-results/background-frame-rate.json) binds the result,
the native/adapter/script checks and five existing live suites to the tested
engine and application hashes.

The unchanged sustained suite (\--windows-codecs --animated-background\, three
30-second phases, manual start) **passed** on the pacing build with the limit
off: 0/1802, 0/1801 and 0/1801 dropped frames, no frame gaps over 80 ms and no
silent PCM samples. With the 144 fps limit turned back on, the same local
diagnostic variant dropped 29/1805 and 25/1803 frames on the #36 candidate, in
five-second bursts, and 0/1800 and 0/1804 on the pacing build. Both runs waited
16 seconds after creating the background tabs so their agent lease had lapsed.
The unchanged sustained suite also **passed** with the limit on: 4/1802 (0.22%),
0/1801 and 0/1802. Three of those drops came about two seconds into the first
phase, while the agent-created background tabs were still within their 15-second
creation lease; one came at 29 seconds.

`node tests/media-integration.mjs` starts the production browser in a disposable
profile and generates a synthetic VP8 clip in a loopback page. It checks decoded
frames, advancing playback time, pause, seek and volume, and records the runtime's
codec declarations. `node tests/media-integration.mjs --require-h264` instead
requires the generated MP4 fixture, H.264 frames with nonblank decoded pictures, non-silent decoded AAC PCM
from the playing media element, and Media Source streaming with both frames and
non-silent audio. Seeking and resuming must restore both video and audio.
A guarded MCP click starts the fixture and activates its audio
context. The final audio output is muted after measurement. It must fail on the
stock CEF distribution. `--windows-codecs` additionally requires the Windows
codec build manifest and verifies that it matches the actual engine DLL.
Build the optional media-enabled engine as described in [BUILD](BUILD.md#website-video-codecs)
before running the MP4 acceptance check. Reports under `out/media-integration-*.json`
include both application and CEF DLL hashes before and after the run.

The Windows recipe removes bundled H.264/AAC software decoding. It requires a
supported Windows GPU for H.264 and Media Foundation for AAC. The fixture records
Web Audio `decodeAudioData` support separately: this synchronous path does not
gain AAC support from the Windows media-element patch. Historical results above
describe the earlier FFmpeg build, not this Windows-decoder configuration.

These fixtures do not establish audible speaker output, physical player-control
input, DRM support or compatibility with every website. Run desktop suites
serially and keep their tested binaries unchanged throughout each run.

## Password groups candidate

This candidate adds Default and Sensitive saved-account groups, client- and workspace-level Default-group grants, and native controls for choosing and changing an account's group.

Windows CI [run 38657838302](https://github.com/Xero-05/xenon-browser/actions/runs/38657838302) on `9c5bea7` (pull request) built the browser and core with MSVC, passed all CTest native tests (including the new `default_account_group` and `account_groups` checks, `native_ui_tests`, the pipe transport and the vault IPv6 assertion) and the adapter/SDK suites, packaged and verified the release, passed the isolated installer and extracted-package smoke tests, and passed the seven live suites on the packaged DLL: `integration`, `batch-integration`, `visibility-integration`, `worker-lifecycle-integration`, `upload-entry-integration`, `persistence-integration` and `theme-integration`. Those live suites do not use saved accounts or the new controls.

What ran earlier, in a Linux container:

- `broker_tests` were built with GCC 13.3 against the pinned nlohmann JSON 3.12.0 header and a local, uncommitted security shim (OpenSSL SHA-256, `std::random_device`, no ACLs), as for the developer mode and desktop shell candidates. Every test outside the Windows-only pipe and persistence-failure cases passed, including the new `default_account_group`: no group access without a grant or without the vault directory; automatic-workspace inheritance of a client's Default group; Sensitive accounts excluded; the engine receiving the vault's exact origin and port, never a claimed one; individual and group grants combined; a relocated origin filtered out; a queued login cancelled before dispatch when its account becomes Sensitive; locked or failing lookups denying; inheritance, workspace-level grants, restrictions and the saved-account capability; and persistence across restart. Three deliberate broker mutations (ignoring the group, ignoring workspace inheritance, returning a fixed origin) each failed these checks; the fixed-origin mutation passed until a second Default account at another origin and port was added to the test.
- The same `broker_tests`, cross-compiled with MinGW-w64 against the real Windows security helpers and run under Wine 9.0, passed every case through the Windows-only workspace-removal and revocation persistence-failure cases, including `default_account_group`. Both this build and the unmodified baseline then stop at the same `pipe_transport` assertion (a second server binding an existing pipe name), a Wine named-pipe difference, so the pipe transport was not exercised.
- `vault_tests` were cross-compiled with MinGW-w64 GCC 13 (posix) against an SQLite 3.53.4 amalgamation taken from the npm `better-sqlite3` 13.0.3 package, because `www.sqlite.org` was unreachable, and run under Wine 9.0 with DPAPI. The new `account_groups` check passed: new accounts default to Sensitive unless Default is chosen; a recreated pre-group schema migrates its rows to Sensitive with credentials intact; updates, Save-prompt updates and CSV replacements keep the existing group while new accounts take the chosen one; reclassification, unknown accounts and the locked vault behave as documented; and groups survive reopening. The unmodified suite fails its IPv6-origin assertion identically under Wine, so the full suite was run with only that assertion relaxed in a scratch copy; everything else passed. A mutation that migrated legacy rows as Default failed the new check.
- `native_ui.cpp` passed a Windows-target syntax check with MinGW headers and stub CEF headers. A script confirmed that every new Controls string has a unique, non-empty Simplified Chinese translation.

Not covered: the Save bubble's **Sensitive** checkbox, the Passwords page controls, the import group question and the configuration dialog's **Allow all Default passwords** checkbox have not been displayed or clicked; the autofill list's **Agents:** line and the operator-run `auth-integration` suite have not run, with or without a Default-group grant.

## 0.1.4 release validation

The [0.1.4 record](test-results/release-0.1.4-validation.json) binds the full Release production/fixture build to all 15 native groups, 111 adapter/fixed-script checks, typechecking, 32 downloader/pointer checks, five branding checks and 47 isolated installer checks. The rebuilt production DLL passed 11 adversarial visibility checks, 26 filter/scroll/collection checks, two focused selection checks each in English/dark and Chinese/light, and the web-theme suite. The first build failed while committing copied-bootstrap branding resources; an unchanged-source retry passed and the failed log remains separate. No dependency pins changed. Package verification and extracted-package smoke checks are recorded after assembly. Physical input, high contrast, other DPI, assistive technology and real accounts remain unverified.

The uncertain-click/filter and native-selection follow-up passed the full Release build, all 15 native groups, adapter/script checks and five branding checks. The unchanged production DLL passed 11 adversarial visibility checks, 26 filter/scroll/collection checks and two focused developer-selection checks each in English/dark and Chinese/light. `node tests/developer-elements-integration.mjs --selection-only` verifies themed row pixels and that a selected-tab reclick hides keyboard focus cues while keyboard input restores them without changing tabs. The bookmark layout was inspected through a synthetic PrintWindow capture. The full developer suite could not acquire foreground activation; divider/hover and physical-input coverage are not claimed. Earlier failed clip-label and driver attempts remain separate. See the [binary-bound record](test-results/uncertain-click-filter-ui.json).

## Developer mode candidate

`developer_trace_tests` passed on Linux with the pinned nlohmann JSON 3.12.0 header, C++20 and warnings treated as errors. It covers opt-in capture, credential/input redaction, returned text/image evidence, response correlation, clear/disable boundaries, concurrent capture and retention limits. The modified broker passed a C++20 syntax check. The broker test executable compiled, but could not run here because native identity/crypto require Windows; its new developer-mode checks remain unverified.

That initial Linux run left Windows validation outstanding. The Windows follow-up below records completed checks; physical keyboard, assistive technology, multi-monitor/DPI and sustained-load acceptance remain manual work.

### Website-side elements panel: Windows follow-up

Developer mode now opens a right-side panel beside the selected website. Native diagnostic reads use the production rendered-viewport observation filter but do not publish or replace an agent's observation token or element references. The panel shows roles, rendered names, possible actions and selected-node metadata, with an interactable-controls filter. Authentication protection, private/internal tabs, held input and dialogs withhold these reads; selected-tab/document/input identity discards late replies. Permissions and actionability still require separate broker checks and fresh agent observations.

The full Windows production/fixture build, all 14 CTest suites (including `developer_trace_tests`, `developer_elements_tests` and `broker_tests`), adapter/script checks, typecheck and branding verification passed. The new `node tests/developer-elements-integration.mjs` suite passed all ten checks in both English/dark and Simplified Chinese/light on the same unchanged production DLL. It verifies filtered evidence, omission of hidden canaries and editable values, selection metadata without action tokens, row-to-page highlighting, page-to-row picking without activating the button, picker cancellation, the interactable filter, continued use of an agent reference after panel refreshes and picking, tab switching, protected-login clearing and disable/hide. Native window and keyboard messages drive the controls; `PrintWindow` verifies the blue outline and panel layout. This is not physical-device coverage. Frame transforms and complex shadow/frame picking were not exercised. See the [sanitized binary-bound record](test-results/developer-elements.json).

Selection uses a native overlay and Chromium node geometry/hit testing, separate from the agent's action references. Two earlier live runs of a Chromium inspect-mode prototype failed on picker cleanup and subsequent website input. That prototype was replaced; its failed reports remain separate from the passing final results.

The initial panel build failed on a mixed integer type in layout; that was corrected. The first two live attempts failed because the UI Automation driver could not read the custom native controls. The driver now uses native tree/list/keyboard messages and the panel's read-only detail field. Those failed local reports were retained, separately from the passing results.

Xenon is an early Windows x64 release. The attached test records distinguish exercised behavior from inherited Chromium features and remaining manual coverage. They are not an independent security audit or a production reliability certification.

### Google control inspection follow-up

Opt-in uncertain-click collection is exercised with `node tests/filter-matrix-integration.mjs --collect-uncertain-clicks`. It runs the 24 filter/scroll checks plus a collection lifecycle check. A screenshot-bound uncertain click must trigger the synthetic website callback and produce one allowlisted JSONL record with native hit ancestry and unsupported-clip diagnostics. Default-off, hover, a covering overlay, verified coordinate clicks, disabled collection and stale tokens must not add records; native clear must empty the file. The [collection validation record](test-results/uncertain-click-collection.json) binds all 25 matrix/collection, 19 inspector and 11 adversarial visibility checks to the current production DLL, together with four focused native suites and five branding checks. Native collector coverage includes redaction, numeric/type rejection, session restart, late-generation cancellation, the size cap, active clear and hard-link rejection without changing linked contents or ACLs. The new Controls layout was inspected through PrintWindow and the reopened preview remains off. Earlier failed collection and ACL-fixture runs remain separate. Physical human clicks and child-frame collection are not covered by these live tests.

Visibility uncertainty is now represented explicitly. Proven targets carry `visibilityStatus: verified`; viewport control candidates carry `visibilityStatus: uncertain`, a bounded structural reason, screenshot guidance and a non-actionable candidate ID. The native list distinguishes them. Approximate-hover coverage now checks dashed amber geometry for uncertain rows, positional hover through an overlay, persistence across refresh and alignment after scrolling, while keeping targetAvailable false. The earlier uncertainty record predates approximate highlighting. Candidate names contain only independently verified text. The matrix checks candidate-ID rejection, hidden-content exclusion, faint controls and a screenshot-bound coordinate click on a visually present rounded-clip control. The inspector suite additionally checks uncertain selection across refresh. The [visibility uncertainty record](test-results/uncertain-controls.json) describes the earlier binary. The [approximate hover record](test-results/approximate-hover.json) binds the current full build to 19 inspector checks, 24 filter cases, 11 adversarial visibility checks, three focused native suites and five branding checks. The covered-control outline was inspected in a synthetic screenshot; the open GitHub preview also produced approximate geometry for its uncertain row. Root-frame candidates support position-only fallback; child-frame candidates require native hit ancestry. These checks use native messages rather than physical mouse input.

The cross-page filter regression is `node tests/filter-matrix-integration.mjs`. It navigates one production CEF tab through 23 synthetic pages and then scrolls after another navigation. Cases cover rectangular pixel/percentage/negative/nested clips, zero-area and unsupported clips, distant and covering blur/backdrop effects, transformed blur, color inversion, faint controls, shadows and translations. Supported controls must produce a website click result; previous-document references must fail, and hidden text, AX labels and editable-value canaries must stay absent. Backdrop descendant geometry is admitted with uncertain rendered labels withheld. Reports use unique filenames and include the production DLL hash before and after testing. See the [sanitized cross-page record](test-results/filter-matrix.json) for final results and public-page coverage.

The final cross-page build passed all 23 matrix checks, 18 English/dark inspector checks, 11 adversarial visibility checks, three focused native suites and five branding checks. Anonymous public navigation through GitHub, LinkedIn, Wikipedia and MDN retained usable control lists on that same DLL. GitHub Search/Sign in/Sign up and hero controls are listed; native-message hover on Sign in selected its row and produced an outline after the asynchronous geometry check completed. Wikipedia's distant inversion no longer hides its search and language controls. Earlier failing reports were retained, including the LayoutView blur omission, backdrop paint regression and corrected synthetic fixture geometry. These are native-driver checks, not physical-device or exhaustive website coverage.

The LinkedIn filtering follow-up bounds backdrop output to its transformed border box instead of treating it as global uncertain paint. Covered controls, separate foreground filters, extending shadows and backdrop descendants retain conservative restrictions. The empty-selection details pane now shows omission diagnostics, with bounded untrusted paint-style examples restricted to the human inspector. All 18 inspector live checks passed in English/dark and Chinese/light on the same production DLL; three focused native suites, five branding checks and all 11 adversarial visibility checks passed. The Chinese driver initially selected rows before refresh completed; explicit fresh-evidence waits fixed that timing. The first adversarial run failed during restored-frame interaction and later reported a zero-size root viewport; an unchanged-binary rerun passed, with the failed report preserved separately. The public LinkedIn preview changed from zero to 82 elements and 35 interactable controls. See [binary-bound evidence](test-results/linkedin-filter.json). Physical hover remains manual coverage, and rendered coverage remains partial.

The page-scroll follow-up fixes native DOM hit testing by converting viewport mouse coordinates to document coordinates using current layout metrics. A moving viewport rejects the lookup, and changed native evidence or viewport schedules stationary-pointer matching and refreshes selected highlight geometry. Details expose both document bounds and viewport highlight bounds. All 17 live checks passed in English/dark and Chinese/light on the same production DLL, including a 1,600-pixel scroll, hover selection and outline alignment after a second scroll. Three focused native suites and five branding checks passed. Physical stationary hover remains manual coverage. The final anonymous Google scrolled-results check was blocked by an unusual-traffic challenge; the updated visible preview uses a local scrolling demo. Earlier failing scroll reports are preserved separately. See [binary-bound evidence](test-results/page-scroll-hover.json).

The search-results follow-up supports bounded 2D translations and axis-aligned unit reflections. Text requires the full element transform chain to finish upright; this admits Google's wrapper/child flips while withholding mirrored text. Consistent inline fragments retain paint ancestry and require a verified center inside an actual fragment. Reflected pointer quads retain the guarded screen-space sample, and a request version prevents superseded highlight callbacks from restoring stale errors. The details expose omission counts and bounded, native-only untrusted transform examples. All 16 live checks passed in English/dark and Chinese/light on the same production DLL, including observation, hover, highlight and website-state verification for a translated, split, reflected link. Three focused native suites and five branding checks passed. An anonymous Google results title matched its link row and blue outline without stale errors through native messages and PrintWindow. Physical hover remains manual coverage. Intermediate omissions and a Chinese driver timing failure are preserved separately. See [binary-bound evidence](test-results/google-results-hover.json).

The hover/scroll follow-up preserves code-pane scroll and text selection during refreshes, resets the view for a newly selected row, and adds passive hover matching while developer mode is enabled. Matching uses bounded accessibility ancestry and, where needed, up to 32 DOM parent identities, including pseudo-element and shadow-host links. Only IDs from the existing rendered list are returned. It does not expose page source, arbitrary scripts or hidden data through MCP. Highlight requests are coalesced and deferred during hover lookups; failed highlights report an error and retry after fresh evidence. All 15 production live checks passed in English/dark and Chinese/light on the same unchanged DLL, including nested text, SVG/pseudo-element matching, scroll persistence and prior ownership/reference/privacy checks. Three focused native suites and five branding checks passed. Anonymous Google native-message checks and PrintWindow confirmed the Apps hover match and blue outlines for Apps and Sign in. The runner could not move the desktop pointer or activate the preview reliably; physical hovering remains manual coverage. Earlier failed driver and ancestry attempts were retained separately. See [binary-bound evidence](test-results/elements-hover-scroll.json).

The subsequent inspector update adds draggable panel-width and list/details dividers, with minimum-size limits. Its native-only label fallback uses the accessibility name of an already admitted rendered element when no rendered name is verified. The list records the label source; details mark this website metadata untrusted and unverified. MCP observations retain the original rendered-name boundary and omit the fallback. Both English/dark and Chinese/light production live runs passed all 12 checks, including divider limits, independent sizes, picker/highlight behavior, authentication clearing, input-value omission and continued use of existing agent references. Three focused native suites and five branding checks passed. Anonymous Google inspection confirmed **Sign in** and **Google apps** as labeled, targetable controls, and a native screenshot verified the Apps highlight. See [sanitized evidence](test-results/elements-resize-labels.json). This uses native messages and PrintWindow, not physical input. An initial DLL-lock build failure and restricted-runner ACL failure were retained separately; ordinary-user reruns passed.

Inspection of the anonymous Google homepage found that its search textarea has AX role `combobox` and was incorrectly classified as **Select**. Editable INPUT/TEXTAREA controls now take priority and show **Fill / key**. Native submit-button captions and icon-only accessibility names are still unavailable as verified rendered text; their control roles, bounds and references remain present. The panel now says **No verified label** and explains this distinction in selected-node details.

The rebuilt production/fixture targets, focused `developer_elements_tests`, `visible_evidence_tests` and `native_ui_tests`, and five branding checks passed. All eleven inspector live checks passed in English/dark and Chinese/light on the same production DLL, including native submit controls, editable comboboxes, hidden-label/value omission and existing selection/input lifecycle behavior. An attempted submit-caption filter change failed its live label assertion and was reverted; that failed local report is retained. See the [binary-bound follow-up record](test-results/google-controls.json).

## Native Chromium extension manager

Set `$env:XENON_TEST_EXTENSION_QUICKLIST='1'` before the manager integration suite to seed three generated extensions in its disposable profile and serve a local synthetic website. The suite verifies enabled/disabled filtering, current-site versus unrelated permissions, an actual content-script change to that website, absence of extra visible/broker tabs, and withholding results after source navigation. Combined with `$env:XENON_TEST_STORE_INSTALL='1'`, it also checks that a newly installed signed Store extension appears immediately. These are compile-time native-driver checks, not physical toolbar-menu acceptance. See [quick-list evidence](test-results/extension-quicklist.json).

`extension_manager_policy_tests` is part of native CTest. It checks exact Chrome Web Store, manager WebUI and extension origins and refuses lookalike hosts, unrelated privileged pages, file/JavaScript URLs, credentials, explicit ports, malformed extension IDs and control characters. It also checks the native `xenon://extensions/` address alias, path/query round trips, unchanged website addresses, and rejection of that alias by page-navigation policy.

`node tests/extension-manager-integration.mjs` requires the separate `XenonAuthTest` target (configure CMake with `-DXENON_BUILD_AUTH_FIXTURE=ON`, then perform the full build). Its compile-time-gated driver accepts only fixed actions in a marked disposable `.cache/extension-manager-integration-*` profile. It checks that the actual `chrome://extensions/` WebUI loads with `chrome.developerPrivate`, that repeated opens reuse the workspace tab without entering the broker inventory, that its native parent is a Xenon content host inside a Xenon shell, that the view fills that host without a Chromium toolbar or window caption, that the real HTTPS Store receives Chromium's `webstorePrivate.beginInstallWithManifest3` binding, that closing preserves workspace tabs and permits reopening, and that browser shutdown drains management views before CEF exits. The default run cancels a generated install preflight by closing its manager and checks rejection of an untracked Chrome window. It does not install an extension, approve installation, add a Store-origin certificate exception or expose a production fixture/MCP endpoint. The optional full-install run below adds fixed native confirmation within its own disposable test process.

The fixture now checks Views/compositor visibility and requires varied painted pixels from its own management HWND, saving a synthetic PNG under its disposable profile and `out/`. The PNG is visually inspected: DOM/API and geometry checks alone previously passed while the manager appeared blank. It does not capture the user's browser or desktop. Set `$env:XENON_TEST_BUILD_DIR='build/extensions-fix'` before running the manager or production browsing suite to validate a separate build; the default remains `build`.

The manager fixture also opens the compact drawer, checks computed visibility of its product/Store artwork, and captures the drawer. Its fixed `sidebar-link` and `empty-store-link` actions click the actual rendered manager anchors, then verify navigation to the real HTTPS Store with native install API bindings and unchanged broker tab count. These actions are compiled only into `XenonAuthTest`, accept no caller-supplied script or URL, and exercise WebUI clicks with a synthetic user gesture rather than physical OS input. Fixture result publication retains and retries a completed result if Windows briefly locks the previous result file; shutdown waits for that publication.

On 2026-10-19, the full build/check script passed, followed by the final presentation/fixture rebuild, all eight manager checks and bootstrap branding integrity verification. The compact drawer and loaded Store captures were visually inspected. The [logo/link validation record](test-results/extension-manager-links.json) binds these results to their DLL hashes. The first live run reached the Store from both page links but timed out because the fixture's completed result could not replace a temporarily locked file; that failed report is preserved separately from the passing rerun. No extension was installed and no permission confirmation was approved in these checks.

Subsequent user acceptance still reported Store navigation staying on Extensions. Brief inspection of the running window reproduced a loading attempt that returned to the manager; the earlier passing fixtures did not establish success in that user session. The fixture now exercises the native shell menu command via `WM_COMMAND`, sends fixed native mouse messages to the rendered sidebar link, uses the public workspace/context validation path, and checks navigation after the last ordinary tab closes. Optional `XENON_TEST_LEGACY_EXTENSION=1` seeds a harmless generated MV3 package only in the disposable profile. These stronger checks passed, including the loaded-extension variant, but did not reproduce the user's cancellation. The separate diagnostic build adds native browser-error/blocked-redirect status without weakening navigation or TLS rules. Its 22 native suites and nine manager checks passed; the user subsequently reported **Extensions blocked a redirect**, confirming a native policy rejection. A follow-up diagnostic displays only the rejected hostname/port (or a generic unsupported-address category), with unit checks for credential/path/query redaction; it also distinguishes workspace-context failure. The user then reported **Blocked: accounts.google.com**, identifying a Google account-service route omitted from the navigation policy. The fix admits the exact HTTPS account host only during an active Store visit and revokes that allowance on return to Extensions or an extension page. Native unit regressions cover the route sequence, revocation and malformed/lookalike origins. The extended live fixture uses a disposable profile to reject direct account navigation, load the real signed-out account page during a Store visit, verify that it has neither Store nor developer APIs, return to the Store, and reject account navigation again after returning to Extensions. These ten manager checks passed, alongside 22 native suites. No credentials were entered; this is a fixed native navigation sequence, not an automated Google sign-in or proof of the original authenticated-session redirect completing. Earlier attempts using a synthetic resource responder failed and are retained in the diagnostic evidence. That responder was removed; the final fixture loads the actual Google account page. Final acceptance in the user's original session remains pending. See [diagnostic evidence](test-results/extension-manager-navigation-diagnostic.json). No personal profile was copied, inspected on disk, or changed to make a test pass; computer control was released after the brief UI inspection.

Latest local validation on 2026-10-18 built the address/visibility fix separately in `build/extensions-fix` to preserve the running browser. All 22 native suites, six manager checks (including painted content), and 17 production browsing checks passed. The [sanitized validation record](test-results/extension-manager.json) binds those results to the new DLL hashes and retains prior validation: the earlier full script's adapter/SDK, credential-script and branding checks, persistence checks, and user-rejected standalone-window implementation. Earlier geometry/API passes did not prove visual rendering; the user reported a blank manager, fixed by showing the Views window through its own API. Failed logs/reports remain local with unique run names, including premature native-parent destruction during close, a file-lock race in fixture requests and an admission check before CEF associated its BrowserView. These are native-driver and protocol checks, not physical UI acceptance. At that validation stage, a complete direct Store install and its native confirmation had not been automated. The installation regression below now covers one fixed signed-package install. Physical permission-dialog placement, keyboard/focus behavior during everyday use, later automatic updates, extension debugging and full popup/tab-API behavior with Alloy pages remain manual acceptance work.

### Store installation crash regression

On 2026-10-19 the user reported a crash after downloading a Store extension. A debugger reproduction reached `ExtensionInstalledWatcher::RunCallback` and `TriggerPostInstallDialog`: Chromium had queued a completion callback for an auxiliary browser whose tab Xenon had already closed. The fix holds auxiliary browsers outside visible/broker inventories, suppresses their native show requests and adds one UI queue turn before Chromium schedules its own close. The queued completion callback runs first. Auxiliary windows cannot navigate or change manager state, and workspace removal/shutdown wait for their close callbacks. Experimental toolbar and infobar-feature workarounds were removed.

Set `$env:XENON_TEST_STORE_INSTALL='1'` before running `node tests/extension-manager-integration.mjs` to opt into a full installation of the fixed public package `ddkjiahejlhfcafbddmgiahcphecmpfh` (uBlock Origin Lite) inside the generated profile. The driver requires a separate exact opt-in marker, the marked disposable profile, the fixed package page, a native **Add extension** button and the package name in the same native window. UI Automation inspects only the fixture process and invokes that fixed button from a background worker; it does not send global mouse/keyboard input. Once invocation is dispatched, an uncertain outcome is not retried. This driver and its Windows UI Automation link dependencies are compiled only into `XenonAuthTest`, with no production or MCP endpoint.

For toolbar action coverage, set `XENON_TEST_EXTENSION_QUICKLIST=1` and `XENON_TEST_EXTENSION_ACTIONS=1`, and select a complete isolated build with `XENON_TEST_BUILD_DIR=build/extension-actions`. Generated extensions and a loopback website verify compact panel rendering, pinning, a genuine dynamic popup view, current-window active-tab lookup, an `activeTab`-only script effect, no-popup `onClicked`, navigation/dismissal cleanup, and actual site/per-extension content-script blocking after reload. Unrequested origins must reject permission expansion. Only read-only metadata may retry stale results; mutation outcomes are never replayed. The report hashes both native and engine DLLs before and after the run. These fixed native-driver checks do not establish physical keyboard, screen-reader or everyday third-party extension acceptance. That toolbar-only mode does not cover side panels or programmatic popup opening; use the extended mode below.

The [2026-10-19 action-popup validation record](test-results/extension-action-popups.json) binds the isolated build and SHA-pinned engine patch to 27 combined action/manager/signed-install checks, 22 native suites, 17 production browsing checks, actual media playback and recipe/provenance checks. It retains the earlier failed lifecycle attempts and distinguishes native fixture checks from physical UI acceptance. The compact panel screenshot contains only generated extension names and a loopback site.

Select `XENON_TEST_BUILD_DIR=build/extension-surfaces` and set `XENON_TEST_EXTENSION_SURFACES=1` with both toolbar flags to add two generated extensions and a CORS-enabled loopback command/report fixture. The API-popup extension has `activeTab` and `scripting` but no website permissions: a real popup must load, its API promise must resolve, its active-tab URL must remain scrubbed and script injection must fail. Existing-popup conflicts and unknown window IDs must reject without replacing the current popup. Side-panel checks require a genuine side-panel context, reserved/resizable website geometry, profile-scoped tab activation, global/contextual selection behavior, extension close/options/events, Chromium's user-gesture enforcement and protected/private rejection. The native driver accepts fixed actions only in the marked disposable profile. Loopback polling is generated extension code, never a production remote-control endpoint. Screenshots contain only the synthetic fixture window. The run waits for its test window to be active and preserves the production foreground checks; keep that window active during popup and panel checks.

The full run first confirms a deliberately mismatched manifest and requires Chromium to reject the signed package without installing it or crashing. It then uses the real Store page and native confirmation for a genuine installation, requires the Store API to report the package installed, and verifies auxiliary-browser drainage and clean shutdown. Short marked profile names and profile-local `TEMP`/`TMP` avoid unrelated cross-volume copies and legacy Windows path-length failures. The fixture applies Chromium's `disable-background-networking` and `disable-default-apps` switches to reduce unrelated background activity. `XENON_TEST_DEBUGGER` optionally selects a debugger executable; `XENON_TEST_SYMBOL_PATH` adds a local symbol directory. Debugger logs remain local under ignored `out/`.

The final full native build, all 22 native suites and all 14 manager checks passed. The real signed-package installation and mismatch rejection both completed under the debugger. This is native test-driver coverage with UI Automation, not physical UI acceptance in the user's profile or compatibility evidence for arbitrary extensions. Historical failures and the final DLL hashes are recorded in [installation crash evidence](test-results/extension-manager-install-crash.json). No personal profile was inspected or changed, and no personal screenshots or raw browsing logs are published.

On 2026-10-19 the extended `build/extension-surfaces` run passed all 36 checks, including nine new programmatic-popup and side-panel checks and a real signed Store installation. The production binary passed all 17 browsing checks and synthetic H.264/AAC MP4 and Media Source playback with decoded pictures and measured audio. All 22 native suites and the source recipe/release checks passed. The [extension surfaces record](test-results/extension-surfaces.json) binds these results to the native/engine hashes and retains the earlier compilation, focus, guard-registration and new-tab lifecycle failures. These are fixed native-driver checks in disposable profiles, not physical keyboard/accessibility acceptance or compatibility evidence for arbitrary extensions. Global panels follow selected live tabs; destroying the currently bound source tab retires the panel.

## Legacy Web Store link installation

`web_store_tests` (part of native CTest) use generated, memory-only RSA and P-256 keys to check signature integrity, algorithm mismatch, malformed keys, required Web Store proof, extension-ID binding and malformed CRX rejection. Synthetic ZIPs cover unsafe Windows names, traversal, links, duplicate case-insensitive paths, file/directory conflicts, oversized entries, truncation and deflated empty directories. They also check exact-link parsing, allowed transport destinations and complete permission disclosure. No fixture key or publisher exception is added to production verification.

The opt-in `cef_web_store_tests` target downloads a real, publicly available store-signed package. It verifies signatures, safe decompression/CRC, cancellation, localized permission review, managed installation, stable key-derived ID, duplicate refusal, restart selection and deferred removal in a generated `.cache/web-store-package-tests/` directory. It never loads or executes the extension. After a full browser build, run in PowerShell (the example uses uBlock Origin Lite only as a package fixture):

```powershell
$env:PATH = (Join-Path (Get-Location) 'build/app/Release') + ';' + $env:PATH
./build/Release/cef_web_store_tests.exe --download ddkjiahejlhfcafbddmgiahcphecmpfh
```

Local validation on 2026-10-18 passed the full `./scripts/build.ps1 -Test` checks (21 native suites plus adapter/SDK and credential-script checks), the focused package tests, the opt-in real-package lifecycle check and all 17 production `tests/integration.mjs` checks. The production suite's local `out/integration-results.json` binds that run to `Xenon.dll` SHA-256 `5e4db421bee961ae38550db08cca1789ff979b4df683b16afd400e7b520f515e` and confirms the binary stayed unchanged throughout the run. An earlier real-package attempt exposed rejection of deflated empty directories, now covered by a regression. The standalone harness initially lacked CEF API initialization, then held its own manifest file open during cleanup; both harness issues were corrected and their failed logs retained locally. Native UI inspection of an earlier local build in a disposable production-browser profile verified the Extensions entry, link dialog, signature-verified permission preview, focus on Cancel and close-without-install behavior. The final wrapping/separate-button UI polish, native Install click, physical session-lock cancellation and execution/compatibility of installed extensions remain manual acceptance work; the final build and package lifecycle tests include that polish.

## Desktop shell, tabs, credentials and extensions candidate

This candidate replaces the shell layout with a Chromium-style toolbar and multiple windows, adds tab drag and drop, tab and workspace menus, workspace cleanup, cross-workspace moves, a field-anchored saved-account list, a toolbar-anchored save bubble and unpacked extension loading.

What ran:

- `broker_tests` gained `workspace_tab_cleanup` and `tab_workspace_move`. They check that cleanup cancels and refuses queued agent work, waits for an accepted gesture before asking the engine to close, claims fully closed control groups for the human, keeps a partly closed group's owner, and keeps the workspace and grants; and that a workspace move is refused for agent-owned, private, protected and same-workspace tabs, rejects non-web addresses, reopens before closing the original with `force: false`, and is allowed after disconnect or **Take ownership**. Two deliberate mutations of the broker (allowing agent-owned moves; closing before input drains) each failed the new checks.
- New `extension_store_tests` cover Chromium's ID alphabet against its published `"test"` vector, Manifest V2/invalid/nameless refusal, localized names, managed copies, out-of-extension page refusal, restart activation, deferred deletion and (on POSIX) link refusal.
- `native_input_policy_tests` now include Ctrl+PgUp/PgDn and Ctrl+1–9 as passive browser shortcuts.
- The broker and extension-store tests, with every other broker test except the Windows-only pipe and persistence-failure cases, were also built with GCC on Linux against a test-only security shim and passed. The native shell and Controls sources passed a Windows-target syntax check with MinGW headers and stub CEF headers.
- Windows CI [run 61](https://github.com/Xero-05/xenon-browser/actions/runs/37889230843) on `d8cfa4d` (workflow dispatch) built the browser and core with MSVC, passed all CTest native tests and the adapter/SDK and credential-script suites, packaged and verified the release, passed the isolated installer lifecycle tests and the extracted-package smoke test, and passed the seven live suites on the packaged DLL: `integration`, `batch-integration`, `visibility-integration`, `worker-lifecycle-integration`, `upload-entry-integration`, `persistence-integration` and `theme-integration`. Its first attempt failed in the unrelated downloader test `an interrupted response body is retried without retaining its partial bytes` (curl reported a too-slow transfer) before any build step; the re-run of the failed job passed. Those live suites drive tab creation, agent work and theme commands through the new shell, not its new human controls. [Run 62](https://github.com/Xero-05/xenon-browser/actions/runs/37889964169) on `602354e` repeated every step, including the seven live suites, with the read-only-copy fix and the new placeholder-format checks, and passed on its first attempt.

### Follow-up: shell fixes, script-driven saving, click offers and update checks

This follow-up rounds every page-frame corner, moves the sidebar scrollbar to the outer edge, keeps the workspace **+** in place (a double-click no longer collapses the group), scrolls new tabs into view, relabels rows in place on title changes, stops the toolbar shifting while a tab opens, adds script-driven and username-first Save/Update capture, re-offers saved accounts on each click of a login field, and adds background update checks with a blue Controls dot.

What ran, on Windows 11 with a local MSVC build:

- All 12 CTest suites passed. `login_capture_tests` adds deferred script-driven candidates (provenance kept for retries; readied only by the same form, context and document's field removal, same-document navigation or HTTPS navigation; cancelled by non-HTTPS navigation and new password edits) and username-first memory (physical input within two seconds, exact origin, ten-minute bound, kept across navigation, cleared on demand). `native_ui_tests` add the automatic-update preference round trip, failed-save rollback and a distinct notification color.
- 89 script tests passed, including 42 login-capture tests. New cases: an account step reported only for trusted Enter/change/button input on marked or email fields; a formless password step mirroring the inspected Google structure (hidden password decoy, repeated hidden email) deferred until the field leaves; a missing account delegated to native code; auxiliary, toggle, disabled and untrusted buttons ignored; `role=button` controls; script-cancelled versus browser-handled GET submissions; one immediate offer when Enter or a submit button precedes a conventional submission; retries superseding earlier attempts. Refusal cases now assert that no credential or privacy marker is emitted; GET and similar forms may emit the value-free edit marker.
- The adapter typecheck and branding-resource checks passed.
- Live suites on fixture DLL SHA-256 `d8561ba29c77f7ac95e3d762b0965894e19988705e77587e96a07ab5eaeea6bb` and production DLL SHA-256 `384510203f2607049f5af9d8a0cb14f99e4aa5271c6726bac4ae703b4ad69956`, unchanged throughout: `human-autofill-integration` 11/11 (the automatic picker appeared 187–188 ms after simulated held input released), `auth-integration` 11/11 and `integration` 17/17.
- Disposable production instances, driven only by window messages and rendered with `PrintWindow`: four rounded frame corners; a left-edge scrollbar with the selected tab joined to the frame; the 23rd new tab scrolled into view; a click then double-click on a workspace **+** opened two tabs and left the workspace expanded; a structural rebuild after closing a background tab kept the sidebar's scroll position (2 → 2). A build reporting 0.1.1 checked automatically after about 20 seconds and showed the blue Controls dot, the tooltip "Controls · Xenon 0.1.2 is available" and the Controls button "Update to 0.1.2".
- `WS_EX_LEFTSCROLLBAR` was confirmed to move a TreeView scrollbar to the left on English Windows 11.
- Passkeys: a probe page on `http://localhost` reported WebAuthn and a user-verifying platform authenticator as available, but a user-activated `navigator.credentials.create()` in a foreground Xenon tab failed with `NotAllowedError` after about 1.1 seconds and Windows Hello never appeared. The cause was later traced outside Xenon: the Windows passkey service on that test machine refused the request before showing any UI. It was not a tab-hosting or CEF limitation.

Not covered by this follow-up: a real Google (or other script-driven) sign-in and save; physical clicks on login fields, the workspace buttons and the hover-only **…**; the toolbar's on-screen repaint while a tab opens, which `PrintWindow` captures cannot show; and an installed update following the blue dot.

Not yet covered: the operator-driven shell, introduction, language, autofill and authentication live suites; physical mouse and keyboard checks of the toolbar, tab drag between windows, tear-off, the ownership chip, context menus and the account list; loading a real extension in CEF; reparenting a CEF browser window between Xenon windows under load; non-100% DPI and high contrast. The extension ID shown in Controls follows Chromium's path rule and has not been compared with a running Chromium.

## Web content theme candidate

The [web theme validation record](test-results/web-theme-validation.json) covers the report that **Theme: Light** left websites and blank pages dark while Windows used dark mode. Native-hosted Alloy pages follow Chromium's process-wide native theme, which `SetChromeColorScheme` does not change. Light and Dark now start CEF with `force-light-mode` or `force-dark-mode`. A full `build.ps1 -Test` run passed, and the new live suite passed four checks under Windows dark mode: Light, Dark, System, and a saved runtime switch applied after restart. A copy of the suite aimed at the unfixed binary failed on the reported case.

The suite has not yet run under Windows light mode. Runtime theme changes reach web content only after restart. The menu is driven by native messages, not physical input, and fixtures are synthetic loopback pages.

## 0.1.3 local release validation

The [0.1.3 validation record](test-results/release-0.1.3-validation.json) binds this local unsigned release to its rebuilt production and unshipped authentication-fixture DLLs. All 15 native groups, 111 adapter/fixed-script checks, 32 downloader/extractor/pointer-script checks, five branding checks and 47 isolated installer checks passed. Typechecking also passed. Dependency pins are unchanged.

Nineteen live runs passed 179 checks: the seven core production suites; graceful persistence recovery; the filter matrix with opt-in uncertain-click collection; the inspector in English/dark and Chinese/light; language and first-run drivers; protected login, native autofill and workspace removal using the unshipped fixture; and native shell tabs, input pause/dialog dismissal and startup recovery using Computer Use. Each suite used disposable synthetic state. The fixture driver coverage and synthesized Windows input are distinct from physical-device acceptance.

Earlier failed inspector runs exposed missing foreground/cursor prerequisites and real pointer messages interfering with synthetic capture-based divider drags. The driver now prepares its own window and aligns the cursor with those drags. The language harness expected a removed Go button; it now verifies the Extensions caption and reads the address edit through the correct cross-process text API. The autofill harness hit a Windows replacement lock; it now bounds retries of publishing the same request bytes without reissuing an accepted fill. Both inspector languages and the affected language/autofill suites passed on the unchanged DLLs after these harness fixes. Original failure reports remain separate and their hashes and failed check names appear in the record. One prematurely overlapping startup harness was stopped and then rerun serially; only the completed serial run is counted.

The restricted build stalled before compilation; the ordinary-user build and tests passed without changing product ACLs. High contrast, multi-monitor/DPI, assistive technology, real accounts/sites and sustained-load acceptance remain unverified. The [release notes](RELEASE-0.1.3.md) document the current CEF security-update lag. Packaging and extracted-archive smoke evidence are recorded after assembly; this section does not claim publication.

## 0.1.2 release validation

0.1.2 is a regular GitHub release, marked latest. It adds the web-content theme, native shell polish, MCP pipe recovery after failed connects and System32 ACL tool paths. Dependency pins are unchanged.

The [release validation record](test-results/release-0.1.2-validation.json) binds the four release files to the successful push CI run for merged commit `743bdf399b12308cd65718b890f71b641ad2f806`. That run built and tested 11 native groups, 101 adapter and fixed-script checks, 5 branding checks and 47 isolated installer checks, and verified all 1,299 ZIP manifest hashes. It then extracted the ZIP, passed the 6 package smoke checks, and ran the seven hosted live suites on the packaged DLL: 57 checks, with the theme suite in Windows light mode.

The same release DLL then ran locally in Windows dark mode. The theme (4), language (6) and first-run (6) driver suites passed. Computer Use drove the fixture windows' operator steps: shell tabs (8), Windows input pause with a website dialog (3) and workspace removal with the native confirmation (4). The unshipped AuthTest build of the same native sources passed protected sign-in (11), human autofill (11) and driver-mode workspace removal (4). Startup with a native Exit (7) passed only on the earlier candidate DLL.

Hands-on testing of that candidate found an unreleased regression: during human pauses, the selected tab kept the previous ownership color while the page frame faded to the new one. #13 repaints the tab tree for the whole fade. A pixel check on the release DLL saw the frame and tab match in every settled state. The record keeps the earlier failures:

- the first hosted fixture-readiness failures, fixed in #10 and #11;
- the language and introduction harness races, which also occur on 0.1.1 and were fixed in #12;
- one human-autofill attempt that stopped on a harness file-rename error and passed on rerun.

The [publication record](test-results/release-0.1.2-publication.json) binds the tag, regular/latest status and all four uploaded asset digests to the tested files. Native update code discovered 0.1.2 from a simulated 0.1.1 and downloaded and verified the 203,506,584-byte installer without launching it.

Computer Use sends synthesized OS input; it is not a person at the device. High contrast, non-100% DPI, real sites and real accounts remain unverified. The user's production installation was not replaced. Downloads remain unsigned: published hashes establish byte integrity, not publisher identity.

## 0.1.1 release validation

0.1.1 is a regular GitHub release, marked latest, with the browser functionality from alpha.19 and the complete matching CEF distribution updated to `154.0.34+g14c5a08` (Chromium `154.0.8037.98`). The October 5 stable-index check, publisher archive size/SHA-1 and recorded SHA-256 verified; the existing CEF license matches the distribution, and notices/pins remain aligned. Chromium now matches Chrome's current Windows stable version checked that day. Other dependency pins are unchanged.

The [release validation record](test-results/release-0.1.1-validation.json) binds the CI-produced Windows x64 Release files and all 11 native groups to the successful merged-source workflow. Local source checks passed 113 adapter/fixed-script checks and typechecking. The exact CI production files passed five branding checks and the production browser suites; 79 checks across nine serial synthetic suites also include three suites using the separately built, unshipped AuthTest DLL from the same source tree. The 47 isolated installer checks passed locally and in CI. Browsing, ownership/batches, hidden-content filtering, worker lifecycle, file/upload grants, protected authentication, native human autofill, workspace cleanup and two-launch persistence passed. The AuthTest-only driver activates its own marked-profile fixture window and gives its CEF view native focus; production focus/input protections are unchanged. Earlier foreground/popup failures and a later lost-focus attempt retain their original run IDs, binary hashes and fixed diagnostic codes, alongside the initial CMake regeneration and restricted-runner failures. The isolated final autofill rerun passed; desktop focus remains an external test-runner dependency.

The [package record](test-results/release-0.1.1-package-smoke.json) verifies all 1,295 extracted runtime hashes, six packaged-browser smoke checks and both archive/installer checksums. The four release files come from the successful CI artifact for merged commit `e7e6d071552f7a8e192318ffbee228ec99b6ea24`. The [publication record](test-results/release-0.1.1-publication.json) binds that source tag, CI artifact identity, GitHub-hosted asset upload, regular/latest release status and all four uploaded asset digests. Two slower local upload attempts timed out without registering assets; the local candidates and their validation remain preserved separately. Native update code discovered 0.1.1 from a simulated alpha.19 version and downloaded/verified the 203,486,544-byte installer without launching it. These post-packaging records are separate from the immutable archive documentation.

The regular-release designation does not establish physical UI or real-site acceptance, an independent security audit or production reliability certification. The full physical dialog/menu, high-contrast and multiple-DPI/monitor matrix remains unverified. Synthetic native authentication drivers are unshipped, and the user's production installation was not replaced. Downloads remain unsigned; published hashes establish byte integrity, not publisher identity.

## Alpha.19 release validation

The [alpha.19 validation record](test-results/alpha19-validation.json) binds the full Windows x64 Release build, all 11 native groups, 113 adapter/fixed-script checks, typechecking, five branding checks and 47 isolated installer checks to the rebuilt release binaries. Serial synthetic CEF suites passed 28 scenarios plus a binary-integrity check: modern/legacy batches with shared node/image evidence, exact private exports and native timings; granted-file pagination and scoped path filters; and protected authentication withholding, including combined inspection. The authentication fixture remains unshipped, and its diagnostic credential-canary scan passed.

The [original candidate record](test-results/browser-improvements-validation.json) retains earlier hashes and failed harness attempts. Physical UI and real-site acceptance remain unverified. Inspection validates a capture interval while page scripts/canvas continue running; native timings exclude model inference and establish no general speedup. Dependency pins are unchanged at the newest version-sorted Windows x64 stable CEF entry checked October 4, with the existing Chrome security-update lag. Package smoke and publication evidence are recorded separately after assembly.

The [alpha.19 package record](test-results/alpha19-package-smoke.json) verifies all 1,293 extracted runtime hashes and six packaged-browser smoke checks, including the scoped helper, combined inspection/control, native timing metadata and exact exported replies/PNG bytes. Its source tree matches merged commit `99814efd252c3783210e0cb4fe83ab73a3fdee55`. The [publication record](test-results/alpha19-publication.json) binds that commit, tag `v0.1.0-alpha.19`, successful Windows CI and all four uploaded assets. Native update code discovered alpha.19 from a simulated alpha.18 version and downloaded/verified the 203,470,281-byte installer without launching it. These post-packaging records are separate from the archive documentation.

## Browser improvements candidate

The [October 4 local validation record](test-results/browser-improvements-validation.json) covers paginated/filtered granted-file discovery, scoped handle diagnostics and client helpers, combined inspection, private opt-in evidence export, native timings and shorter worker instructions. The full Release build, all 11 native groups, adapter typechecking, 113 adapter/script checks and five branding checks passed. On the final production DLL, modern/legacy batch fixtures verified shared element/coordinate evidence, exact exported arguments/results, timing metadata and unchanged mutation deduplication. Eleven file/upload checks verified later-page discovery and scoped path filtering; eleven unshipped authentication-fixture checks also exercised inspection withholding across quarantine and handoff, with a clean credential-canary diagnostic scan.

The record retains the export startup failures caused by modern stdio discovery and the upload harness's outdated total-count assertion. The fixed exporter starts only on an accepted tool call; reports now retain individual runs. All live data was synthetic. Inspection validates a capture interval while scripts/canvas continue running. Physical human UI, real sites, installer/package/publication and a controlled model speed comparison were not exercised for this candidate.

## Alpha.18 release validation

Alpha.18 packages guarded simple-action batching. The [release validation record](test-results/alpha18-validation.json) binds the full Release build, all 11 native groups, adapter typechecking, 107 adapter/fixed-script checks, five branding checks, seven live batch/integrity checks and 47 isolated installer checks to the rebuilt release binaries. Modern and legacy clients verified the eight-action synthetic form sequence in one interaction call; separate calls required eight. Replaced/covered targets and navigation stopped the remaining steps.

Harness timings exclude model inference and do not establish a general speedup. Physical human input, real-site acceptance and unrelated live suites retain their earlier scope. Runtime pins remain at the newest Windows x64 stable CEF build checked on 2026-10-03, with the documented Chrome security-update lag. Package smoke and publication evidence are recorded separately after assembly.

Initial packaged-fixture attempts passed the existing three checks but withheld the new input labels. Explicit foreground/background colors and block label spacing fixed the synthetic page, and the harness count was updated to require all four checks. The corrected fixture passed on the unchanged production DLL; failed attempts remain in the release validation record.

The [alpha.18 package record](test-results/alpha18-package-smoke.json) verifies all 1,287 extracted runtime hashes and four packaged-browser smoke checks, including a three-step batch with verified website state. The [publication record](test-results/alpha18-publication.json) binds four uploaded assets and tag `v0.1.0-alpha.18` to source commit `ec29d0ba329eecd1222c5dba69aa339df810a0e2` and its successful Windows CI. Native update code discovered alpha.18 from a simulated alpha.17 version and downloaded/verified the 203,446,281-byte installer without launching it. Requested cleanup removed alpha.9–15 releases and their 26 assets, leaving alpha.16–18; all existing source tags were verified unchanged. These post-packaging records are separate from the archive documentation.

## Simple-action batching candidate

The 2026-10-03 local batching change passed the full Release build, all 11 native test groups, adapter typechecking, 107 adapter/fixed-script tests, five branding checks and seven live batch/integrity checks. Modern and legacy clients each filled three fields, selected an option, checked a box and clicked three buttons in one `xenon_batch` call; synthetic website values and counters were verified. Replaced/covered targets and navigation stopped the suffix. Native tests additionally cover handoff, human epochs, read-only policy, disconnect/retire, revocation/removal, withheld uncertainty and restart without replay or persisted input values. [Sanitized evidence](test-results/action-batching.json) binds the live results to the rebuilt production DLL and retains development failures.

The eight-action sequence used one interaction call versus eight separate calls. Recorded harness timings exclude model inference and do not establish a general model-speed improvement. Physical human input and real-site acceptance were not tested for this change; no package or release was published. To try the local build, restart `build/app/Release/Xenon.exe` and reconnect the rebuilt adapter so the host discovers `xenon_batch`.

## Alpha.17 release validation

Alpha.17 packages the autofill timing, credential-detection and named-submit-button fixes. The [release validation record](test-results/alpha17-validation.json) binds the full Release build, all 11 native groups, 93 adapter/fixed-script checks, five branding checks, 22 checks across the serial human-autofill and protected-login CEF suites, and 47 isolated installer checks to the rebuilt release binaries. The Google-shaped unowned username picker appeared 279 ms after simulated qualified held input released; unrelated text and hidden password decoys remained untouched. Both synthetic-profile diagnostic scans passed without credential canaries.

Real Google, CWL/PD Portal and Duo authentication, physical input hooks and human account-picker selection remain unverified. The native driver checks actual CEF fields and the real popup, while simulated provenance is distinct from physical UI acceptance. Earlier candidate evidence retains its original binary hashes. Package smoke and publication evidence are recorded separately after assembly. Runtime dependencies remain pinned to the newest Windows x64 stable CEF entry checked on 2026-10-03; Chrome's newer security update remains ahead of that available build, as documented in the release record.

The [alpha.17 package record](test-results/alpha17-package-smoke.json) verifies all 1,283 extracted runtime hashes and three packaged-browser smoke checks. The [publication record](test-results/alpha17-publication.json) binds four uploaded assets and tag `v0.1.0-alpha.17` to source commit `a1d71f516a57797e44944a43e26e7880e0a5bc00` and its successful Windows CI retry. The initial hosted attempt passed the build/tests and ZIP checks but failed when the pinned Inno Setup download returned HTTP 500 three times; that failure is retained, and the unchanged source passed on retry with all verification enforced. Native update code discovered alpha.17 from a simulated alpha.16 version and downloaded/verified the 203,431,584-byte installer without launching it. These post-packaging records are separate from the archive documentation.

## Alpha.16 release validation

Alpha.16 packages the brightness-hover reference fix. The [release validation record](test-results/alpha16-validation.json) binds the full Release build, native and adapter checks, hidden-content and hover regressions, synthetic file transfer and isolated installer lifecycle checks to the rebuilt alpha.16 binaries. The public Greenhouse check uses normal MCP observation, screenshots, scrolling and two successive Attach hovers in a disposable profile; it preserves all 33 observed nodes and both Attach references.

Real employer uploads, physical UI acceptance and the full theme/DPI/permission matrix remain unverified. Earlier tour, authentication, autofill and lifecycle evidence retains its original binary hashes. Package smoke and publication evidence are recorded separately after assembly; unsigned hashes establish byte integrity, not publisher identity.

The [alpha.16 package record](test-results/alpha16-package-smoke.json) verifies all 1,277 extracted runtime hashes and three packaged-browser smoke checks. The [publication record](test-results/alpha16-publication.json) binds the four uploaded assets and tag to source commit `4e3197c84315a18383f88093e86a66fbf76c67c8` and its successful Windows CI. Native update code discovered alpha.16 from a simulated alpha.15 version and downloaded/verified the 203,419,099-byte installer without launching it. These post-packaging records are separate from the archive documentation.

## Alpha.15 release validation

Alpha.15 includes the first-run tour, Simplified Chinese native interface and decorative-shadow observation fix. The [release validation record](test-results/alpha15-validation.json) binds a full Release build, 11 native groups, 90 adapter/fixed-script checks, 18 bootstrap checks, five branding checks, 47 isolated installer checks and 48 serial live checks to the alpha.15 binaries. The live suites cover introduction and language persistence, immediate renderer locale, duplicate startup and cancellation, Menu replay and documentation navigation, MCP isolation and handoff, rendered visibility and synthetic uploads.

The English language-selection and Chinese ready-page captures were inspected at default DPI with the System dark theme. Native message drivers do not establish physical-input acceptance. Light/high contrast, multi-monitor/DPI and the full permission/password UI matrix remain manual; authentication, autofill, crash persistence and lifecycle suites retain their earlier binary-bound evidence. An initial Windows PowerShell run misread Chinese assertions without a UTF-8 BOM; that failed report is retained, and both fixtures passed with Windows-compatible encoding on unchanged release binaries. Package and publication evidence are recorded separately after assembly.

The [alpha.15 package record](test-results/alpha15-package-smoke.json) verifies all 1,273 extracted runtime hashes and three packaged-browser smoke checks. The [publication record](test-results/alpha15-publication.json) binds four uploaded assets and tag `v0.1.0-alpha.15` to tested source commit `944852582a1a4139b06f386d88f0caea82c7c4b3` and its successful Windows CI. Native update code discovered alpha.15 from a simulated alpha.14 version and downloaded/verified its 203,412,329-byte installer without launching it. The immediate first discovery attempt found no eligible release; that report is retained, and later discovery/download passed on unchanged code. These records are separate from the documentation bundled in the archive.

## First-run introduction candidate

The [introduction validation record](test-results/introduction-validation.json) covers the language-first welcome flow, short browser tour, completion and skip persistence, normal restart, Menu replay and GitHub documentation navigation. The final source candidate passed a full build, all 11 native test targets, six introduction checks, six existing language checks and five bootstrap branding checks. English and Simplified Chinese fixture captures were inspected at the runner's default DPI with the System dark theme. Live checks use native window messages; physical input, multi-monitor/DPI, light-theme and high-contrast acceptance remain unverified.

## Greenhouse hover fix candidate

The [hover validation record](test-results/greenhouse-hover-fix.json) preserves the alpha.15 reproduction: two Attach hovers failed as stale and reduced 33 observed nodes to 12, then 3; moving the pointer away restored 33. The hover style also applies `brightness(0.6)`, which the filter previously rejected as potentially unbounded paint. Supported brightness darkening now preserves geometry and independently proves readable contrast; unknown filters and darkened frame embeddings remain conservative omissions. The existing post-pointer-move validation remains in place.

Native regressions cover percentages, chained filters, copied LayoutText styles, unreadable darkened text and unsupported effects. Synthetic live checks cover two successive hovers, hidden brightness text, stale references and approved file transfer through a hovered entry. The public-page check uses only observation, screenshots, scrolling and hovering in a disposable profile. It verifies both Attach references survive without filling fields or sending files to Greenhouse.

## Greenhouse observation fix candidate

The [candidate validation record](test-results/greenhouse-observation-fix.json) records the alpha.14 empty-observation reproduction and the corrected production browser. Decorative box shadows previously acted as potential page-wide blockers, including shadows below the viewport. The filter now bounds supported shadows conservatively and preserves fail-closed handling of uncertain paint and geometry.

The native regression covers distant, offset, blurred, spread, inset, multiple and clipped shadows, unsupported syntax/colors, transformed ancestry and fragmented layouts. Live visibility and upload fixtures include decorative shadows below the viewport; they verify usable rendered references, queries/waits, hidden-content exclusion, stale-reference denial and actual synthetic file transfer. The public Greenhouse check only observes, captures screenshots and scrolls in a fresh disposable profile; it verifies the three reported text inputs and both Attach references without filling the application or sending files. Coverage remains explicitly partial for other unsupported content and unproven frame embeddings.

## Alpha.14 release validation

Alpha.14 packages the tab, workspace, updater and date-field fixes below with updated version metadata. The [release validation record](test-results/alpha14-validation.json) binds fresh Release-build tests and serial automated live suites to the new production/AuthTest hashes. The local candidate record below retains its original hashes and physical observations; those UI checks were not repeated on the version-bumped binary. Post-packaging smoke and publication evidence are recorded separately after assembly.

The release build passed 11 native groups, adapter typechecking, 90 adapter/script checks, 18 bootstrap checks, five branding checks and 47 isolated installer checks. Eight serial live suites passed 69 checks, including the unchanged external benchmark's seven workflows. The original authentication probe failure is retained separately; a diagnostic rerun passed, then the harness replaced a fixed 20-ms delay with completed-loading/native-quarantine checks and passed again without retrying a login mutation. A restricted-runner pointer test attempt failed with `spawn EPERM`; the ordinary-user run passed all 14 checks.

The [alpha.14 package record](test-results/alpha14-package-smoke.json) verifies all 1,262 extracted runtime hashes and three packaged-browser smoke checks. Setup was assembled from that verified ZIP. The [publication record](test-results/alpha14-publication.json) binds the four uploaded assets and tag to source commit `fc9af84220209a9446d0699e09065bd353ac06e6` and its successful Windows CI. Native update code discovered alpha.14 from a simulated alpha.13 version and downloaded/verified the 203,186,564-byte installer without launching it. These post-packaging records are separate from the documentation bundled in the archive.

## Local tab, workspace and updater candidate

The local build adds visible tab close buttons and middle-click closure, keeps empty saved workspaces in the sidebar after restart, and opens a blank human tab when one is selected. **Install and exit** confirms normal shutdown before starting verified setup, fixing the conflict with the running-browser marker. The expanded external benchmark also exposed and now covers native date-field filling. This record describes the local alpha.13-version candidate before the alpha.14 version bump. Exact binary hashes and coverage are in the [local validation record](test-results/workspace-tabs-updater.json).

The full Release build passed all 11 native groups, adapter typechecking, 90 adapter/script checks and five branding checks. All 18 bootstrap and 47 isolated installer checks passed. Eleven serial live suites passed 87 checks, including the unchanged external fixture's health, comparison, calibration and four concurrent workflows. These are deterministic MCP compatibility tests, without a model speed or cost comparison. The runtime-copy fixture passed all three smoke checks with the bundled adapter, and all 1,257 manifest file hashes verified. Published release artifacts were untouched during that candidate validation.

Computer Use verified background X/middle-click closure, saved workspace reopening and visibility after normal restart, Ctrl+W, the new Updates menu/window, click and keyboard pauses, preserved human text, surfaced Controls and native dialog dismissal. Inspection used Dark/System resolved to Dark at 96 DPI. Computer Use was exited after each UI burst. The updater discovered and verified the public alpha.13 installer from a simulated alpha.12 version without running it. Its launch regression uses an inert unshipped process; no production upgrade was installed over the user's copy, and the available-update confirmation/install UI has not been exercised end to end. Light, high contrast, other DPI and the complete permission/password UI matrix remain manual work.

The first expanded benchmark's date failure and the new sidebar harness's incorrect URL assertion remain in separate ignored reports. The latter was corrected to read URLs through authorized page observations, with no authorization relaxation. An initial restricted-runner SDK test attempt failed to spawn subprocesses; the same tests passed under the ordinary interactive-user runner. These failed attempts have not been relabelled as passing runs.

## Alpha.13 navigation and visual fixes

Alpha.13 fixes native Enter/Go address submission and Find command routing, keeps address drafts during toolbar refresh, buffers frame painting, limits divider hover invalidation, adds gray background-tab feedback and workspace chevrons, and replaces dashed mouse-selection focus boxes with a keyboard-only solid mark. Validation and exact tested binary hashes are recorded in the [alpha.13 validation record](test-results/alpha13-validation.json).

The full Release build passed 11 native test groups, adapter typechecking, 84 adapter/production-script tests and five branding checks. All 18 bootstrap tests and 47 isolated installer checks passed. Ten live suites ran serially and passed 75 checks, including production Windows input with dialog dismissal and native menu Exit/restart. Computer Use also verified native Enter/Go navigation, Find Enter/Escape, retained form text after sidebar resizing and tab switching, gray background-tab feedback, workspace chevrons and Controls selection/table headings.

The production UI inspection used Dark mode at 96 DPI and Windows input injection. Static captures do not establish continuous flicker-free rendering. Light/System, high contrast, other DPI and the complete native pairing, permission and password-dialog matrix remain manual acceptance work.

The [alpha.13 package record](test-results/alpha13-package-smoke.json) verifies the ZIP checksum/inventory and all 1,257 extracted runtime file hashes. All three smoke checks passed using the packaged production browser and bundled Node adapter, including a verified synthetic page action. The installer was assembled from that verified ZIP. This post-packaging record is separate from the documentation bundled in the archive; its isolated installer coverage uses a synthetic payload and test identity.

The [publication record](test-results/alpha13-publication.json) binds the published four-asset inventory and tag to tested source commit `427c26762022cd939101aca5c15a14fb06d6f94a` and its successful Windows CI run. Remote asset sizes and SHA-256 digests matched the local verified ZIP, installer and checksum files. These unsigned checksums establish byte integrity, not publisher identity.

## Alpha.12 release preparation

The matching CEF distribution was updated to `154.0.33+ga03e714` (Chromium `154.0.8037.94`) after checking the Windows x64 stable index, verifying the publisher SHA-1 and recording SHA-256. Chrome's `154.0.8037.97/.98` security release is ahead of that available CEF build; no claim is made that all of its fixes are included. Runtime licenses/notices and dependency pins remain aligned.

Release preparation passed 11 native groups, adapter typechecking, 84 adapter/script tests, five branding checks, 65 live checks across eight serial suites and 47 isolated installer checks. The [release validation record](test-results/alpha12-validation.json) binds the rebuilt production and unshipped AuthTest binaries to those suites. It distinguishes automated broker/CEF coverage from earlier physical UI observations and retains the initial UI-test failure. That fixture now reads a fixed 32-bit offscreen bitmap after `GdiFlush`, preserving exact Light/Dark hover and mouse-leave assertions; `GetPixel` disagreed with its actual bitmap bytes. Browser painting code was unchanged.

The extracted release ZIP passed all three package smoke checks, including its bundled Node/MCP connection and a verified page action. All 1,254 manifest file hashes and both archive/installer checksums verified. See the [package record](test-results/alpha12-package-smoke.json). Post-packaging records are retained in the repository rather than retroactively inserted into the tested archive.

The [published alpha.12 prerelease](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.12) is bound to source commit `9411c88ee7911d49a1316fdaad86aa50f9925b37`, which passed Windows CI. All four uploaded asset digests match the verified local files. Native update code discovered alpha.12 from a simulated alpha.11 version and downloaded and verified the 203,163,154-byte installer in disposable staging without launching it. The first immediate post-publication check returned no eligible release; its result is retained separately from the successful retry. See the [publication record](test-results/alpha12-publication.json).

The full physical theme, high-contrast, multiple-DPI/monitor, permission, credential-dialog and menu matrix remains unverified on the release binary. Earlier startup/input/dialog and visual observations retain their original hashes; the runtime update does not turn those records into new physical acceptance coverage.

## Visual and controls overhaul

The local overhaul build passed all 10 native test groups, adapter typechecking,
84 adapter/production-script tests, five branding checks and 75 live checks across
10 serial suites. The live suites cover background tabs, handoff, pointer input,
visibility, uploads, protected authentication, native autofill, worker retirement,
workspace removal, forced-stop persistence and normal native Exit/restart.
The [sanitized overhaul evidence](test-results/visual-overhaul.json) binds each
suite to its tested production or unshipped AuthTest DLL hash.

Actual Windows UI input verified gray/teal/orange ownership borders, readable
status and dialog text, persistent native cursor parking, keyboard pauses and
native dialog dismissal on the final production build. Earlier visual inspection
also verified Light/Dark/System, workspace creation, renaming, filtering, separate
configuration windows and persistence; its distinct hash is retained in the
record. Native configuration and credential fixture checks are not physical
permission or account-picker approval coverage. Actual high contrast, multiple
DPI/monitor settings, assistive technology and the complete physical dialog/menu
matrix remain manual acceptance work.

The run found and fixed an animated-drag callback lifetime bug; retirement now
balances a started button and cancels queued input. Two test-only filesystem
races were also corrected without repeating native mutations. Original failed
reports remain separate in ignored local `out` files, identified in the evidence.

The subsequent [visual refinements](test-results/visual-polish.json) passed 11
native groups, 84 adapter/script checks and five branding checks, then 17
production live checks on the final DLL. A real Win32 regression checks hover
subclass attachment, distinct Light/Dark hover paint, list sizing and saved
sidebar settings. Native UI automation checked the final Dark shell at 96 DPI,
the reload icon, complete bottom curves, Controls hover/tooltip and the restored
sidebar width. Preliminary sidebar-drag and Controls-list observations are
labelled separately; their exact preliminary AuthTest hashes were not captured.
The final physical Light/System, high contrast, multiple DPI and full menu/dialog
matrix remain unverified. The first drag-result assertion failure is retained;
the harness now scrolls to the visible result after handoff without relaxing the
actual website outcome check.

## Repeatable checks

| Layer | Command | What it exercises |
| --- | --- | --- |
| Dependency bootstrap | `node --test tests/bootstrap-download-tests.mjs tests/bootstrap-extract-tests.mjs` | Actual curl/PowerShell against a loopback fixture: checked cache reuse/repair, transient and interrupted-transfer recovery, bounded timeouts, permanent failure and partial-file cleanup; small-archive extraction despite PATH shadowing. No browser launch or full acceptance suite is needed for a downloader-only change. |
| Native updater | `ctest --test-dir build -C Release -R '^updater_tests$' --output-on-failure` | Release/channel/version selection, exact repository and asset binding, redirect policy, digest/size verification and private staging with synthetic sources. An unshipped inert process verifies suspended preparation, marker release before start, cancellation, replay denial and staged-path binding. It installs nothing and reads no real profile. |
| Windows installer | `./scripts/bootstrap-installer.ps1`; `./tests/installer-tests.ps1` | Actual Inno install/upgrade/uninstall with a tiny synthetic payload, unique test-only identity/HKCU keys/shortcuts/mutexes and a disposable install folder. Running/locked files, invalid versions, downgrade rejection and preserved synthetic profile data. No production installation. |
| Native C++ | `ctest --test-dir build -C Release --output-on-failure` | Broker authorization, async queues, clean-boundary handoffs, popup groups, dispatch guards, durable deduplication, Windows named pipes, DPAPI/SQLite and pending-login confirmation, native login-capture state transitions, CSV import, file and folder capabilities. |
| Native UI regression | `ctest --test-dir build -C Release -R '^native_ui_tests$' --output-on-failure` | Hidden real Win32 controls verify case-insensitive subclass attachment, exact Light/Dark hover enter/leave paint using synchronized 32-bit bitmap bytes with reduced motion, timed hover fade-in/fade-out with motion enabled, continuous fade reversal, color mixing and symmetric easing, that an ownership color fade repaints the selected tab with the page frame, and DPI list height. Synthetic settings exercise legacy loading, atomic theme/width persistence, bounds and malformed input. This simulates messages and does not establish physical hover, resize or high-contrast acceptance. |
| Pointer targeting | `node --test tests/pointer-target-tests.mjs` | Exact production target script with deterministic randomness: visible unobscured interior, partial coverage fallback, fixed final revalidation, clipped/tiny targets, shadow hit tests, stale geometry and opacity changes. Native `pointer_motion_tests` covers bounded curves and visual-only pause/resume transitions; live retirement/handoff tests check release after actual pointer-down. |
| Form fill preparation | `node --test tests/form-fill-tests.mjs` | Exact production script with synthetic DOM: canonical dates, rejected values without changes, disabled/read-only/credential fields, focus-handler invalidation, detachment and unchanged text-selection semantics. The external benchmark matrix additionally checks actual Chrome date/form outcomes. |
| Adapter and credential scripts | `npm test` | Private pairing-file creation, image envelopes, fragmented/out-of-order pipe replies, uncertain outcomes, strict schemas, actual modern and legacy SDK stdio clients, plus the production login-monitor, human-autofill and passkey-gate scripts in a synthetic DOM. |
| Login monitor only | `node --test tests/login-capture-tests.mjs` | Production isolated-world script: supported forms including the public CWL form shape, rejection cases, trusted-event filtering, privacy-before-capture ordering, revealed-password identity, submitted-attempt semantics and MFA input exclusion. No native prompt is opened. |
| Human autofill script | `node --test tests/human-autofill-tests.mjs` | Exact production inspector/fill scripts: form/node binding, Google-shaped unowned phases, explicit username priority, phone/revealed fields, strict phases, actionability, preservation of human values, one-shot use, no submission and honest outcomes after page handlers reset or replace fields. No native prompt is opened. |
| Passkey gate | `node --test tests/passkey-guard-tests.mjs`; `ctest --test-dir build -C Release -R '^passkey_guard_tests$' --output-on-failure` | Exact production main-world wrapper against a synthetic `CredentialsContainer`: non-passkey pass-through, no browser call before the native decision, forwarding of the original receiver and options, refusal as `NotAllowedError`, settled notices, fixed mediation values, forged and repeated decisions, fail-closed missing binding and timeout, and resistance to later built-in tampering. The native target covers message parsing bounds, the human-controlled, gesture-window, agent-action and autofill rules, and pending-ceremony bounds and expiry; `broker_tests` covers `human_controls` and `passkeyNeedsHuman`. Neither starts a real WebAuthn ceremony or Windows prompt. |
| Certificate warning page | `node --test tests/certificate-warning-tests.mjs` | Exact production enhanced-protection style script against a synthetic document: it changes only Chromium's error document (websites, a look-alike path and `about:blank` are untouched), places the rule in the head or root element, and waits once for `DOMContentLoaded` when it runs before the document has a root. It does not cover Chromium's warning page in Xenon. |
| Protected login script | `node --test tests/protected-login-tests.mjs` | Exact production login script: named button/input submitters, username-first phases, fill-only use, ambiguity, unsafe form/button overrides, changed destinations and browser validation. No native prompt or real account is used. |
| Native human autofill | `node tests/human-autofill-integration.mjs` | Unshipped `XenonAuthTest` driver invokes the native path against generated HTTPS forms while real MCP clients observe: automatic popup timing after simulated qualified input releases, held-input suppression, preserved agent cooldown, no-grant fill, quarantine/handoff, exact origin, navigation/replacement/dismissal, phased and Google-shaped unowned filling, explicit username priority and lock cancellation. The driver checks the real native popup but does not exercise physical mouse hooks or click the account picker. |
| Native login policy | `ctest --test-dir build -C Release -R '^login_capture_tests$' --output-on-failure` | Production secret-free capture state: physical-input freshness, form/context/document binding, one-shot provenance, SSO origin retention, MFA input, cancellation, plus real DPAPI vault confirmation and destination-origin denial. |
| Production browser | `node tests/integration.mjs` | Actual CEF pages controlled through separate adapters and multiplexed workers, isolation, interactions, visual evidence, dialogs, downloads, live handoff and reconnect. |
| Simple-action batches | `node tests/batch-integration.mjs` | Production CEF with modern/legacy MCP clients, eight-action form/button sequences, deduplication, stopped suffixes for replaced/covered targets and navigation, and a separate-call timing comparison. Verifies synthetic website state; excludes model inference and physical input. Native broker tests cover handoff, human epochs, read-only policy, permission/client/workspace revocation, unknown outcomes and metadata-only restart without replay. |
| Hidden-content defense | `node tests/visibility-integration.mjs` | Adversarial invisible text and accessibility labels are withheld; visible controls remain usable, searches use the same filter, and page instructions remain untrusted data. Coordinates computed from screenshot pixels (`image pixel / scale`) must hit 12 px controls beside both native scrollbars, and a scrollbar pixel must be rejected without input. A right-to-left page must keep its main-frame scrollbar on the right: targets at both page edges are hit and a right scrollbar pixel is rejected. A far-corner control must stay verified and clickable by reference and by coordinates from inspection bounds, first at browser zoom 1 and again at 1.2, where its bounds must remain CSS pixels inside the smaller CSS viewport. Browser zoom scales Chromium's layout units the way Windows display scale does. The suite selects its agent tab in its own window, posts the native **Zoom in** command, waits for the resulting human pause to end, and resets the zoom before the remaining checks. Chromium switches such as `--force-device-scale-factor` are ignored, so on a 100% monitor the zoom pass is the only coverage of scaled layout units; it does not change the window's display scale or native UI scaling. |
| Pinch zoom | `node tests/pinch-zoom-integration.mjs` | Unshipped `XenonAuthTest` (configure CMake with `-DXENON_BUILD_AUTH_FIXTURE=ON`) with a compile-time-gated driver that asks Chromium for a synthetic 2x touchpad pinch on a marked disposable `.cache/pinch-zoom-integration-*` profile. Coordinates from a screenshot taken before the pinch are rejected as stale, pinched screenshot pixels hit their targets, a click, drag or scrollbar pixel outside the magnified view is rejected without input, and unpinching restores the full page area. While pinched, observation reports only the magnified view as its `viewport` and gives no reference to content outside it; `xenon_inspect` succeeds and its node bounds address the view; a reference observed before a pinch that moved its control out of view is rejected without input; and agent pointer movement and a 3x-pinched scroll send no page pointer events outside the view, which a page listener would report. The same driver records a person's passive hover position, as the mouse hook would, without sending input; the next agent movement must start there. With the agent tab selected, the agent pointer overlay window must sit at the pinched input point, and the Elements inspector, driven by posted window messages, must pick the element under a pinched window point and draw its highlight around that element in a `PrintWindow` capture. The driver stands in for a person's touchscreen or precision-touchpad gesture; physical touch input is not exercised. |
| Worker lifecycle | `node tests/worker-lifecycle-integration.mjs` | More than 16 sequential workers reuse one live tab; retirement preserves page state, reconnect honors concurrent capacity, and queued work is not replayed. |
| Workspace removal | `node tests/workspace-removal-integration.mjs` | Actual CEF and MCP with the unshipped `XenonAuthTest` native driver: workspace/popup closure, access and pending-read revocation, preservation of another live workspace and shared files, then tombstoned-profile cleanup and survivor cookie/storage checks after restart. This mode does not exercise the human confirmation dialog. |
| Human workspace removal | `node tests/workspace-removal-integration.mjs --manual` | Same generated-profile checks using production `Xenon.exe`. At READY, select only the named fixture workspace in that test instance's Controls, click **Remove workspace**, and approve the confirmation. No other profile or workspace is authorized by this harness. |
| Protected sign-in | `node tests/auth-integration.mjs` | Exact-origin grants, actual synthetic HTTPS login including a flow requiring a named submitter, username-first forms, quarantine across four clients/handoffs, password/OTP input and reveal, persistent approved-folder uploads. |
| Profile persistence | `node tests/persistence-integration.mjs` | Two browser launches using the same generated profile; persistent cookies and local storage survive forced termination after a 35-second flush interval, workspace isolation remains, and writes are not replayed. |
| Blank startup | `node tests/startup-integration.mjs` | At the READY marker, use the named fixture window's native menu **Exit**. Checks one human-owned blank tab on restart, no old page loads or mutation replays, retained persistent/session cookies and local storage, fresh-workspace isolation, and retention after closing one workspace's last tab while another stays open. Seeds legacy Chromium restore preferences in the generated profile only. |
| Sidebar tab lifecycle | `node tests/shell-tabs-integration.mjs` | Follow each READY native UI step in the disposable production-browser instance: background X and middle-click closure, surviving document/input state, opening empty saved workspaces, normal Exit/restart, retained names and blank startup, Ctrl+W and final native Exit. This requires actual operator/Computer Use input; the harness does not drive Win32 controls. |
| Browser language | `./tests/ui-language-integration.ps1` | Disposable production profile: native message driver verifies English → 简体中文 → English persistence, translated real window captions, configuration/panel controls and normal shutdown between launches. This is native driver coverage, not physical menu navigation or visual/DPI acceptance. `native_ui_tests` checks legacy fallback, failed saves, Unicode, Chinese fonts, file filters and unchanged website titles/user names. |
| Address suggestions | `ctest --test-dir build -C Release -R '^(omnibox\|browser_data)_tests$' --output-on-failure`; `./tests/omnibox-integration.ps1` | `omnibox_tests` ranks synthetic metadata: unchanged typed-address and Ctrl+Enter interpretation, host-root and path completion (implied scheme and `www.`, a visited port and HTTP scheme, none for partial schemes or trailing spaces), every-term matching with case-folded titles, word-start single characters, frequency/recency/bookmark order, deduplication, row limits, bidi/control stripping and bold ranges. `browser_data_tests` adds workspace and private-workspace isolation, index refresh after a new bookmark, and encrypted history removal that survives restart while keeping bookmarks. The live suite launches production `Xenon.exe` in Light and Dark with disposable profiles and loopback pages, shown off-screen without activation. A native message driver checks that externally set addresses still open as typed, inline completion and its selection, the list, Up/Down row text, Escape, Backspace without re-completion, and clicking a history row. It relies on Xenon's own thread focus rather than physical input or the desktop foreground; that background focus was once not acquired within its retry window, and the suite now reports which window held focus. It does not cover Shift+Delete (which needs a held Shift key), high contrast, other DPI or assistive technology. |
| Site information | `ctest --test-dir build -C Release -R '^site_info_tests$' --output-on-failure`; `./tests/site-info-integration.ps1` | `site_info_tests` covers origins (user information dropped, IPv6 hosts), every toolbar connection state including minor certificate notices, high-bit errors, mixed and insecure content, loopback HTTP and protected sign-in, cookie scope against look-alike and sibling domains, the setting and value tables, storage sizes, certificate hex encoding, the load errors that show Chromium's certificate warning and the Chrome Help Center addresses its **Learn more** link is redirected from. The live suite uses a loopback page that sets two synthetic cookies in production `Xenon.exe`, shown off-screen without activation, then the unshipped `XenonAuthTest` build with its pinned test certificate. The HTTP part runs in Dark. A native message driver checks the bubble's rows and connection view, that a Site settings combo box renders dark (mean luminance of its rendered pixels), a Pop-ups change in Site settings appearing in the bubble and resetting, deleting one selected cookie and then all site data (confirmed in the dialog), verified by the cookies the website receives next, the certificate-error warning and summary, and the Windows certificate viewer opening. It does not cover physical input, the certificate warning page itself, its **Learn more** redirect or its hidden enhanced-protection suggestion in a running browser, a valid public certificate (checked once by hand against `https://example.com`), camera, location or notification behavior after a change, storage clearing beyond cookies, high contrast, other DPI or assistive technology. |
| Browser essentials | `ctest --test-dir build -C Release -R '^browser_import_tests$' --output-on-failure`; `./tests/browser-essentials-integration.ps1` | `browser_import_tests` covers the app-link scheme denial list and escaping, launch-link filtering and `--single-argument` parsing, bookmark HTML and Chromium `Bookmarks` parsing, name cleaning, folders, per-visit history deletion, import deduplication and persistence, and reading a synthetic profile's `History` while another handle holds it open. The live script drives one disposable production instance through a native message driver with every window parked beyond the monitors: startup and running-browser link hand-off, a site icon in the tab list, the bookmark manager, history search and deletion, an app-link prompt reaching a temporary HKCU test scheme's recorder as one escaped argument, a refused `ms-msdt:` link, the screen-share prompt and its denial, Clear browsing data and Settings. It never enters full screen or allows screen sharing. |
| Screenshot safety and recovery | `ctest --test-dir build -C Release -R '^(screenshot_guard_tests\|broker_tests)$' --output-on-failure`; `node tests/screenshot-integration.mjs` | The production scan component has deterministic callback tests for transient/persistent timeouts, full rescans, credential precedence, malformed documents, frame replacement/churn, navigation, human input, authority loss and capture continuation checks. Production CEF/MCP fixtures verify safe top-level/same-origin/cross-origin pages, empty credential fields in top-level/shadow/frame content, and capture after a credential frame is replaced with safe content. Timeout injection is unit coverage, not a reproduced live website timeout. Uses generated profiles and local synthetic pages; does not inspect personal applications or claim physical UI coverage. |
| Agent gates | `node tests/agent-gates-integration.mjs` | Production CEF and MCP: an agent's own clicks on an app link and a screen-share button in its tab open no native prompt, and the page receives `NotAllowedError`. |
| Passkey gate in the browser | `node tests/passkey-integration.mjs` | Production CEF and MCP with loopback fixtures. Every probe calls `navigator.credentials.get` with an already-aborted signal, so Chromium answers `AbortError` when the gate forwards the request and the gate's `NotAllowedError` when it refuses; no authenticator or Windows prompt is used. An agent-created tab's page, same-origin frame, out-of-process frame and new blank frame are wrapped before their own script runs and refused, with no placeholder history entry and `passkeyNeedsHuman` set; an agent click is refused without an ownership change; navigation clears the notice; and the human tab, loaded through the address bar by a native message driver, forwards every probe. A physical click inside an agent tab is not covered. |
| Web content theme | `node tests/theme-integration.mjs` | Four normal launches of production CEF and MCP with one generated profile. Light and Dark set a loopback fixture's `prefers-color-scheme` report, its rendered canvas and a blank page regardless of Windows; System follows Windows. A native-message **Theme** command is saved, leaves this run's pages unchanged and applies after restart. The suite reads but never changes the Windows app mode, so run it once in each mode. This is native driver coverage, not physical menu input. |
| First-run introduction | `./tests/introduction-integration.ps1` | Disposable production profiles: language selection before CEF, duplicate startup, cancel/retry, translated tour with Previous/Next, immediate first-launch CEF locale, durable completion/skip, normal blank restart, Menu replay and documentation URL navigation. Captures target only the synthetic introduction window. Native message driver coverage does not establish physical input or multi-monitor/DPI acceptance. `native_ui_tests` also checks legacy profiles, interrupted preference writes and completion-save rollback. |
| Windows input | `node tests/native-input-smoke.mjs` | Append the requested public text to the draft in the named fixture window. Verifies automatic creator ownership, Windows input pause without an ownership change, dispatch/read denial while paused, and fresh-evidence recovery with the human edit preserved. |
| Human script dialog | `node tests/native-input-smoke.mjs --dialog` | After editing, click **Confirm synthetic logout**, verify Controls reappears with that tab selected, wait at least three seconds and click **Dismiss**. Checks the pause lasts while the dialog awaits an answer, then ends without changing document or owner. Controls surfacing is a manual observation. |
| Upload entries | `node tests/upload-entry-integration.mjs` | Direct inputs, visible custom buttons and StaticText Device entries opening hidden inputs, cross-origin frame uploads, invalid grants, stale labels/text, missing choosers, multiple inputs, disabled/directory controls and cross-frame delegation denial. `node --test tests/upload-entry-tests.mjs tests/pointer-target-tests.mjs` also checks the fixed entry classifier and exact text hit areas. Add `--benchmark <path-to-browser-benchmark.cjs>` to copy and exercise the external synthetic health fixture without modifying its source or original evidence. |
| External benchmark workflows | `node tests/upload-entry-integration.mjs --benchmark <path-to-browser-benchmark.cjs> --benchmark-matrix` | Unchanged external health, comparison, calibration and four concurrent workflows through real MCP. Verifies actual date/form state, exact approved upload, delayed review, distinct concurrent identities, trusted activation and untouched final submission. Deterministic adapter input tests browser compatibility; it does not compare model speed, cost or task success. The external fixture is supplied separately and is never committed. |
| Release archive | `./scripts/verify-package.ps1 -Zip <release.zip>` | ZIP checksum, per-file SHA-256 manifest, required runtime files, exclusion of test binaries and private state. |
| Bootstrap branding | `node tests/branding-resources.mjs` | Reads PE files without loading them, compares code and non-branding resource payloads with the pinned bootstrap, and checks exact Xenon icon/version resources. See the narrowly permitted resource-layout effects in [branding](BRANDING.md). |
| Extracted release | `node tests/package-smoke.mjs <release-directory>` | Verifies packaged branding against the pinned bootstrap, then starts the packaged browser and bundled Node adapter with a generated profile and verifies a real page action. |

The protected sign-in suite uses the separately compiled `XenonAuthTest` executable with one pinned loopback certificate. The production `Xenon.exe` has no certificate exception. Tests use generated profiles and public canary credentials, never an existing Chrome, Edge, Firefox or Xenon profile.

The removal driver's implementation and command-line switch are also compiled only into `XenonAuthTest`, which is excluded from release archives. The driver requires a marked disposable removal profile under `.cache`; it calls the native broker API rather than adding an MCP removal capability. The removal harness generates its own profile, pairing, private pipe, credentials and files, and stops only its spawned browser process tree. Its report identifies the binary hash and whether removal used the test driver or actual human confirmation. A test-driver pass must not be reported as a human UI pass.

Run desktop suites one at a time and avoid physical input into fixture windows except at the requested manual steps. Physical page input pauses agent work and invalidates evidence. Alpha.5/alpha.6 used immediate takeover; those historical tests do not establish the newer pause behavior. The earlier concurrent interruption and reported alpha.6 benchmark ownership change did not record enough native input detail to establish their exact causes.

For an additional ordinary-shutdown check, run the persistence suite with `--graceful` and close the remaining native browser window when prompted. The default forced-stop test does not guarantee retention of data written immediately before a crash.

## Acceptance evidence

The 2026-10-03 local autofill/login regression passed the full native build, 11 native tests, 14 adapter tests, 32 login-monitor tests, 24 human-autofill script tests, 10 protected-login script tests, six form-fill tests, typecheck and five branding checks. The final DLL passed 11 protected-login live checks and 10 human-autofill live checks. The native test driver verified a real automatic account picker 277 ms after simulated qualified input released, while the agent cooldown and ownership remained intact. A named-submit-button fixture authenticated through exactly one POST and reached its success page. Both live suites checked unchanged fixture DLL hashes and found no credential canaries in diagnostic logs. Physical mouse hooks, selecting an account through the picker, real CWL/PD Portal and Duo remain unverified. The installed browser was not changed. See [binary-bound evidence and the initial harness count failure](test-results/autofill-latency-login-submitter.json); earlier acceptance records retain their original scope and hashes.

Machine-readable reports in [test-results](test-results/) record each live check, timestamps, dependency pins and application DLL hashes. Native and adapter JUnit records are preserved there as well. Run the commands above to reproduce results on another Windows machine.

The alpha.11 destination-folder fix passed 47 isolated installer checks, covering custom paths, remembered updates, existing default-folder registration, shortcuts, protected/occupied/unreadable destinations, lock handling and retained synthetic profile data. A Computer Use check confirmed the editable destination, Browse dialog and chosen folder on the Ready page, then cancelled before installation. Five bootstrap branding checks passed after the version rebuild. See [alpha.11 installer evidence](test-results/alpha11-installer-folder.json), the [packaged-browser smoke](test-results/alpha11-package-smoke.json), and the [publication record](test-results/alpha11-publication.json). Windows CI passed, and the native updater downloaded and verified the public installer from a simulated alpha.10 version without launching setup. Unrelated live browser acceptance suites were not repeated for this fix.

The alpha.10 installer/update change used focused validation: six native updater test groups, 24 isolated Inno lifecycle checks, a real GitHub metadata request, a native Updates window inspection in a fresh profile, normal test-browser shutdown, and the five bootstrap branding checks. The UI showed the current version and disabled download/install when no newer eligible installer existed. These checks do not claim that the available-update UI or a future production upgrade has been exercised end to end. See [alpha.10 focused evidence](test-results/alpha10-installer-updater.json), the [packaged-browser smoke](test-results/alpha10-package-smoke.json), and the [publication record](test-results/alpha10-publication.json). After publication, the production native update path discovered alpha.10 from a simulated alpha.9 version and downloaded and verified the 202,924,385-byte installer in disposable staging without launching it. Windows CI passed for the release commit. Unrelated browser acceptance suites were not repeated locally for this change; their historical release-specific results remain below.

The alpha.9 branding build passed all 76 live browser checks, seven native tests,
14 adapter tests, 32 login-monitor tests, 24 human-autofill script tests and five
bootstrap-resource integrity checks. The alpha.9 production DLL is
`51cca524e7c4948ce064877c1d040594f63e3dd970b4ce4bfbb07a9934eeb25b`;
the separate authentication fixture DLL is
`fa0bb616a664ac3b6e8735ceb912951bab4c31ecc05da3e3883ff0aa7668f6a3`.
The Windows visual check confirmed the teal frame, Xenon window titles, small
logo, navy Controls header and readable grouped controls. A physical page edit,
restyled dialog dismissal, native Exit and blank restart also passed. Alpha.9 UI
observations and their narrower coverage are in `test-results/native-ui-manual.json`;
resource evidence is in `test-results/branding.json`. Computer Use was closed.

The alpha.8 local run passed 76 live browser checks, seven native tests, 14 adapter tests, 32 login-monitor tests and 24 human-autofill script tests. The ordinary Windows page-edit/dialog and native Exit/startup checks were repeated on that release's production DLL. Controls showed the new **Fill saved account…** button without visible overlap; this inspection did not exercise account selection or filling through the picker. Those alpha.8 UI observations are archived in `test-results/alpha8-native-ui-manual.json`. Computer Use was closed afterward.

The alpha.7 upload suite passed ten live checks, including the same external synthetic health-fixture source that previously failed `upload_transfer`. The source was copied unchanged into a new disposable run and its digest rechecked afterward; original benchmark evidence was left untouched. Alpha.6 reproduced that exact graded failure, while alpha.7 transferred the approved fixture file through the visible button and hidden input and passed the health flow. Final submission remained untouched in both runs. The baseline record is explicitly labelled `baseline_failure_reproduction`; its `passed` means the failure was reproduced, not that alpha.6 passed upload acceptance. Harness timings are deterministic test-driver measurements, not model-speed comparisons or reusable browser certification.

The parallel baseline runs four independently paired clients, each using three live tabs, then four logical workers through one adapter with three tabs each. Separate-client runs negotiate both modern and legacy MCP protocols. The tests verify page mutations rather than treating successful protocol replies as proof of website success.

The idle live-handoff fixture records document time origin, draft text, active element, selection, scroll position, a running WebSocket and website events. It asserts unchanged document/form state, continued streaming, no added navigation/input/focus/visibility/storage events, and fresh-evidence enforcement for the recipient. The broker unit test independently asserts that ownership commit produces an empty browser-command trace.

Additional live cases cover existing popup-group transfer, handoff after pointer-down and before release, stale replaced nodes, overlays, same-origin and cross-origin frames, shadow DOM, screenshot-bound canvas actions, nonmodal JavaScript dialogs, scoped download metadata, workspace cookies, pending waits and disconnect/reconnect without replay. Native tests also cover late popups, queued callback invalidation, revocation during reads, human input/IME boundary events, interrupted dispatch, unknown outcomes and recovery after the pipe reaches connection capacity.

The historical alpha.6 Windows UI check verified the **Remove workspace** button and its confirmation, including the workspace name, cleanup/preservation warning and default **No** choice. Selecting **No** preserved the workspace and tabs. This record is retained in `test-results/alpha6-native-ui-manual.json`; deletion was not approved through the UI.

The historical alpha.7 Windows input check separately exercised a native page click and typed edit. Both paused agent dispatch without changing owner or generation; idle alone could not authorize a stale action, and a new observation let the same owner continue with the human draft preserved. A human-triggered confirmation held the pause beyond two seconds, surfaced Controls with the original agent labelled **paused for you**, and was dismissed natively without changing document or owner. Its UI observations are retained in `test-results/alpha7-native-ui-manual.json`; current input reports identify their own tested DLL. Computer Use was closed after the check. These synthetic checks do not establish PD Portal logout or automatic password-prompt acceptance.

Worker lifecycle tests perform 1,025 native retirement/replacement cycles and 260 disconnect cycles to exercise bounded handle retention. The live fixture replaces the worker 20 times around one unchanged tab, verifies continued timers and WebSocket traffic with no added website events, and retires a worker during a finite drag while cancelling queued input. A configured four-worker limit is shared across modern and legacy clients; retirement and disconnection free slots, and resume checks available capacity. These checks establish reusable admission and safe retirement, not performance at the maximum configurable limit of 256 workers.

The hidden-content fixture includes public attack canaries in invisible DOM text, accessibility metadata, clipped and covered text, shadow trees and hidden frame content. Positive controls verify that visible labels, icons and same-origin/cross-site frames remain usable. Fixture-driven changes preserve the document and observation token while changing a visible label or hiding a containing frame; stale actions must fail before page input, then work with fresh evidence after restoration. Visible malicious prose is intentionally retained with no instruction authority. These checks test filtering and provenance, not whether any particular model resists every prompt injection.

Authentication tests assert that credentials are accepted by the fixture server while no username/password canary occurs in supported MCP outputs; protected screenshots return no image. GET forms and readonly credential fields are rejected before any value, input event or submission, for both fill-only and submission requests. Password and one-time-code reveal cases simulate DOM input events with public fixture values. They do not substitute for manual testing of every keyboard, IME, passkey, MFA or site-specific login flow.

The automatic human-login monitor has 42 passing JavaScript tests that execute the exact production script with a deterministic synthetic DOM and clock. They exercise supported submissions, script-driven and username-first sign-in, rejected forms, trusted-event filtering, reveal handling, ordinary MFA input exclusion and the distinction between a submitted attempt and a successful login. Anonymous GET-only inspection of UBC's public CWL login form found the supported shape: same-origin POST, one text username, one password and a hidden CSRF field, without autocomplete attributes. A synthetic fixture reproduces that shape without real credentials or token values. This inspection did not authenticate to CWL, Duo or PD Portal.

The native `login_capture_tests` target passes using the production capture-policy class and a generated DPAPI/SQLite vault. It checks physical-input recency and one-shot form/context/document binding; an HTTPS login→MFA→application sequence that preserves the source origin; MFA input retention; cancellation on a new qualifying password edit or non-HTTPS navigation; explicit native confirmation even for a potentially incorrect attempt; denial of the saved secret at destination origins; expiry and lock clearing. The extended vault tests also pass: no save before confirmation, Save/Update behavior, duplicate suppression, stale conflicts, replay rejection, the 64-candidate bound and absence of pending records after reopening. These tests simulate native provenance inputs; they do not prove the Windows input hook or a human typing, submitting and clicking the prompt end to end. That combined flow has not yet been automated or recorded as passing manual acceptance.

The human-autofill renderer suite passes 31 tests using the exact production scripts in a synthetic DOM. It covers conventional/CWL-shaped forms, explicit username/current-password phases, Google-shaped unowned inputs with `username webauthn` and a hidden password decoy, unique explicit username priority, phone usernames, revealed current-password fields, top-level HTTPS and same-origin POST checks, ambiguity, disabled/read-only/OTP/new-password fields, overlays, bound node/document/form association, unchanged human values, no overwrite or submission, consumed offers, fixed metadata and results, and page handlers that reset, detach, reparent or replace fields. A replacement password input never receives the saved password, and a changed result is not reported as successfully filled. These tests do not exercise the Windows picker or establish real-site compatibility.

The alpha.8 native human-autofill live suite passed all nine checks on `XenonAuthTest` DLL SHA-256 `5d0fa005e548a343be01116f524c5d558de80bffcc5d2f94c54072e228c33d4a`, unchanged throughout the run. It verified no-grant, fill-only use of a CWL-shaped form; protection from four MCP clients across handoff; wrong-origin, stale-navigation, replaced-form and dismissed-offer denial; separate username/password phases; and lock cancellation, including lock/unlock while an accepted fill waited for a guarded action callback to drain. That cancellation fixture uses a bounded wait to hold the action barrier; it does not independently exercise a real pointer/key release. Its scan of 107 generated diagnostic log files found no credential canary. This uses a marked generated profile and an unshipped native test driver, not a person clicking **Fill**. Actual physical-focus suggestions, account selection in the native picker and PD Portal/CWL/Duo human autofill have not been manually verified. An ordinary Windows page-input smoke pass is separate evidence and must not be described as credential-UI acceptance.

The 2026-10-03 credential-detection follow-up passed all 11 native autofill checks on fixture DLL SHA-256 `cb1c663ff79dadfb6ac76356d853740846a7170d01255fcc64d703e2d6c87995`, unchanged throughout both live suites. Its Google-shaped unowned username picker appeared 284 ms after simulated held input released; it filled only the username, left the hidden password decoy empty and performed no submission. Separate unowned password and explicitly marked username-with-auxiliary-text cases filled only their original credential nodes; replacing unowned nodes invalidated an existing offer. All 11 protected-login checks also passed on the same fixture binary. Both generated-profile diagnostic scans passed. [Sanitized evidence](test-results/credential-detection.json) records the matching production DLL and actual checks. Real Google authentication, physical input hooks and human account-picker selection were not tested. See the [source comparison and remaining gaps](CREDENTIAL_DETECTION.md).

The alpha.6 workspace-removal harness passed all four live checks in two consecutive runs on the same unshipped `XenonAuthTest` DLL: a removed workspace and its related popup become inaccessible; pending reads and stale actions are denied; another live workspace retains its document and remains usable; restart removes the tombstoned profile while preserving the other workspace's cookies/local storage, shared vault, completed download and approved source file. The report binds those results to the tested DLL. These runs used the native test driver, not a human approving deletion. Accepting the native confirmation and completing removal through that UI still requires a `--manual` run.

An earlier alpha.6 removal run failed the profile-absence assertion after restart; its exact cause was not established. A later alpha.7 failure exposed a startup defect: Chromium selected the tombstoned workspace from `profile.last_used` and opened it before Xenon's cleanup. The cleanup preflight correctly reported Windows sharing violation 32 and deleted nothing. Pinned Chromium/CEF source confirms that CEF adopts this already-opened initial profile even when the configured global cache path is `Default`. The browser process now pins `--profile-directory=Default`; cleanup's bounded, handle-checked deletion policy is unchanged. The regression seeds both `profile.last_used` and `profile.last_active_profiles` with the removed workspace while the disposable browser is stopped, then retains the strict profile-absence assertion. The rebuilt alpha.7 `XenonAuthTest` passed all four live checks in run `workspace-removal-1790894928882-8944a0ad`, including removal of that seeded profile and retention of the survivor's cookies/local storage, vault, download and approved source. Its DLL SHA-256 remained `25103e5f2658e55ae56ebc98eb33d098021f84be7d242b36f4137dc59e21d480` throughout the run. This used the unshipped native driver, not human confirmation. The failed binary hash and sanitized diagnosis remain preserved in [the pre-fix removal record](test-results/alpha7-removal-before-startup-profile-fix.json).

Cleanup can still defer externally locked or refused entries. The fixture verifies a completed download's retention, not cancellation of an ongoing transfer; pending input, late popup/creation callbacks and unusual locked-file scenarios need their own coverage. Unit checks must not be described as completed human UI or real-site coverage.

## Native Save/Update manual acceptance matrix

Every row below remains manual acceptance work. Use disposable HTTPS accounts and profiles; record the application hash, Windows version and outcome before claiming coverage.

| Scenario | Expected result |
| --- | --- |
| Human types and submits a conventional same-origin HTTPS POST login, with or without a full navigation | A source-owned Save prompt appears after input settles, without focus activation. It identifies a submitted attempt; the vault changes only after the native Save click. |
| Supported password submission redirects through an HTTPS MFA provider to another HTTPS application; the human clicks or types the MFA step | The offer remains bound to the original credential-entry origin, not the MFA provider or destination. MFA input is not captured as a password. |
| Incorrect password, visible login error or a still-visible credential form after a qualifying submission | The offer may still appear. It must not claim success; the human can dismiss it and a new qualifying password edit cancels it. |
| Existing account with changed password, unchanged password, or a concurrent native account edit | Update requires confirmation; unchanged credentials show no prompt; an older candidate cannot overwrite the concurrent edit. |
| Not now, close prompt, close source tab, non-HTTPS navigation or ten-minute expiry | The offer is discarded without saving. |
| HTTP, browser-handled GET or cross-origin submission, private mode, iframe, ambiguous fields or new-password/OTP form | No automatic offer for unsupported capture forms; explicit account entry/import remains available. |
| Script-driven sign-in such as Google: type the account, press **Next**, type the password, press **Enter** or **Next** | No offer while the password box stays; an offer for the original origin, naming the typed (or repeated) account, after the page navigates or removes the box. A wrong password that leaves the box visible produces no offer. |
| Reveal, **Forgot password?**, back or another-way buttons on a password step | Not treated as sign-in attempts. |
| Password-only re-authentication on a site with saved accounts and no known account | No offer. |
| Agent-filled or synthetic website submission without qualifying physical password input | No automatic Save/Update offer; pending credentials remain inaccessible through MCP. |
| Windows lock/unlock, new qualifying password editing or agent mutation while an offer is pending | Pending credentials are canceled; unlock cannot reuse pre-lock capture provenance. Ordinary MFA input alone does not cancel the candidate. |
| Held mouse/key gesture, minimized/background source window and concurrent work in another tab | Prompt display waits for the source to be eligible, does not steal focus and does not block unrelated agents. |
| Confirm Save/Update, then inspect the account from an ungranted paired client | Saving creates no individual grant; the client cannot use the account until separate native approval. |
| Save a new account with **Sensitive** ticked, then list accounts from a client allowed all Default passwords | The account is absent until it is individually granted. |
| Save a new account as Default, then list accounts from a client allowed all Default passwords; then choose **Make sensitive** | The account is listed for its exact origin without another grant; after **Make sensitive** it is absent from the next listing and a queued login is cancelled. |

## Practical limits and remaining manual coverage

- The native Xenon shell supplies bookmarks, history, find, zoom, printing/PDF, downloads and site permission controls. UI inspection and functional acceptance must be recorded separately from native driver coverage.
- Session recovery reloads saved nonprivate URLs with human ownership. It does not recover an interrupted renderer's form state or replay actions. Live handoff preserves the existing renderer and is a separate operation.
- Private browsing is a native human workspace with memory-only context storage. Downloads remain ordinary files. Agents' default workspaces are persistent.
- Native Save/Update prompts cover a bounded class of human-typed top-level HTTPS sign-in: conventional POST submissions, and script-driven or username-first sign-in once the page moves on, subject to the manual acceptance matrix above. They do not prove authentication succeeded. Supported submissions can continue through HTTPS SSO redirects and human MFA without losing the original-origin candidate. Explicit account entry and CSV import remain available. Unusual forms, cross-origin embedded login, MFA, passkeys and CAPTCHAs need the human.
- The passkey gate has script, native policy, broker and live-browser coverage with already-aborted requests. No included suite runs a WebAuthn ceremony through Windows Hello, a security key or a phone, or checks that a physical click inside an agent tab is forwarded and holds the agent pause. A popup's first document and passkey requests made through other browser APIs are not covered by the gate.
- Native human autofill is fill-only and supports a conventional two-field form or explicitly marked username/current-password phases, including unowned inputs. Verify physical-focus suggestions, the manual Controls path, native account selection, wrong-origin denial, changed-value preservation and lock cancellation with disposable accounts before claiming human UI coverage. It neither advances MFA nor grants agent account access.
- Broker input barriers have deterministic native coverage with simulated human-input events. The current Windows smoke test exercises a physical edit and optional human dialog with retained ownership. The broader physical-device/IME, multi-monitor/DPI and assistive-technology matrix remains manual release work. The live drag regression covers one finite pointer gesture, not every native HTML drag-and-drop integration.
- Long-duration load, renderer crashes under stress, network failures, ongoing media/download transfer during every handoff variation and all Windows versions require further testing. Passing the included fixtures does not establish universal website compatibility.
- GitHub Actions builds, tests and verifies the runtime ZIP on Windows for pull requests, pushes to `main` and manual dispatches. A separate Ubuntu job runs the adapter typecheck and `npm test` for faster feedback; it does not replace the Windows build. Check the [actual workflow runs](https://github.com/Xero-05/xenon-browser/actions/workflows/windows.yml) for remote CI status; the records here describe local validation. After uploading the release artifact, the Windows job extracts the ZIP and runs `tests/package-smoke.mjs` on it, then runs the non-interactive production-browser suites (integration, batch, visibility, worker lifecycle, upload entry, persistence, theme and passkey) serially against the packaged DLL and uploads their reports as `xenon-live-results`. The hosted theme suite covers only the runner's Windows app mode. Operator-driven suites and the AuthTest suites, which need the unshipped fixture build, still run locally; hosted CI does not claim those results or physical input.

The alpha deliberately reports stale targets, blocked sensitive captures, unsupported controls and unknown outcomes instead of silently guessing or repeating a submission. Review these limits and the [security boundary](SECURITY.md) before publication or sensitive real-account use.
