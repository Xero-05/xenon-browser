# README screenshots

These are window captures of a locally built production `Xenon.exe` from the **0.1.6** source, taken on **2026-10-17**. They illustrate the UI, not an acceptance-test result or a localized application interface.

| File | View | Dimensions |
| --- | --- | --- |
| `browser-light.png` | Light theme. Personal, Research and Review workspaces; the selected Research tab is controlled by the demo agent, which has filled the planner's form. | 1306 × 833 |
| `browser-dark.png` | Dark theme, the same arrangement in a separate run. | 1306 × 833 |
| `controls-workspaces.png` | Native Controls in Dark theme, Workspaces section, with the Research workspace, its connected demo agent and its tabs. | 1146 × 753 |

Each capture used a newly generated disposable profile and private broker pipe, with the CEF sandbox enabled. A synthetic MCP client named "Research assistant" was seeded into that profile and shared the Research and Review workspaces. Through the normal MCP tools, its "Trip research" worker opened three loopback pages and filled the planner's form with `xenon_batch`; a second worker opened the Review page and released it to the person, so that tab shows human control. The planner is [demo-planner.html](demo-planner.html); the other pages were small inline pages served by the same loopback server, and each page's tab icon was an inline SVG. No existing profile, personal page, real website, account, saved password, file grant or pairing token is included.

The browser windows were placed off-screen without activation, the planner tab was selected through the sidebar's tree control, and each window was rendered with `PrintWindow(PW_RENDERFULLCONTENT)`. Images are cropped only to remove the invisible resize border. The agent cursor is a separate overlay window, so it does not appear. Window title bars show the inactive state.

The captured `Xenon.dll` SHA-256 was `6bd420f82ba5f2c207c508ea31fcc63228221a4e54a1f26b55de580ed2766e6b`. That build used the optional media-enabled CEF distribution described in [BUILD](../BUILD.md#website-video-codecs); published packages use the stock CEF distribution, which draws the same interface.

When refreshing screenshots, use a new synthetic profile and private pipe, keep the CEF sandbox enabled, avoid credentials and personal data, and capture the full native application window. Update both [English](../../README.md) and [简体中文](../../README.zh-CN.md) captions and image paths together.
