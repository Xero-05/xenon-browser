# Browser branding

Xenon keeps the matching CEF sandbox bootstrap and Chrome browser controls. Branding adds the Xenon application icon, version metadata, a teal Chrome theme accent and native window styling. Startup still opens one human-owned `about:blank` tab; it does not load a custom start page, reconnect old pages or replay actions.

The artwork lives in `assets/branding/`. `xenon-mark.png` is the original mark; `xenon-icon.png` and the multi-size `xenon-icon.ico` are application assets. `native/resources/xenon.rc.in` embeds icon group **101** and the release's Windows version metadata in the application DLL. Native windows load shared icon handles from that DLL. Shared handles must not be destroyed by callers.

CEF's `SetChromeColorScheme` applies the accent per request context while preserving its light or dark mode. Page titles remain intact; the native Chrome window title uses the ` — Xenon` suffix, or `Xenon Browser` for a generic empty title. This does not rename website tabs, change website content or expose browser chrome through MCP.

Native Controls uses a navy logo header, white section cards, teal headings and
buttons, and Segoe UI typography. The native saved-account and file-permission
windows share that styling. Controls retain their existing action identifiers and
handlers. The packaged CEF chrome still supplies the tab strip, address bar,
menus and settings; inherited labels such as **About Chromium** remain. This
release does not fork or replace Chromium's entire browser UI.

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
