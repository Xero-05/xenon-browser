# Contributing

Keep changes reviewable and describe the concrete behavior they change. An issue is useful for discussing a major browser-engine or protocol redesign before investing in implementation. Do not include accounts, captured browsing sessions, personal screenshots, vault databases, CSV imports, tokens or logs containing page data in issues or pull requests.

Coding agents and human contributors should read [AGENTS.md](AGENTS.md) for the source map, build commands, scoped checks and implementation invariants. If you are connecting an agent to use the browser rather than changing its code, use the separate [agent operating guide](docs/AGENT_GUIDE.md) and [MCP setup](docs/MCP.md).

Fork the repository, create a focused branch, and open a pull request explaining the problem, resulting behavior and validation. Small fixes do not require a preliminary issue. Use synthetic fixtures for reproductions and keep unrelated refactors out of the change.

Use C++20 for native broker/CEF code and strict TypeScript for the MCP adapter. Keep the adapter unprivileged: credentials are decrypted only in the native browser process. Browser state and agent identity must remain separate. Never implement handoff by reattaching a debugger, switching profiles, reloading, focusing or resizing the tab.

Before a pull request:

1. Run the checks appropriate to the change in [AGENTS.md](AGENTS.md). Native/browser changes normally need `./scripts/build.ps1 -Test` and the relevant live fixture workflows; a documentation-only edit does not require rebuilding CEF.
2. Add regression coverage for authorization, ownership, node identity, uncertain outcomes or credential-boundary changes.
3. Check that errors and diagnostics contain no secrets or raw protocol payloads.
4. Update documentation when changing tool schemas, native grants, coverage or known limits.
5. Include dependency license and lockfile changes with manual dependency updates.

Third-party code must have a compatible license and clear provenance. Do not copy proprietary browser code or import personal browser profiles for development. Contributions are provided under the project's Apache-2.0 license; keep existing copyright and license notices.

Report vulnerabilities through [GitHub private vulnerability reporting](https://github.com/Xero-05/xenon-browser/security/advisories/new), following the [reporting policy](SECURITY.md). Include a minimal synthetic reproduction and affected version; do not post live credentials or exploit details in public issues. See [security boundaries](docs/SECURITY.md).
