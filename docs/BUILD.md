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

Publish the ZIP, installer and both `.sha256` files as assets on a release tagged `v<version>` in `Xero-05/xenon-browser`, after reviewing checks and release notes. Mark alpha releases as prereleases; publish 0.1.1 as a regular release and mark it latest. Do not edit existing release assets in place or reuse a version for different bytes. Verify that GitHub reports each installer asset's `digest` as `sha256:<expected digest>` before publishing: the native updater refuses missing/mismatched digests, unexpected asset names and downgrades. The updater enumerates published releases, so alpha releases are eligible without relying on GitHub's stable-only latest-release endpoint.

The native updater uses GitHub HTTPS metadata and asset hashes; it does not yet verify an Authenticode publisher. No signing private key belongs in the repository. Introducing signed releases requires a reviewed expected-publisher verification policy, signing after branding, timestamping, regeneration of manifests/checksums after signing, and signing the final installer. Keep unsigned filenames and labels until that complete path is configured.

## CEF security maintenance

CEF dependency updates remain manual maintainer work. Before each public release, inspect the current stable [CEF build index](https://cef-builds.spotifycdn.com/index.html) and [Chromium security releases](https://chromereleases.googleblog.com/search/label/Stable%20updates). Pick the complete matching Windows x64 CEF distribution, verify the publisher checksum, record a locally verified SHA-256, update the lockfile, and rebuild from a clean dependency directory. Review CEF API changes, bootstrap/sandbox requirements and Chromium licensing notices. Run native, SDK and live acceptance tests on the new runtime before distributing it. Never update just `libcef.dll` inside an older ZIP.

CEF integration guidance: [sandbox/bootstrap requirements](https://github.com/chromiumembedded/cef/blob/master/docs/sandbox_setup.md), [CEF General Usage](https://github.com/chromiumembedded/cef/wiki/GeneralUsage). Xenon does not guarantee proprietary DRM, arbitrary extension compatibility, cloud synchronization, mobile support or built-in AI chat.

## Public Windows code signing

Current release scripts deliberately produce unsigned packages. See [signing preparation](SIGNING.md) for Azure Artifact Signing onboarding, artifact order and the changes required before publishing a signed release.
