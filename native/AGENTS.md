# Native development

The shared [repository instructions](../AGENTS.md) apply. This guide covers `native/`; read it when following adapter requests into native enforcement as well.

## Source map

Paths below are relative to `native/` unless prefixed with `../`. Headers are in `include/xenon/`.

| Area | Start here |
| --- | --- |
| Authorization, ownership, queues, worker lifecycle and operation journal | `src/broker.cpp`, `include/xenon/broker.hpp`, `include/xenon/contracts.hpp`, `include/xenon/permissions.hpp` |
| CEF integration, observations, guarded input and protected login | `src/cef_engine.cpp`, `include/xenon/cef_engine.hpp`, `include/xenon/pointer_motion.hpp`, `include/xenon/pointer_target.hpp`, `include/xenon/pointer_overlay.hpp` |
| Windows startup, shell, input routing and controls | `src/main_win.cpp`, `src/cef_shell.cpp`, `src/native_ui.cpp`, `include/xenon/browser_shell.hpp`, `include/xenon/ui_theme.hpp`, `include/xenon/native_input_policy.hpp` |
| Bookmarks/history, folders, import and non-destructive legacy migration | `src/browser_data.cpp`, `include/xenon/browser_data.hpp`, `include/xenon/browser_import.hpp` |
| Links for other apps and default-browser launch links | `include/xenon/external_protocol.hpp`, `include/xenon/launch_urls.hpp`, `src/main_win.cpp`, `../installer/xenon.iss` |
| Vault, CSV import, file capabilities and profile cleanup | `src/vault.cpp`, `src/file_policy.cpp`, `src/workspace_storage.cpp`, matching headers and `include/xenon/local_security.hpp` |
| Rendered-evidence filter, isolated-world credential scripts and main-world passkey gate | `src/visible_evidence.cpp`, `include/xenon/login_monitor.hpp`, `include/xenon/human_autofill.hpp`, `include/xenon/passkey_guard.hpp` |
| Native update checks and Windows setup | `src/updater.cpp`, `include/xenon/updater.hpp`, `../installer/`, `../scripts/build-installer.ps1`, `../VERSION` |

## Implementation workflow

- Use C++20 and the surrounding code's conventions. Trace changes through the broker contract, engine implementation and asynchronous callbacks before altering behavior.
- For public contract changes, inspect the corresponding schema in `../adapter/src/server.ts` and update [MCP documentation](../docs/MCP.md) alongside enforcement.
- Locate regression coverage in `../tests/native/` and the relevant `../tests/*-integration.*` fixture. Read the [test selection guide](../tests/AGENTS.md) before building or running suites.

## Build constraints

Use `./scripts/build.ps1 -CoreOnly -Test` for core changes that do not require CEF, or `./scripts/build.ps1 -Test` for the full browser. Run commands from the repository root.

`-CoreOnly` reconfigures the shared `build` directory without the browser. Run the build script without that switch to restore the full configuration. Do not run concurrent builds against the same output tree or rebuild a DLL while a live suite uses it. Coordinate shared files and browser test slots when multiple contributors are working.

See [BUILD](../docs/BUILD.md) for dependency/setup details and [BRANDING](../docs/BRANDING.md) before changing bootstrap resources. Do not edit downloaded third-party code in place.
