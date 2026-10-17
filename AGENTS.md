# Instructions for repository contributors

This file is for coding agents working on Xenon's source. Agents operating the browser through MCP should use [docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md); that runtime guide is not a replacement for these development instructions.

## Start here

Xenon is a Windows x64 alpha built with C++20, CEF's sandboxed Chrome runtime, a native capability broker, and a strict TypeScript MCP stdio adapter. It does not contain an LLM or require a model API key.

1. Check `git status --short` and preserve existing work. Read the relevant implementation before changing behavior.
2. Read the scoped guide below for the area you will change. These guides supplement this file; the shared invariants below apply everywhere. For changes spanning areas, read each relevant guide.
3. Find files with `rg --files <directory>` and search with `rg -n '<symbol>' <directory-or-file>`. Start with the source maps in the scoped guides; widen only when needed. Avoid searching generated output, downloaded dependencies or test profiles (`build/`, `dist/`, `out/`, `third_party/`, `node_modules/`, `.cache/`).
4. Make a focused change, update affected contracts/docs, and use the [test selection guide](tests/AGENTS.md) before choosing checks. Documentation-only changes need link/example verification and `git diff --check`, without a browser rebuild.
5. Report the resulting behavior, checks actually run and remaining limits. Do not rerun expensive checks just to recover output: retain their logs and exit status, then inspect the relevant portions.

| Work area | Read next |
| --- | --- |
| Native broker, CEF, Windows UI, vault, files and updater | [native/AGENTS.md](native/AGENTS.md) for source locations and build constraints |
| MCP schemas, stdio, transport and notifications | [adapter/AGENTS.md](adapter/AGENTS.md) for source locations and protocol checks |
| Writing or running native, adapter, live or installer tests | [tests/AGENTS.md](tests/AGENTS.md) for check selection, isolation and evidence rules; applies to test execution from any directory |
| Dependencies, build, branding and packaging | [BUILD](docs/BUILD.md), [BRANDING](docs/BRANDING.md) and [test selection](tests/AGENTS.md); start at `dependencies.lock.json`, `package-lock.json`, `CMakeLists.txt`, `scripts/`, `installer/` and `VERSION` |

Read the relevant sections of [ARCHITECTURE](docs/ARCHITECTURE.md) for component boundaries, [SECURITY](docs/SECURITY.md) for security contracts, [MCP](docs/MCP.md) for the public protocol, and [TESTING](docs/TESTING.md) for live suites and recorded limitations. Tool schemas in `adapter/src/server.ts` and broker enforcement are the source of truth when documentation differs; correct the inconsistency as part of the change.

## Setup

Use Windows 10/11 x64, Visual Studio 2022 C++ Build Tools with the Windows SDK and CMake component, Node 24, npm and 7-Zip (installed in `Program Files/7-Zip`). All commands in these guides run from the repository root in PowerShell. For a fresh checkout:

```powershell
./scripts/bootstrap.ps1
npm.cmd ci
./scripts/build.ps1 -Test
```

Bootstrap downloads the pinned native dependencies into ignored directories. On an existing checkout, use the scoped checks instead of repeating setup or a full build for every change. The browser output is `build/app/Release/Xenon.exe`; keep its matching DLL, CEF libraries, resources and locales beside it. The executable is the pinned CEF sandbox bootstrap with permitted branding-resource changes; do not replace its code or disable the sandbox.

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
- Keep updates native-only, bound to the fixed release repository and exact versioned installer asset, with mandatory size/SHA-256 verification, normal TLS validation and bounded requests. Preserve the running/setup mutex boundary, human installation decision, profile retention and downgrade rejection. Do not claim unsigned hashes establish publisher identity or add unattended forced shutdowns.

## Change and review hygiene

Keep changes scoped and reviewable. Follow the user's existing authorization; this file adds no extra approval workflow for routine edits or tests. Avoid unrelated formatting, generated-output churn and dependency upgrades. Update schemas and docs together when changing the public contract, and add meaningful regression coverage for boundary or lifecycle fixes.

Do not commit user data, pairing tokens/configuration, vaults, password CSVs, raw browsing logs, personal screenshots or `.cache` test profiles. Ignored paths are not proof that their contents are safe to publish. Use sanitized synthetic test evidence only. Record what actually ran, distinguish native test-driver coverage from physical UI coverage, and keep live evidence bound to the tested binary. Do not overwrite historical failed evidence with a claim that it passed.

Dependency changes require matching pins, licenses and notices. Avoid editing downloaded third-party code in place. Summarize the resulting behavior, checks run and material limits in the PR. See [CONTRIBUTING](CONTRIBUTING.md) for the public contribution workflow.
