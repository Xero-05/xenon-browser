# Release notes

## 0.1.0-alpha.17

The native saved-account picker can appear about 250 ms after physical input releases and leaves an eligible login field focused. It no longer waits for the full two-second agent cooldown; that pause still applies to agent input. Held input, dialogs, navigation, new activity and vault lock continue to block or invalidate offers.

Human-confirmed autofill now recognizes explicitly marked username/current-password inputs outside a form, including a Google-shaped `username webauthn` step with a hidden password decoy. A unique marked username takes precedence over unrelated text fields. Phone usernames and revealed passwords marked `current-password` are supported. Filling remains bound to the exact HTTPS origin and original fields, requires native account selection and never submits or grants agent account access.

Protected agent sign-in now submits with the single visible native login button when present, preserving its name/value for flows such as CWL's `_eventId_proceed`. Ambiguous submitters and unsafe form/button overrides are refused. Bound fields and destinations are rechecked after page handlers, and browser validation can leave credentials filled with `submitted: false`. Requested submission remains distinct from successful authentication.

The [Chromium/Firefox comparison](CREDENTIAL_DETECTION.md) documents remaining gaps. Real Google, CWL/PD Portal and Duo sign-in and physical account-picker selection remain unverified; automatic Save/Update capture for username-first and JavaScript-only flows remains unsupported. The Windows x64 installer and portable ZIP remain unsigned prerelease artifacts. Runtime dependencies are unchanged, with the existing documented CEF security-update lag. Earlier candidate evidence retains its original binary hashes; [release validation](TESTING.md#alpha17-release-validation) records the final version's checks.

## 0.1.0-alpha.16

Ordinary `brightness(0.6)` hover styling now preserves rendered control references, including Greenhouse Resume/CV and Cover Letter **Attach** buttons. Previously each hover failed as stale and removed unrelated visible controls; both now retain their references and the page's 33 observed nodes. Supported brightness darkening uses conservative text contrast bounds, while unsupported effects and darkened frame embeddings remain withheld. Hidden-content exclusion and post-pointer-move target validation remain enforced.

The Windows x64 installer and portable ZIP remain unsigned prerelease artifacts. Runtime dependencies are unchanged. The [original reproduction and candidate checks](test-results/greenhouse-hover-fix.json) retain their alpha.15 binary hashes; [alpha.16 validation](TESTING.md#alpha16-release-validation) records checks on the rebuilt release. Synthetic uploads verify the approved-file path; a real employer upload remains unverified.

The [alpha.16 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.16) publishes both formats and their checksums. All 1,277 runtime hashes and three extracted-package smoke checks passed. [Package evidence](test-results/alpha16-package-smoke.json) and [publication evidence](test-results/alpha16-publication.json) bind the uploaded bytes, successful Windows CI and source tag; the native updater downloaded and verified the installer without running it.

## 0.1.0-alpha.15

New installations start with **English / 简体中文** selection and a short native tour of browsing, tabs, workspaces and agent controls. The chosen language applies before the first browser tab opens. The tour supports Previous, Next and Skip, remembers completion, and links to the GitHub getting-started guide. **Menu → Quick tour** reopens it; **Menu → GitHub documentation** opens a new Personal tab.

The native shell, Controls, configuration, saved-account prompts, file permissions and update UI now support Simplified Chinese. Later language changes use **Menu → Language / 语言** and apply after restart. Website content and user-provided names retain their original text. Blank-tab labels use the selected language throughout creation and navigation.

This release also includes the rendered-observation fix for decorative box shadows, expanded English and Chinese READMEs, and the original logo artwork. The observation filter preserves hidden-content exclusion and stale-target checks; see the [candidate evidence](test-results/greenhouse-observation-fix.json).

The Windows x64 installer and portable ZIP remain unsigned prerelease artifacts. Runtime dependencies are unchanged. See [installation](GETTING_STARTED.md) and the [alpha.15 validation scope](TESTING.md#alpha15-release-validation).

The [alpha.15 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.15) publishes both formats and their checksums. All 1,273 runtime hashes and three extracted-package smoke checks passed; [package evidence](test-results/alpha15-package-smoke.json) and [publication evidence](test-results/alpha15-publication.json) bind the uploaded bytes, successful Windows CI and source tag. Native update code discovered and verified the public installer without running it. These records were produced after packaging and are separate from the archive documentation.

## 0.1.0-alpha.14

Tabs now have visible **X** buttons and middle-click closure, alongside **Ctrl+W** and sidebar **Delete**. Closing the last tab preserves its workspace. Empty saved workspaces remain in the sidebar after restart; selecting one opens a blank human-owned tab without automatically reloading old pages.

**Menu → Check for updates** opens native Updates. **Install and exit** confirms the human decision, prepares the verified installer suspended, closes Xenon normally and releases its running marker before starting setup. Other running instances still block installation. Profiles, pairing state, fixed-repository checks, exact asset matching and mandatory size/SHA-256 verification remain protected.

The expanded MCP benchmark fixture exposed native date-field filling that reported dispatch without changing the field. Canonical date values are now validated before applying the native setter and script-generated input/change events; ordinary text keeps the existing input path. These are deterministic MCP compatibility checks, not model speed or cost comparisons.

Exact release-build hashes and automated coverage are recorded in [alpha.14 validation](test-results/alpha14-validation.json). Earlier physical UI coverage remains bound to the distinct [local candidate hashes](test-results/workspace-tabs-updater.json); the version bump does not turn those observations into physical checks of the release binary. No production upgrade was installed over the user copy.

The Windows x64 installer and portable ZIP remain unsigned. See [signing preparation](SIGNING.md) for the public-trust setup and packaging changes required for a future signed release.

The [alpha.14 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.14) publishes both runtime formats and matching checksums. All 1,262 runtime hashes and three smoke checks passed; [package verification](test-results/alpha14-package-smoke.json) and [publication verification](test-results/alpha14-publication.json) retain exact assets, tested source and successful Windows CI. Native update code also discovered and verified the public installer without running it. These records were produced after packaging and are not retroactively inserted into the archive.

To upgrade alpha.13 or earlier, download the installer, close all Xenon instances and stop their MCP adapters before running setup. The corrected **Install and exit** flow becomes available after alpha.14 is installed; it cannot alter the older installed updater code.

## 0.1.0-alpha.13

Address and Find command keys now reach their native handlers before Windows dialog translation. Toolbar refresh preserves an address draft when focus moves to Go. Frame painting is buffered, divider hover invalidates only its grip, background tab hover uses gray, and collapsed workspace groups use a chevron. Dashed focus boxes are replaced by a small keyboard-only mark in browser tabs and Controls.

Validation passed 11 native groups, adapter typechecking, 84 adapter/production-script tests, 18 bootstrap tests, five branding checks, 47 isolated installer checks and 75 checks across ten serial live suites. Production UI checks exercised Enter/Go navigation, Find, sidebar resizing, retained form text, workspace chevrons and neutral selections. Exact tested hashes and remaining manual coverage are recorded in [alpha.13 validation](test-results/alpha13-validation.json).

The ZIP passed checksum/inventory verification for all 1,257 runtime files, and all three extracted-package smoke checks passed with the bundled Node adapter. The installer was assembled from that ZIP. Asset hashes and the post-packaging scope are recorded in [alpha.13 package verification](test-results/alpha13-package-smoke.json).

The [alpha.13 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.13) publishes the unsigned Windows x64 installer and portable ZIP with matching checksums. The [publication record](test-results/alpha13-publication.json) binds the uploaded assets, successful Windows CI and exact tested source commit. Post-packaging records are separate from the documentation bundled in the archive.

## 0.1.0-alpha.12

This release replaces the visible Chromium shell with a native Xenon interface while retaining the pinned sandbox bootstrap, isolated workspace profiles and broker boundaries. Download the unsigned installer or portable ZIP from the [alpha.12 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.12), with its matching checksum. See [installation and pairing](GETTING_STARTED.md).

- Grouped vertical tabs, connected selected-tab outlines, rounded icon buttons, hover labels and gray feedback, and saved System/Light/Dark themes. The sidebar supports dragging, keyboard width controls and reset. Dark mode uses charcoal surfaces; Controls lists use neutral gray selections.
- Teal selected-page borders indicate an available agent owner, orange indicates its human-input pause, and gray indicates human or unavailable ownership. Background paused/unowned tabs have no outline. Background page hosts keep rendering, and agent-created tabs do not steal human selection or focus.
- Controls separates Clients, Workspaces and Passwords. Pairing requests have Approve/Deny actions. Named workspace creation opens a blank human-owned tab; separate configuration windows manage renaming, allowed clients, permissions and resources. Selecting a workspace filters a labelled tabs table.
- New paired clients start read-only, with automatic workspace creation disabled and quotas of four workers and four automatic workspaces. Workspace grants narrow the client policy. Automatic workspaces inherit permitted files/folders and exact-HTTPS-origin accounts live; revocation removes inherited access. Native checks cover dispatch, callbacks, late results and indirect downloads. Versioned atomic state migration preserves existing identities, profiles and grants; legacy clients retain their previous capabilities and historical workspaces are exempt from creator quotas.
- Compact native menus provide navigation, find, zoom, bookmarks, history, downloads, printing/PDF, site permissions and third-party notices. Per-workspace encrypted bookmarks/history migrate non-destructively; private records remain memory-only and new protected-authentication visits are omitted.
- A persistent native agent cursor uses bounded smooth movement and revalidated element interiors. Screenshot coordinates stay exact. Human input cancels continuation, authority is checked throughout movement, and held input is balanced safely. Handoff and visual cursor pause/resume add no website input or move the Windows mouse.
- The full matching CEF distribution is updated to `154.0.33+ga03e714` (Chromium `154.0.8037.94`), the newest Windows x64 stable CEF entry checked during release preparation. Its publisher SHA-1 and locally recorded SHA-256 were verified. Chrome's newer `154.0.8037.97/.98` security update is ahead of that available CEF build; this release does not claim to include all of its fixes. See [CEF maintenance](BUILD.md#cef-security-maintenance).

The updated-runtime release build passed 11 native groups, adapter typechecking, 84 adapter/script tests, five branding checks, 65 live checks across eight serial suites and 47 isolated installer checks. See [release validation](test-results/alpha12-validation.json) for exact binary hashes and the retained test-fixture failure. Packaging and publication results are recorded separately in [test scope](TESTING.md).

The earlier refinement build passed 17 production live checks, while the preceding overhaul passed 75 live checks across 10 suites on its separately recorded binaries. [Historical validation](TESTING.md#visual-and-controls-overhaul) retains those hashes and failed-run history. Physical checks of the earlier Dark shell covered hover, icons, frame corners and saved width at 96 DPI; the full physical theme, high-contrast, multiple-DPI/monitor and menu/dialog matrix remains unverified on the release binary.

## 0.1.0-alpha.11

The Windows installer now offers a **program folder chooser** on a fresh installation. Choose an empty, writable folder on a local fixed drive, or keep the default `%LOCALAPPDATA%\Programs\Xenon Browser`. Download the unsigned installer and its SHA-256 file from the [alpha.11 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.11). Portable ZIPs remain available.

Updates reuse the existing installation folder. To relocate an installed copy, including alpha.10, close Xenon and stop its MCP adapters, uninstall through Windows **Installed apps**, then reinstall into the new folder. Uninstall preserves browser data and pairing files; choosing a program folder does not move the separate browser data directory. Update your MCP host's Node and adapter paths after relocation. See [installation and pairing](GETTING_STARTED.md) for examples.

The native update checks, explicit installation decision, unsigned-release limitations and profile-preservation policy are unchanged. See [test evidence and scope](TESTING.md) for validation records.

## 0.1.0-alpha.10

This Windows x64 release adds an **unsigned per-user installer** and native update controls. Download the installer and its SHA-256 file from the [alpha.10 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.10), or use the portable ZIP. Follow [Get started](GETTING_STARTED.md) for checksum verification, installation and MCP pairing.

Setup installs to `%LOCALAPPDATA%\Programs\Xenon Browser` and creates a Start menu shortcut. Browser profiles, saved accounts and pairing configuration remain separate from the application files; uninstall retains them. Close Xenon and stop its MCP adapters before an upgrade. Setup does not force-close your work and rejects downgrades.

**Xenon Controls → Check for updates** checks the fixed GitHub release repository on demand. Downloading and opening setup are separate human actions. Xenon verifies the installer's size and SHA-256 before launch; the alpha remains unsigned, so this trusts GitHub and the release maintainers rather than an Authenticode publisher. Updating a portable copy installs the new version into the standard per-user location; it does not overwrite the portable folder.

Focused validation covered native updater boundaries, isolated installer lifecycle checks, the native Updates window, bootstrap branding and an extracted-package smoke test. The available-update UI and a future production upgrade have not been exercised end to end. Unrelated browser acceptance suites were not repeated for this change; see [test evidence and scope](TESTING.md) for the release-specific records and limits.

## 0.1.0-alpha.9

First public Windows x64 alpha of Xenon Browser. Download the **unsigned** browser ZIP and its SHA-256 file from the [GitHub release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.9). Extract the entire archive; no installer or automatic updater is included.

### Included

- Sandboxed Chromium/CEF browser with persistent isolated workspaces and native Xenon controls.
- External stdio MCP clients, reusable workers, automatic creator ownership and concurrent work across tabs.
- Permission-only handoff on the same live tab, fresh-evidence requirements and temporary pauses for human page input.
- Rendered viewport observations, screenshots, guarded page actions, dialogs and permitted file transfers.
- Native DPAPI-encrypted saved-account vault, CSV import, supported-form Save/Update and human fill prompts, and account grants for agents.
- Blank startup, explicit session restoration, workspace removal, and Xenon branding based on the supplied logo.
- Setup, user and agent guides, contributor instructions, Windows CI and retained upstream license notices.

The public package refresh adds documentation and attribution to the locally tested alpha.9. Browser, CEF, Node and adapter executable payloads are unchanged. The earlier local archive is not the public artifact; use the checksum attached to the public release.

### Validation and limits

Local alpha.9 validation passed 76 live browser checks, seven native tests, 14 adapter tests, 32 login-monitor tests, 24 human-autofill script tests and five bootstrap-resource checks. The release archive has a per-file SHA-256 manifest and is checked with an extracted-package smoke test. See [test evidence and scope](TESTING.md) and [Windows CI runs](https://github.com/Xero-05/xenon-browser/actions/workflows/windows.yml).

The pinned CEF build was verified against the upstream Windows stable index during publication preparation. Updates remain manual; a past stable check does not establish future security currency.

This is experimental software. Real-site authentication compatibility and the full physical credential-picker workflow are not universally verified. MFA, passkeys, CAPTCHA and unusual forms require human handling. Hidden-text filtering cannot make a model immune to prompt injection. There is no independent security audit, proprietary DRM guarantee, broad extension guarantee, cloud sync or built-in model. Some inherited Chromium menus remain. See [security boundaries](SECURITY.md).

Follow [Get started](GETTING_STARTED.md) for installation and MCP pairing, [the user guide](USER_GUIDE.md) for daily controls, and [the agent guide](AGENT_GUIDE.md) for a copyable host instruction template.
