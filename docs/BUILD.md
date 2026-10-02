# Building and packaging Xenon

## Windows x64 prerequisites

- Windows 10/11 x64 with an interactive desktop for browser tests.
- Visual Studio 2022 Build Tools, Desktop development with C++, MSVC v143, Windows 10/11 SDK, and the CMake component.
- Node.js 24 LTS, npm, Git, PowerShell 7 or Windows PowerShell 5.1, and Windows `tar.exe`.
- Several GB of free disk space for CEF, generated build files and release staging.

`scripts/bootstrap.ps1` downloads CEF, SQLite, nlohmann/json and the redistributable Node runtime. Archive/header SHA-256 values are pinned in `dependencies.lock.json`. npm dependencies and their integrity digests are pinned in `package-lock.json`. Dependencies are downloaded into ignored `third_party/` subdirectories; never commit browser profiles, password CSVs, vaults or pairing configuration.

```powershell
./scripts/bootstrap.ps1
npm.cmd ci
./scripts/build.ps1 -Test
```

The CMake generator is `Visual Studio 17 2022`, architecture x64, Release configuration. Native code uses C++20 and the static MSVC runtime. Sandbox support is mandatory. The matching CEF distribution supplies `bootstrap.exe`; the build copies it to `Xenon.exe` beside the application `Xenon.dll`, which exports `RunWinMain`. A build-only tool replaces the copy's icon and version resources. The original bootstrap, executable code, entry point and manifest remain unchanged; `node tests/branding-resources.mjs` verifies that boundary. Do not substitute a bootstrap from another CEF release. See [branding](BRANDING.md) for the artwork and native theme implementation.

The application is in `build/app/Release/`. Native tests are registered with CTest; TypeScript and SDK tests use `npm test`. `./scripts/build.ps1 -CoreOnly -Test` builds the broker/vault/file tests without CEF. Reconfigure without `-CoreOnly` to restore the full browser build.

The default concurrent worker limit is 16 across all paired clients. Set a different limit when starting the browser, for example:

```powershell
.\Xenon.exe --max-concurrent-workers=32
```

The supported range is 1–256. This is a native startup setting, unavailable to MCP clients; it takes effect on the next browser launch. It limits connected workers, not the number created over the browser's lifetime. Retirement and disconnection free connected capacity without closing tabs or deleting profiles. Resuming a disconnected worker requires an available slot. Higher values are resource budgets, not tested performance guarantees; the separate 64-tab limit still applies. Hosts should reuse workers or pass an existing granted `workspaceId` when creating replacements to avoid creating unnecessary persistent profiles.

Parallel work uses a fixed startup policy: `disable-backgrounding-occluded-windows` keeps covered Chrome windows rendering and `disable-background-timer-throttling` keeps background page timers running. These internal settings follow [Chrome tooling guidance](https://github.com/GoogleChrome/chrome-launcher/blob/main/docs/chrome-flags-for-tools.md); they never change during a handoff. They can increase CPU/GPU use and power consumption compared with an ordinary browser's background throttling. External Chromium command-line settings remain disabled.

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

Packaging requires built browser and adapter output. It creates a fresh staging directory under `dist/`, installs only the locked production npm dependencies there, copies the pinned Node runtime and CEF runtime/resources, includes project and dependency licenses, creates the Windows x64 ZIP and writes a SHA-256 checksum. Generated runtime profiles are never packaged. This is an **unsigned alpha**, not a signed installer or an automatic update channel.

## CEF security maintenance

Alpha updates are manual. Before each public release, inspect the current stable [CEF build index](https://cef-builds.spotifycdn.com/index.html) and [Chromium security releases](https://chromereleases.googleblog.com/search/label/Stable%20updates). Pick the complete matching Windows x64 CEF distribution, verify the publisher checksum, record a locally verified SHA-256, update the lockfile, and rebuild from a clean dependency directory. Review CEF API changes, bootstrap/sandbox requirements and Chromium licensing notices. Run native, SDK and live acceptance tests on the new runtime before distributing it. Never update just `libcef.dll` inside an older ZIP.

CEF integration guidance: [sandbox/bootstrap requirements](https://github.com/chromiumembedded/cef/blob/master/docs/sandbox_setup.md), [CEF General Usage](https://github.com/chromiumembedded/cef/wiki/GeneralUsage). Xenon does not guarantee proprietary DRM, arbitrary extension compatibility, cloud synchronization, mobile support or built-in AI chat.
