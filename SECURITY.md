# Security reports

Please report suspected vulnerabilities privately through [GitHub private vulnerability reporting](https://github.com/Xero-05/xenon-browser/security/advisories/new). Do not open a public issue containing passwords, pairing tokens, sensitive website content or a working secret-exfiltration payload.

Include the Xenon version, Windows version, affected boundary, expected and actual behavior, and a minimal reproduction using synthetic accounts and pages. Redact private paths, URLs and identifiers from diagnostics. A report does not need real credentials or a copy of your browser profile.

Xenon is an experimental unsigned alpha. Maintainers will triage reports as availability permits; there is no guaranteed response time, independent audit or long-term supported-version commitment. Updates are manual. Reproduce against the newest published alpha where practical and avoid treating an older release as maintained indefinitely.

For architectural boundaries, credential encryption, prompt-injection limits and file permissions, read [docs/SECURITY.md](docs/SECURITY.md). The [build guide](docs/BUILD.md#cef-security-maintenance) describes Chromium/CEF updates. Do not disable the sandbox, add a certificate bypass or expose a debugging port to work around a problem.
