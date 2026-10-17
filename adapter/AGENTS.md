# MCP adapter development

The shared [repository instructions](../AGENTS.md) apply. This guide covers `adapter/`, including its tests. The adapter uses strict TypeScript and remains unprivileged; authorization belongs in native code.

## Source map

Paths below are relative to `adapter/`.

| Area | Start here |
| --- | --- |
| Public tools, schemas and results | `src/server.ts` |
| Stdio CLI and pairing | `src/cli.ts`, `src/private-config.ts` |
| Named-pipe transport and native errors | `src/ipc.ts` |
| Human-activity resource and notifications | `src/human-activity.ts` |
| SDK compatibility and regressions | `test/adapter.test.ts`, `test/sdk-fixture.ts`, other `test/*.test.ts` |
| Compiler settings and commands | `tsconfig.json`, `../package.json` |

## Implementation workflow

- Follow existing schema, transport and error-handling patterns. For changes to native behavior, read [native/AGENTS.md](../native/AGENTS.md) and trace the request into broker enforcement; schema validation alone cannot authorize it.
- Keep stdout reserved for MCP messages. Send startup/status diagnostics to stderr without tokens, credential values or raw protocol payloads.
- Preserve adapter-owned trust metadata and the activity notification allowlist. Website content must not become trusted control metadata.
- Update [MCP documentation](../docs/MCP.md) with public schema changes. Use existing modern/legacy SDK fixtures for protocol regressions.

## Checks

From the repository root, run `npm.cmd run typecheck` and `npm.cmd test`. The test command builds the adapter and includes SDK and fixed-script tests. Edit TypeScript sources, not generated `dist/` output.

Read [tests/AGENTS.md](../tests/AGENTS.md) for additional checks when a change crosses into native or live behavior. Adapter-only changes do not require a browser rebuild.
