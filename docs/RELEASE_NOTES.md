# 0.1.0-alpha.9

First public Windows x64 alpha of Xenon Browser. Download the **unsigned** browser ZIP and its SHA-256 file from the [GitHub release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.9). Extract the entire archive; no installer or automatic updater is included.

## Included

- Sandboxed Chromium/CEF browser with persistent isolated workspaces and native Xenon controls.
- External stdio MCP clients, reusable workers, automatic creator ownership and concurrent work across tabs.
- Permission-only handoff on the same live tab, fresh-evidence requirements and temporary pauses for human page input.
- Rendered viewport observations, screenshots, guarded page actions, dialogs and permitted file transfers.
- Native DPAPI-encrypted saved-account vault, CSV import, supported-form Save/Update and human fill prompts, and account grants for agents.
- Blank startup, explicit session restoration, workspace removal, and Xenon branding based on the supplied logo.
- Setup, user and agent guides, contributor instructions, Windows CI and retained upstream license notices.

The public package refresh adds documentation and attribution to the locally tested alpha.9. Browser, CEF, Node and adapter executable payloads are unchanged. The earlier local archive is not the public artifact; use the checksum attached to the public release.

## Validation and limits

Local alpha.9 validation passed 76 live browser checks, seven native tests, 14 adapter tests, 32 login-monitor tests, 24 human-autofill script tests and five bootstrap-resource checks. The release archive has a per-file SHA-256 manifest and is checked with an extracted-package smoke test. See [test evidence and scope](TESTING.md) and [Windows CI runs](https://github.com/Xero-05/xenon-browser/actions/workflows/windows.yml).

The pinned CEF build was verified against the upstream Windows stable index during publication preparation. Updates remain manual; a past stable check does not establish future security currency.

This is experimental software. Real-site authentication compatibility and the full physical credential-picker workflow are not universally verified. MFA, passkeys, CAPTCHA and unusual forms require human handling. Hidden-text filtering cannot make a model immune to prompt injection. There is no independent security audit, proprietary DRM guarantee, broad extension guarantee, cloud sync or built-in model. Some inherited Chromium menus remain. See [security boundaries](SECURITY.md).

Follow [Get started](GETTING_STARTED.md) for installation and MCP pairing, [the user guide](USER_GUIDE.md) for daily controls, and [the agent guide](AGENT_GUIDE.md) for a copyable host instruction template.
