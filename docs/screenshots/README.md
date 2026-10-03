# README screenshots

These are unedited window captures of the locally built production `Xenon.exe` interface for **0.1.0-alpha.14**, taken on **2026-10-03**. They illustrate the UI, not an acceptance-test result or a localized application interface.

| File | View | Dimensions |
| --- | --- | --- |
| `browser-light.jpg` | Light theme, Research and Review workspaces, agent-owned integration workbench. | 1186 × 793 |
| `browser-dark.jpg` | Dark theme, the same agent-owned workbench. | 1186 × 793 |
| `controls-workspaces.jpg` | Native Controls, Workspaces section, synthetic Research assistant. | 1206 × 803 |

Capture used a newly generated disposable profile and private broker pipe. The demo MCP client had page-interaction access only; saved accounts, uploads, downloads, and automatic workspace creation were disabled. Research and Review were synthetic seeded workspaces. Page content came from [the integration workbench](../../tests/fixtures/workbench.html), served over loopback by [the fixture server](../../tests/fixtures/server.mjs). No existing user profile, personal page, account, file grant, or pairing token is included in these images.

The browser shell and Controls were captured through the Computer Use window API. Only the application-window JPEGs are retained here; generated profile state and pairing files stay outside the documentation in ignored disposable storage. Light/Dark refers to the native shell theme; the test webpage retains its own styling.

The captured `Xenon.dll` SHA-256 was `0502d163ee4148261181be19701dd3c9aee8c1880b458dc62844d3c07a196a91`. This identifies the local capture binary; it does not claim a new build or test pass for other working-tree changes.

When refreshing screenshots, use a new synthetic profile and private pipe, keep the CEF sandbox enabled, avoid credentials and personal data, and capture the full native application window. Update both [English](../../README.md) and [简体中文](../../README.zh-CN.md) captions and image paths together.
