# Instructions for repository contributors

This file is for coding agents working on Xenon's source. Agents operating the browser through MCP should use [docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md); that runtime guide is not a replacement for these development instructions.

## Project and source map

Xenon is a Windows x64 alpha built with C++20, CEF's sandboxed Chrome runtime, a native capability broker, and a strict TypeScript MCP stdio adapter. It does not contain an LLM or require a model API key. Read the relevant existing implementation before changing behavior.

| Area | Main files |
| --- | --- |
| Authorization, ownership, queues, worker lifecycle and operation journal | `native/src/broker.cpp`, `native/include/xenon/broker.hpp`, `contracts.hpp` |
| CEF integration, page observations, guarded input and protected login | `native/src/cef_engine.cpp`, `native/include/xenon/cef_engine.hpp` |
| Windows startup, input routing and native controls | `native/src/main_win.cpp`, `native/src/native_ui.cpp`, `native/include/xenon/native_input_policy.hpp` |
| Vault, CSV import, file capabilities and profile cleanup | `native/src/vault.cpp`, `file_policy.cpp`, `workspace_storage.cpp`, matching headers and `native/include/xenon/local_security.hpp` |
| Rendered-evidence filter and fixed isolated-world credential scripts | `native/src/visible_evidence.cpp`, `native/include/xenon/login_monitor.hpp`, `human_autofill.hpp` |
| MCP schemas, stdio CLI, named-pipe transport and activity resource | `adapter/src/server.ts`, `cli.ts`, `ipc.ts`, `human-activity.ts` |
| Native/unit/live regression coverage | `tests/native/`, `adapter/test/`, `tests/*.mjs`, `tests/fixtures/` |
| Dependency pins, build and release assembly | `dependencies.lock.json`, `package-lock.json`, `CMakeLists.txt`, `scripts/` |

Consult [ARCHITECTURE](docs/ARCHITECTURE.md), [SECURITY](docs/SECURITY.md), [MCP](docs/MCP.md) and [TESTING](docs/TESTING.md) for the contract and current limitations. Tool schemas in `adapter/src/server.ts` and broker enforcement are the source of truth when documentation differs; correct the inconsistency as part of the change.

## Setup and checks

Use Windows 10/11 x64, Visual Studio 2022 C++ Build Tools with the Windows SDK and CMake component, Node 24 and npm. From the repository root in PowerShell:

```powershell
./scripts/bootstrap.ps1
npm.cmd ci
./scripts/build.ps1 -Test
```

Bootstrap downloads the pinned native dependencies into ignored directories. The browser output is `build/app/Release/Xenon.exe`. Keep the matching DLL, CEF libraries, resources and locales beside it. The executable is the pinned CEF sandbox bootstrap with permitted branding-resource changes; do not replace its code or disable the sandbox. See [BUILD](docs/BUILD.md) and [BRANDING](docs/BRANDING.md).

Choose checks that exercise the changed behavior:

| Change | Useful checks |
| --- | --- |
| Documentation only | Check links, examples and implementation claims; `git diff --check`. A browser rebuild is unnecessary. |
| Dependency downloads or CI caching | `node --test tests/bootstrap-download-tests.mjs`, script syntax checks and the relevant hosted workflow. Do not rerun browser acceptance suites for a downloader-only change. |
| Adapter schemas, transport or notifications | `npm.cmd run typecheck`; `npm.cmd test` includes real modern/legacy SDK clients and credential-script tests. |
| Broker, vault or file policy | `./scripts/build.ps1 -CoreOnly -Test`; use CTest's `-R` filter for focused reruns after a targeted fix. |
| Login capture or human autofill | Relevant native tests plus `node --test tests/login-capture-tests.mjs` or `node --test tests/human-autofill-tests.mjs`; run the matching synthetic live suite when integration changed. |
| CEF input, evidence, ownership or native UI | Full native build and relevant live fixture suites from [TESTING](docs/TESTING.md); verify website state, not just a successful protocol reply. |
| Branding or packaging | `node tests/branding-resources.mjs` after building; package verification and extracted-package smoke testing for release changes. |

