# Release notes

## 0.1.0-alpha.10

This Windows x64 release adds an **unsigned per-user installer** and native update controls. Download the installer and its SHA-256 file from the [alpha.10 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.10), or use the portable ZIP. Follow [Get started](GETTING_STARTED.md) for checksum verification, installation and MCP pairing.

Setup installs to `%LOCALAPPDATA%\Programs\Xenon Browser` and creates a Start menu shortcut. Browser profiles, saved accounts and pairing configuration remain separate from the application files; uninstall retains them. Close Xenon and stop its MCP adapters before an upgrade. Setup does not force-close your work and rejects downgrades.

**Xenon Controls → Check for updates** checks the fixed GitHub release repository on demand. Downloading and opening setup are separate human actions. Xenon verifies the installer's size and SHA-256 before launch; the alpha remains unsigned, so this trusts GitHub and the release maintainers rather than an Authenticode publisher. Updating a portable copy installs the new version into the standard per-user location; it does not overwrite the portable folder.

Focused validation covered native updater boundaries, isolated installer lifecycle checks, the native Updates window, bootstrap branding and an extracted-package smoke test. The available-update UI and a future production upgrade have not been exercised end to end. Unrelated browser acceptance suites were not repeated for this change; see [test evidence and scope](TESTING.md) for the release-specific records and limits.

## 0.1.0-alpha.9

First public Windows x64 alpha of Xenon Browser. Download the **unsigned** browser ZIP and its SHA-256 file from the [GitHub release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.9). Extract the entire archive; no installer or automatic updater is included.

### Included

- Sandboxed Chromium/CEF browser with persistent isolated workspaces and native Xenon controls.
- External stdio MCP clients, reusable workers, automatic creator ownership and concurrent work across tabs.
- Permission-only handoff on the same live tab, fresh-evidence requirements and temporary pauses for human page input.
- Rendered viewport observations, screenshots, guarded page actions, dialogs and permitted file transfers.
- Native DPAPI-encrypted saved-account vault, CSV import, supported-form Save/Update and human fill prompts, and account grants for agents.
- Blank startup, explicit session restoration, workspace removal, and Xenon branding based on the supplied logo.
- Setup, user and agent guides, contributor instructions, Windows CI and retained upstream license notices.

The public package refresh adds documentation and attribution to the locally tested alpha.9. Browser, CEF, Node and adapter executable payloads are unchanged. The earlier local archive is not the public artifact; use the checksum attached to the public release.

### Validation and limits

Local alpha.9 validation passed 76 live browser checks, seven native tests, 14 adapter tests, 32 login-monitor tests, 24 human-autofill script tests and five bootstrap-resource checks. The release archive has a per-file SHA-256 manifest and is checked with an extracted-package smoke test. See [test evidence and scope](TESTING.md) and [Windows CI runs](https://github.com/Xero-05/xenon-browser/actions/workflows/windows.yml).

The pinned CEF build was verified against the upstream Windows stable index during publication preparation. Updates remain manual; a past stable check does not establish future security currency.

This is experimental software. Real-site authentication compatibility and the full physical credential-picker workflow are not universally verified. MFA, passkeys, CAPTCHA and unusual forms require human handling. Hidden-text filtering cannot make a model immune to prompt injection. There is no independent security audit, proprietary DRM guarantee, broad extension guarantee, cloud sync or built-in model. Some inherited Chromium menus remain. See [security boundaries](SECURITY.md).

Follow [Get started](GETTING_STARTED.md) for installation and MCP pairing, [the user guide](USER_GUIDE.md) for daily controls, and [the agent guide](AGENT_GUIDE.md) for a copyable host instruction template.
