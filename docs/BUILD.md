# Building and packaging Xenon

## Windows x64 prerequisites

- Windows 10/11 x64 with an interactive desktop for browser tests.
- Visual Studio 2022 Build Tools, Desktop development with C++, MSVC v143, Windows 10/11 SDK, and the CMake component.
- Node.js 24 LTS, npm, Git, PowerShell 7 or Windows PowerShell 5.1, and Windows `tar.exe` and `curl.exe`.
- [7-Zip](https://www.7-zip.org/) installed in `Program Files/7-Zip` for CEF archive decompression.
- Several GB of free disk space for CEF, generated build files and release staging.

`scripts/bootstrap.ps1` downloads CEF, SQLite, nlohmann/json and the redistributable Node runtime. Archive/header SHA-256 values are pinned in `dependencies.lock.json`. npm dependencies and their integrity digests are pinned in `package-lock.json`. Dependencies are downloaded into ignored `third_party/` subdirectories; never commit browser profiles, password CSVs, vaults or pairing configuration.

Downloads use HTTPS with certificate validation, a 30-second connection limit, a 45-second low-speed limit (below 1 KiB/s), and a five-minute limit per attempt. Transient failures receive at most three attempts with bounded backoff. A unique `.part` file becomes a usable cache entry only after its pinned SHA-256 matches. Existing cached downloads are verified on every bootstrap; a corrupt cached file is discarded and fetched again. HTTP errors such as 404, certificate verification errors and digest mismatches fail without repeated retries. Interrupted partial files are never reused or cached.

The console reports the dependency, attempt, host, elapsed time and received byte count every ten seconds, then the HTTP/transfer result and hash verification. CEF extraction uses 7-Zip to decompress bzip2 into an on-disk tar archive, then Windows' `System32/tar.exe` to unpack it. The two stages share a five-minute limit, print tool versions, and report file counts/bytes every 30 seconds. This avoids the observed hosted Windows tar stall on the large bzip2 archive, binary PowerShell pipelines, and accidentally selecting Git's GNU tar, which can interpret drive-letter paths as remote locations. Extraction has completion markers tied to the verified archive digest. Interrupted extraction is retried instead of treating the first extracted file as success. TLS checks and pinned digests are never bypassed. `curl.exe` ignores user curl configuration; bootstrap does not accept arbitrary mirror URLs or credentials.

Windows CI caches only downloaded archives and the JSON header, keyed by the full dependency lockfile, operating system and architecture. It never caches extracted dependency trees or partial transfers, and bootstrap rechecks cached hashes before use. The workflow runs `bootstrap.ps1 -DownloadOnly`, saves the verified downloads immediately, and then runs extraction separately. The cache therefore survives a later extraction or build failure. A cache service failure falls back to ordinary downloads. Fetching has a 20-minute step budget and extraction an eight-minute step budget inside the existing 45-minute job budget. On a persistently corrupt shared cache, remove that Actions cache entry; immutable exact-key caches cannot be repaired in place.

For download/CI changes, run the focused regression harness rather than the browser acceptance suites:

```powershell
node --test tests/bootstrap-download-tests.mjs tests/bootstrap-extract-tests.mjs
```

These use actual PowerShell and curl against a generated loopback HTTP fixture, plus a small archive extracted while a different executable shadows `tar.exe` on PATH. They cover verification, cache repair, retries, interrupted transfers, bounded timeouts and selecting the correct Windows extractor. Production dependency URLs remain HTTPS-only. Use `./scripts/bootstrap.ps1` to verify the currently cached pinned dependencies; the full CI run checks a fresh Windows checkout.

```powershell
./scripts/bootstrap.ps1
npm.cmd ci
./scripts/build.ps1 -Test
```

The CMake generator is `Visual Studio 17 2022`, architecture x64, Release configuration. Native code uses C++20 and the static MSVC runtime. Sandbox support is mandatory. The matching CEF distribution supplies `bootstrap.exe`; the build copies it to `Xenon.exe` beside the application `Xenon.dll`, which exports `RunWinMain`. A build-only tool replaces the copy's icon and version resources. The original bootstrap, executable code, entry point and manifest remain unchanged; `node tests/branding-resources.mjs` verifies that boundary. Do not substitute a bootstrap from another CEF release. See [branding](BRANDING.md) for the artwork and native theme implementation.

The application is in `build/app/Release/`. Native tests are registered with CTest; TypeScript and SDK tests use `npm test`. `./scripts/build.ps1 -CoreOnly -Test` builds the broker/vault/file tests without CEF. Reconfigure without `-CoreOnly` to restore the full browser build.

The default browser-wide concurrent worker limit is 16 across all paired clients. New clients also default to four concurrent workers and four automatic workspaces; native Controls configures these client quotas and the permission to create workspaces. Both limits apply. Migrated clients retain their previous capabilities and global worker limit until configured. Set a different browser-wide limit when starting the browser, for example:

```powershell
.\Xenon.exe --max-concurrent-workers=32
```

The supported range is 1–256. This is a native startup setting, unavailable to MCP clients; it takes effect on the next browser launch. It limits connected workers, not the number created over the browser's lifetime. Retirement and disconnection free connected capacity without closing tabs or deleting profiles. Resuming a disconnected worker requires an available slot. Higher values are resource budgets, not tested performance guarantees; the separate 64-tab limit still applies. Hosts should reuse workers or pass an existing granted `workspaceId` when creating replacements to avoid creating unnecessary persistent profiles.

Parallel work uses a fixed startup policy: `disable-backgrounding-occluded-windows` keeps covered Alloy page hosts rendering and `disable-background-timer-throttling` keeps background page timers running. These internal settings follow [Chrome tooling guidance](https://github.com/GoogleChrome/chrome-launcher/blob/main/docs/chrome-flags-for-tools.md); they never change during a handoff. They can increase CPU/GPU use and power consumption compared with an ordinary browser's background throttling. External Chromium command-line settings remain disabled.

Covered tabs keep that rendering but paint at 1 frame per second while no agent works in them. A tab is covered when another tab of its window is selected. Selecting a tab, any agent page operation or upload, and agent tab creation restore full rate before the work runs; full rate lasts 15 seconds after the last operation finishes (`native/include/xenon/tab_render_pacing.hpp`). Pacing never follows tab ownership, so a handoff commit does not change it. Pages stay visible to scripts, and their timers, network and audio are unaffected; only BeginFrame-driven work slows: painting, `requestAnimationFrame`, CSS animation and displayed video frames. One GPU device presents every Xenon window, so each covered animated tab otherwise competes with the selected tab's frames. A per-application GPU-driver frame limit applies to that device as a whole. The pacing requires Xenon's engine patch (below); the stock CEF distribution ignores it.

Tests that set Windows owner-only ACLs must run with the ordinary interactive user's Windows permissions. A restricted execution sandbox may reject `WRITE_DAC`; do not weaken product ACLs to make such a runner pass. Administrator privileges are not a normal runtime requirement.

Live test harnesses use a fresh profile and a unique local named pipe, so an existing Xenon session can stay open. The native `--broker-pipe=xenon-example` startup option selects the pipe name; the default is `xenon-browser`. Custom names must start with `xenon-` and contain at most 100 ASCII letters, digits, hyphens or underscores. Use the corresponding full `\\.\pipe\xenon-example` path in the adapter's private configuration or `--pipe` argument. Pipe authentication and current-user ACLs apply equally to custom endpoints. Always use distinct profile directories for separate browser instances.

## Live integration

Run with a generated profile and private test pipe:

```powershell
node ./tests/integration.mjs
```

This creates a unique `.cache/integration-*` profile, seeds only synthetic paired clients in that isolated profile, starts a local instrumented website, launches the browser, and exercises actual modern and legacy MCP stdio clients. It never imports an existing browser profile. It terminates only the process tree it started. Results are written to `out/integration-results.json`; fixture page screenshots and telemetry remain in the disposable profile for diagnosis. Test failures produce a nonzero exit status.

Protected HTTPS sign-in tests use a separate build target. Configure with `-DXENON_BUILD_AUTH_FIXTURE=ON`, build `XenonAuthTest` and `vault_fixture_seed`, then run `node tests/auth-integration.mjs`. The separate `build/auth-fixture/Release/XenonAuthTest.exe` accepts only the pinned loopback test certificate through a compile-time hook; that hook is absent from `Xenon.exe`. This fixture uses public synthetic credentials, a generated marked profile and no Windows certificate-store changes. Never package the test browser, seed executable or fixture key. Read `tests/fixtures/AUTH-FIXTURE.md` for the fixture's exact scope.

## Release ZIP

```powershell
./scripts/package.ps1
```

Packaging requires built browser and adapter output. It creates a fresh staging directory under `dist/`, installs only the locked production npm dependencies there, copies the pinned Node runtime and CEF runtime/resources, includes project and dependency licenses, creates the Windows x64 ZIP and writes a SHA-256 checksum. Generated runtime profiles are never packaged. Current packages are **unsigned releases**. `VERSION` drives the native application and CMake project versions, the default package name and the version the MCP server reports; packaging refuses a browser DLL built for a different version. It must be `MAJOR.MINOR.PATCH` or `MAJOR.MINOR.PATCH-alpha.N` without leading zeros, which the CMake configuration, installer builder and package verifier all enforce. When preparing a release, update the `version` fields in `package.json` and `package-lock.json` to match it; adapter tests fail on a mismatch.

## Windows installer and update publication

```powershell
./scripts/bootstrap-installer.ps1
$version = (Get-Content ./VERSION -Raw).Trim()
./scripts/build-installer.ps1 -Zip "./dist/Xenon-$version-windows-x64-unsigned.zip"
```

The installer compiler is pinned separately and installed in portable mode in ignored build storage. The installer builder first verifies the ZIP checksum and every manifest entry, then compiles the same runtime files into `Xenon-<version>-windows-x64-setup-unsigned.exe` and writes its SHA-256. Installation requires no administrator access. Fresh installs offer an empty, writable program folder on a local fixed drive, defaulting to `%LOCALAPPDATA%\Programs\Xenon Browser`; updates reuse the recorded installation folder. Relocation requires uninstalling and reinstalling, while the separate browser data and pairing files remain. MCP host paths must point into the chosen program folder. Do not replace the signed upstream compiler with an unverified download.

Do not publish packages with the codec-enabled engine described in [website video codecs](#website-video-codecs) until codec licensing for distributing it is confirmed. 0.1.6 was built that way and withdrawn the same day for this reason. Stock-engine releases are published from the hosted CI artifacts. Hosted CI builds only the stock engine, so a codec-enabled release must be built on a maintainer machine with `./scripts/build.ps1 -CefRoot <codec-enabled distribution> -Test`, then package, verify, test the isolated installer, build the installer, smoke-test the extracted ZIP, and run the CI live suites and both `media-integration.mjs` modes on the packaged DLL.

Publish the ZIP, installer and both `.sha256` files as assets on a release tagged `v<version>` in `Xero-05/xenon-browser`, after reviewing checks and release notes. Mark alpha releases as prereleases; publish `MAJOR.MINOR.PATCH` versions as regular releases and mark them latest. Do not edit existing release assets in place or reuse a version for different bytes. Verify that GitHub reports each installer asset's `digest` as `sha256:<expected digest>` before publishing: the native updater refuses missing/mismatched digests, unexpected asset names and downgrades. The updater enumerates published releases, so alpha releases are eligible without relying on GitHub's stable-only latest-release endpoint.

The native updater uses GitHub HTTPS metadata and asset hashes; it does not yet verify an Authenticode publisher. No signing private key belongs in the repository. Introducing signed releases requires a reviewed expected-publisher verification policy, signing after branding, timestamping, regeneration of manifests/checksums after signing, and signing the final installer. Keep unsigned filenames and labels until that complete path is configured.

## Website video codecs

The stock CEF archive in `dependencies.lock.json` supports WebM but omits
H.264/AAC. Those codecs require a CEF source build; a Xenon runtime switch or
an extra player script cannot enable them in the stock binary.

The optional `cef.mediaSourceBuild` recipe pins the same CEF and Chromium source
revisions, the automation script hash and depot_tools revision. Build it in a
dedicated directory without spaces, with hundreds of GB free, Python 3,
Visual Studio 2022 C++ tools with the C++ ATL component
(`Microsoft.VisualStudio.Component.VC.ATL`) and Windows SDK 10.0.28000 (including Debugging
Tools). The first build can take hours:

```powershell
./scripts/build-cef-media.ps1 -WorkDirectory E:\XenonCefMedia-154 -Python C:\Python312\python.exe
$cefVersion = (Get-Content ./dependencies.lock.json -Raw | ConvertFrom-Json).cef.version
$cefMedia = "E:\XenonCefMedia-154\chromium\src\cef\binary_distrib\cef_binary_${cefVersion}_windows64_minimal"
./scripts/build.ps1 -CefRoot $cefMedia -Test
node tests/media-integration.mjs --windows-codecs
node tests/media-integration.mjs
```

The current source recipe uses Windows GPU decoding for H.264 and Windows Media
Foundation for AAC. It sets `proprietary_codecs=true ffmpeg_branding=Chromium`,
disables FFmpeg video decoders and bundled OpenH264, and applies the hash-pinned
`scripts/patches/cef-windows-codecs.patch`. The patch extends Chromium's existing
Windows audio decoder to AAC-LC/HE-AAC, retaining the upstream OS requirement for
xHE-AAC. MP4 streaming passes raw AAC access units to Media Foundation instead
of adding the ADTS headers used by FFmpeg. It does not relax the sandbox or
install Windows codec DLLs.

The source script resumes only a directory marked with its exact recipe. It
produces the matching CEF sandbox bootstrap and complete distribution, and leaves
the downloaded stock distribution intact. `build.ps1 -CefRoot` checks the CEF version and uses
that distribution for both compilation and bootstrap branding verification.
Later builds reuse the selected distribution from CMake's cache. To switch
back to the stock distribution, pass `-CefRoot ./third_party/cef` explicitly.
The branding check also uses that cached distribution unless `--cef-root` is
specified, including when checking an extracted package from this build.

For an already completed build of the previous pinned FFmpeg recipe, use
`-MigrateToWindowsCodecs -BuildOnly` once to reuse its source/dependency checkout
and incremental build output. Migration accepts only the exact previous recipe,
preserves its marker and distribution, and applies the tracked patch; it does
not accept an arbitrary source revision. `-BuildOnly` skips dependency sync and
is intended only for a prepared checkout. Subsequent retries use `-BuildOnly`
without the migration switch. Failed builds retain their logs.

For the previous Windows-codec recipe, use `-UpdateVideoScheduling -BuildOnly`
once. This accepts only that exact recipe and preserves its marker and previous
distribution. The additional hash-pinned
`scripts/patches/cef-video-frame-release-priority.patch` schedules Windows video
frame-release waits at the compositor's GPU priority. This prevents background
presentation work from starving decoder picture recycling; sync-token fences
remain in place. Other platforms retain their original priority. Rebuild the
complete CEF distribution before using it with Xenon; this is not a runtime flag.

For the video-scheduling recipe, use `-UpdateBackgroundFrameRate -BuildOnly`
once, with the same marker and distribution preservation. The additional
hash-pinned `scripts/patches/cef-background-frame-rate.patch` lets
`CefBrowserHost::SetWindowlessFrameRate` pace windowed Windows browsers through
Chromium's frame-sink throttling. It is re-applied when a navigation replaces the
page's renderer. Child frames inherit it and video-captured frame sinks are exempt.
All throttled browsers share the highest requested rate; 60 or more removes the
throttle. Xenon uses it for covered tabs, as described above.

The post-build audit checks the generated GN arguments, selected FFmpeg codec
configuration, absence of OpenH264/Symphonia AAC in libcef's dependency graph,
and all three pinned source patches. It emits `xenon-codec-build.json` with engine/bootstrap
hashes. The Xenon build and packaging check both the engine hash and the pinned
recipe before copying this manifest.
`--windows-codecs` requires that provenance and tests actual MP4 and Media Source
video frames and decoded audio PCM.

H.264 playback requires supported Windows GPU decoding; there is no bundled
software fallback. AAC requires the Windows Media Foundation decoder (Windows N
may require Microsoft's Media Feature Pack). Web Audio `decodeAudioData` uses a
different synchronous decoder path and is not enabled for AAC by this patch;
HTML media-element playback and its Web Audio output are tested separately.
DRM services and every GPU/Windows configuration are not covered by these tests.

`-Jobs` bounds compilation parallelism (default four). `-UseGitHubMirror` fetches
Chromium from its official GitHub mirror and still verifies the pinned commit.
The checkout omits prebuilt branded Chrome updater test executables through
a generated `DEPS.xenon-media` manifest, derived from the pinned Git revision.
The filter does not edit `DEPS` or undo CEF's own patches on resumed builds;
all other dependencies, pins and hooks from that revision are preserved.
These executables belong to Chromium's `testonly`
`old_updater` target and are not used by CEF. This also avoids downloading an
updater test package quarantined by Defender during local validation; no
antivirus exclusions or quarantine restoration are required.
Keep failed-attempt logs. If an initial checkout was interrupted before it
wrote `chrome/VERSION`, preserve that incomplete `chromium` directory under a
different name before retrying; completed checkouts and builds are resumable.

Using operating-system decoders is a technical distribution change, not legal
clearance or a universal website compatibility guarantee. Before distributing a codec-enabled engine, address the applicable
codec licensing requirements described by [CEF](https://github.com/chromiumembedded/cef/issues/3559)
and retain the generated `LICENSE.txt` and `CREDITS.html`. Run the native and
live browser suites against the rebuilt binary; a capability declaration is
not sufficient evidence of successful decoding.

## CEF security maintenance

CEF dependency updates remain manual maintainer work. Before each public release, inspect the current stable [CEF build index](https://cef-builds.spotifycdn.com/index.html) and [Chromium security releases](https://chromereleases.googleblog.com/search/label/Stable%20updates). Pick the complete matching Windows x64 CEF distribution, verify the publisher checksum, record a locally verified SHA-256, update the lockfile, and rebuild from a clean dependency directory. Review CEF API changes, bootstrap/sandbox requirements and Chromium licensing notices. Run native, SDK and live acceptance tests on the new runtime before distributing it. Never update just `libcef.dll` inside an older ZIP.

CEF integration guidance: [sandbox/bootstrap requirements](https://github.com/chromiumembedded/cef/blob/master/docs/sandbox_setup.md), [CEF General Usage](https://github.com/chromiumembedded/cef/wiki/GeneralUsage). Xenon does not guarantee proprietary DRM, arbitrary extension compatibility, cloud synchronization, mobile support or built-in AI chat.

## Public Windows code signing

Current release scripts deliberately produce unsigned packages. See [signing preparation](SIGNING.md) for Azure Artifact Signing onboarding, artifact order and the changes required before publishing a signed release.
