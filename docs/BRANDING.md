# Browser branding

Xenon keeps the matching CEF sandbox bootstrap and embeds windowed Alloy page hosts in its native Windows shell. The shell supplies grouped vertical tabs, navigation, compact menus and ownership borders. Startup opens one human-owned `about:blank` tab; it does not reconnect old pages or replay actions.

The artwork lives in `assets/branding/`. `xenon-mark.png` is the original mark; `xenon-icon.png` and the multi-size `xenon-icon.ico` are application assets. `native/resources/xenon.rc.in` embeds icon group **101** and the release's Windows version metadata in the application DLL. Native windows load shared icon handles from that DLL. Shared handles must not be destroyed by callers.

CEF's `SetChromeColorScheme` follows the saved System, Light or Dark choice for each request context. The native shell uses monochrome surfaces, Segoe UI, neutral controls and teal/orange/gray ownership borders with readable status text. Rounded buttons, icons and outlines use antialiased native vectors at the window's DPI. Toolbar icons retain native accessibility names and show hover labels. The selected sidebar tab connects to the page outline; Controls uses the same connected section layout and gray list selections. The shell title uses the ` — Xenon` suffix while retaining the selected website's title. Ownership border and cursor changes do not modify website content or add website input.

Controls is divided into Clients, Workspaces and Passwords. Workspace and client
configuration opens in separate native windows. Saved-account, file, update and
website-dialog surfaces share the native palette and DPI-aware typography, with
Windows high-contrast colors when enabled. The visible Chromium tab strip,
address bar, menus and settings are replaced by functioning Xenon controls.
About and Third-party Notices retain access to the bundled engine licenses and
credits; third-party resource contents remain intact.

Choose **Menu → Language / 语言 → English** or **简体中文** to change the
browser's interface language. Save unfinished website work, exit Xenon normally,
and open it again to apply the choice to both Xenon's native controls and CEF's
built-in UI. English remains the default for existing installations.
The preference is stored alongside the theme and sidebar width in
ui-settings.json. Chinese controls use Microsoft YaHei UI. Translations cover
the shell, Controls, configuration, saved-account prompts, file permissions,
downloads, site-permission prompts and updates. Website content, user names,
MCP results and bundled legal notices retain their original text; unfamiliar
backend error details remain available after a translated failure message.
Windows file pickers and system dialog buttons follow the installed Windows
display language.

CMake first copies the pinned CEF `bootstrap.exe` to the build output as `Xenon.exe`. The build-only `xenon_brand_bootstrap` helper then copies only icon and version resources from the Xenon DLL into that copy. It updates the version resource's `.dll` filename suffix to `.exe` and its fixed file type to an application. The original file under `third_party/cef/Release/` must remain untouched. The helper is not packaged, and this process does not sign the executable or replace the CEF sandbox startup code.

Run the resource integrity check after building:

```powershell
node tests/branding-resources.mjs
node tests/branding-resources.mjs --release 'C:\path\to\extracted-release-directory'
```

The check reads PE files as data; it never loads a DLL or launches the browser. It compares the branded executable with the pinned bootstrap, requiring identical executable and other non-resource section bytes, matching executable identity and memory policy, and preservation of non-branding resource leaves. Only icon (`RT_ICON`, `RT_GROUP_ICON`) and version (`RT_VERSION`) resources may change. The CEF manifest, revocation-list resource, dialogs and string resources remain byte-identical. Windows may grow the resource section and move the adjacent, nonexecuting `.reloc` section: the check permits only the corresponding aligned resource-growth movement, requires unchanged relocation bytes, size and flags, and proves that relocation directory 5 still points to that same payload. All other section locations and non-resource directories remain unchanged. Both the EXE and DLL must contain group 101 with image bytes matching the approved ICO asset and the expected Xenon version metadata. Results, section details and file hashes are written to `out/branding-results.json`.

`tests/package-smoke.mjs` performs this same check against the extracted release before launching its disposable test browser. It binds the checked EXE and DLL to their release-manifest hashes, then verifies the packaged MCP connection and a real page action. The normal archive verifier still checks every packaged file hash and rejects development binaries or private state.

On the tested Windows build host, `EndUpdateResource` also changes preserved resource-directory CodePage metadata from unspecified (`0`) to Western Windows (`1252`). The verifier permits and records this exact normalization only for unchanged Unicode dialog/string resources, the self-encoded XML manifest and the raw CEF revocation-list data. Their IDs, language IDs and complete payload bytes must remain identical. Any other encoding-metadata change fails verification.

These checks establish resource integrity and metadata, not visual quality or security certification. Native icon appearance, DPI scaling, title readability, light/dark contrast and Controls layout require a separate visual check. Windows taskbar or Explorer icon caches may temporarily retain an older icon. Keep the blank-startup, native-input, credential and page-interaction regressions when changing window styling; branding must not change ownership, focus policy, page visibility or credential protection.
