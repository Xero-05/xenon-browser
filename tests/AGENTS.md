# Test selection and execution

The shared [repository instructions](../AGENTS.md) apply. Read this guide when editing tests or choosing/running checks, including tests under `adapter/test/`. All commands run from the repository root in PowerShell.

## Choose checks by changed behavior

| Change | Useful checks |
| --- | --- |
| Documentation only | Check links, examples and implementation claims; `git diff --check`. A browser rebuild is unnecessary. |
| Dependency downloads or CI caching | `node --test tests/bootstrap-download-tests.mjs tests/bootstrap-extract-tests.mjs`, script syntax checks and the relevant hosted workflow. Do not rerun browser acceptance suites for a downloader-only change. |
| Adapter schemas, transport or notifications | `npm.cmd run typecheck`; `npm.cmd test` includes real modern/legacy SDK clients and credential-script tests. |
| Broker, vault or file policy | `./scripts/build.ps1 -CoreOnly -Test`; use CTest's `-R` filter for focused reruns after a targeted fix. |
| Login capture or human autofill | Relevant native tests plus `node --test tests/login-capture-tests.mjs` or `node --test tests/human-autofill-tests.mjs`; run the matching synthetic live suite when integration changed. |
| CEF input, evidence, ownership or native UI | Full native build and relevant live fixture suites from [TESTING](../docs/TESTING.md); verify website state, not just a successful protocol reply. |
| Branding or packaging | `node tests/branding-resources.mjs` after building; package verification and extracted-package smoke testing for release changes. |
| Installer or native updater | Focused `updater_tests`, isolated installer fixture tests, native update UI check, package verification and the packaged-browser smoke test. Use synthetic state and test installer identities; do not install over the user's copy merely to test setup. |

Find native unit tests in `tests/native/`, adapter/SDK tests in `adapter/test/`, script and live suites in `tests/*.mjs` and `tests/*.ps1`, and synthetic pages/servers in `tests/fixtures/`. Test registration lives in `CMakeLists.txt`; npm commands live in `package.json`. Consult the relevant sections of [TESTING](../docs/TESTING.md) for suite prerequisites and limitations, and [BUILD](../docs/BUILD.md) for package commands.

## Shared build and desktop state

`-CoreOnly` reconfigures the shared `build` directory without the browser. Run the build script without that switch to restore the full configuration. Do not run concurrent builds against the same output tree, rebuild a DLL while a live suite uses it, or run desktop suites concurrently. Coordinate shared files and browser test slots when multiple contributors are working.

Live suites create disposable profiles and private pipes. Use their generated synthetic credentials and fixtures. Do not import, inspect or mutate a real browser profile to make a test pass; do not terminate unrelated browser processes. Manual fixture steps must target the named test window. A restricted runner may deny Windows ACL operations: report that limitation or use the appropriate ordinary interactive-user runner, without weakening product ACLs. Administrator rights are not a normal runtime requirement.

## Regression coverage and evidence

- Add meaningful regressions for authorization, ownership, node identity, uncertain outcomes and credential-boundary/lifecycle changes. Test the externally observable result or rejected effect, not only a successful protocol reply.
- Keep test certificate exceptions and native fixture drivers gated into `XenonAuthTest` as required by the root instructions. Production acceptance must use the production target; record when a test driver was used.
- Preserve logs and the command's exit status for slow builds/suites, then inspect failures without rerunning solely to recover output. Keep logs local and publish only sanitized synthetic evidence.
- Record what actually ran, distinguish native test-driver coverage from physical UI coverage, and bind live evidence to the tested binary. Do not overwrite historical failed evidence with a claim that it passed.