`-CoreOnly` reconfigures the shared `build` directory without the browser. Run the build script without that switch to restore the full configuration. Do not run concurrent builds against the same output tree, rebuild a DLL while a live suite uses it, or run desktop suites concurrently. Coordinate shared files and browser test slots when multiple contributors are working.

Live suites create disposable profiles and private pipes. Use their generated synthetic credentials and fixtures. Do not import, inspect or mutate a real browser profile to make a test pass; do not terminate unrelated browser processes. Manual fixture steps must target the named test window. A restricted runner may deny Windows ACL operations: report that limitation or use the appropriate ordinary interactive-user runner, without weakening product ACLs. Administrator rights are not a normal runtime requirement.

## Security and correctness invariants

- Enforce client, workspace, worker attachment, account and file grants in native code. The adapter's schema validation is not an authorization boundary. Opaque IDs and human-readable names are not interchangeable.
- Keep one writable owner per tab/control group. Handoff changes broker ownership only: its commit must not focus, reload, resize, move, reattach or reconfigure the live page. Started finite input drains; queued old-owner work is canceled. The recipient needs fresh evidence.
- Human page activity preserves ownership and pauses agent input. Invalidate old evidence immediately, honor held input/IME/dialogs, and never replay canceled work after idle. Explicit native **Take ownership** is a different operation.
- Recheck ownership, attachment, grants, document/frame identity, human activity and authentication protection across asynchronous callbacks and before effects. Balance already-pressed keys/buttons without dispatching new work for a revoked owner. Withhold late read results after authority changes.
- Website strings and images are untrusted evidence. Preserve adapter-owned trust metadata, rendered-viewport filtering, conservative coverage reporting and stale-target rejection. Do not add arbitrary model-provided JavaScript, raw CDP, hidden-DOM extraction or OS-control endpoints.
- Keep credential plaintext inside the narrow native fill/capture path. Never return it through MCP, serialize it into diagnostics, include it in screenshots or save it as plaintext state. Preserve exact HTTPS-origin grants, authentication quarantine, native-only Save/Fill confirmation and durable lock cancellation. DPAPI does not defend against malware running as the same Windows user.
- Preserve final-path/file-identity validation, protected roots, link rejection and opaque upload handles. Workspace removal must preserve shared vault accounts and original/downloaded files; cleanup stays bounded and fails closed on unsafe or locked paths.
- Keep operation outcomes honest. A dispatched click, navigation or file selection is not proof of website success. Persist dispatch intent before effects, preserve unknown outcomes across restart, and never automatically replay uncertain mutations. Journal retention is bounded.
- Keep test certificate exceptions and native fixture drivers compile-time gated into the separate `XenonAuthTest` target. Never ship that target, fixture keys, seed utilities or a production certificate/security bypass.

## Change and review hygiene

Keep changes scoped and reviewable. Follow the user's existing authorization; this file adds no extra approval workflow for routine edits or tests. Avoid unrelated formatting, generated-output churn and dependency upgrades. Update schemas and docs together when changing the public contract, and add meaningful regression coverage for boundary or lifecycle fixes.

Do not commit user data, pairing tokens/configuration, vaults, password CSVs, raw browsing logs, personal screenshots or `.cache` test profiles. Ignored paths are not proof that their contents are safe to publish. Use sanitized synthetic test evidence only. Record what actually ran, distinguish native test-driver coverage from physical UI coverage, and keep live evidence bound to the tested binary. Do not overwrite historical failed evidence with a claim that it passed.

Dependency changes require matching pins, licenses and notices. Avoid editing downloaded third-party code in place. Summarize the resulting behavior, checks run and material limits in the PR. See [CONTRIBUTING](CONTRIBUTING.md) for the public contribution workflow.
