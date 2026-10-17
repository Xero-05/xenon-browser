<div align="center">

<img src="assets/branding/xenon-icon.png" alt="Xenon Browser 图标" width="104" height="104">

# Xenon Browser

**你和 AI 智能体共用的 Windows 浏览器。**

为你提供熟悉的 Chromium 日常浏览，为 MCP 智能体提供原生控制层，<br>每个智能体能接触什么，都由你决定。

[![最新版本](https://img.shields.io/github/v/release/Xero-05/xenon-browser?label=release&color=0f766e)](https://github.com/Xero-05/xenon-browser/releases/latest)
[![Windows 发行构建](https://github.com/Xero-05/xenon-browser/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/Xero-05/xenon-browser/actions/workflows/windows.yml)
[![Windows 10/11 x64](https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-0078d4)](docs/GETTING_STARTED.md)
[![27 个 MCP 工具](https://img.shields.io/badge/MCP-27%20tools-6e56cf)](docs/MCP.md)
[![许可证：Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

[**下载 Windows 版本**](https://github.com/Xero-05/xenon-browser/releases/latest) · [快速入门](docs/GETTING_STARTED.md) · [用户指南](docs/USER_GUIDE.md) · [智能体指南](docs/AGENT_GUIDE.md) · [MCP 参考](docs/MCP.md)

[English](README.md) · **简体中文**

</div>

![浅色主题的 Xenon：Research 工作区中当前选中的标签页由 Trip research 智能体控制，页面带青绿色边框，工具栏显示控制权标签](docs/screenshots/browser-light.png)

<p align="center"><sub>智能体在自己的 Research 工作区里填写一个合成的旅行规划页面。青绿色边框和工具栏中的控制权标签表明谁在控制这个标签页。<a href="docs/screenshots/README.md">截图说明</a>。</sub></p>

## 为什么选择 Xenon

- **智能体能接触什么，由你决定。** 每个 MCP 宿主都由你亲自配对，默认只读。客户端和工作区设置决定它能使用哪些工作区、已保存账号和文件，并由 Xenon 的原生代码强制执行。
- **人和智能体安全地共用标签页。** 每个标签页只有一个控制者，边框颜色一目了然。在智能体的页面上点击或输入，该智能体就会暂停。把正在运行的标签页交给智能体或收回，都不需要重新加载页面。
- **多个智能体并行工作。** 多个已配对宿主，或同一宿主下的多个工作会话，可以各自操作自己的标签页。各工作区的 Cookie 和存储彼此独立。
- **机密留在原生层。** 已保存的密码存放在加密凭据库中，由原生代码填写，MCP 永远不会返回它们。智能体无法发起通行密钥提示、打开其他应用或共享你的屏幕。
- **自带模型。** Xenon 不内置大语言模型，也不需要模型 API 密钥。模型由你的本地 stdio MCP 宿主提供。

## 0.1.5 之后的新功能

以下改动已在源代码中。对应的 0.1.6 版本正在等待 H.264/AAC 编解码器许可审查，暂未发布，因此最新下载仍是 0.1.5。


- **智能体可以请你帮忙。** 借助新的 `xenon_attention` 工具，遇到 CAPTCHA、MFA 或需要你批准的操作时，智能体可以让标签页闪烁橙色；如果你允许，还会显示一条可打开该标签页的 Windows 通知。
- **更聪明的地址栏。** 根据历史记录和书签给出建议并自动补全，可选择搜索引擎，还新增了网站信息按钮，用来查看证书、Cookie 和网站设置。
- **日常必备功能。** 全屏（**F11**）、标签页网站图标、带文件夹的书签管理器、可搜索的历史记录、**清除浏览数据**、设置窗口，以及从 Chrome、Edge 等 Chromium 浏览器导入书签和历史记录。
- **与 Windows 更好地配合。** 可设为默认浏览器，在对应应用中打开 `mailto:` 和会议链接，并在视频会议中共享屏幕；每项操作都需先在原生提示中确认。

阅读 [0.1.6 说明](docs/RELEASE-0.1.6.md)或[全部发行说明](docs/RELEASE_NOTES.md)（英文）。

## 应用截图

| 深色主题 | Xenon Controls |
| --- | --- |
| ![深色主题的 Xenon，显示同一个由智能体控制的旅行规划页面](docs/screenshots/browser-dark.png) | ![Xenon Controls，显示 Research 工作区、已连接的 Trip research 智能体及其标签页](docs/screenshots/controls-workspaces.png) |
| 同一个正在运行的页面，显示在 Xenon 的深灰色界面中。网页也会跟随主题。 | 工作区、已连接智能体、标签页控制权、会话恢复，以及共享和文件控制。 |

## 功能

### 熟悉的浏览体验

- **与 Chrome／Edge 相似的工具栏。** 后退、前进和刷新；带建议和网站信息按钮的地址栏；页面查找、缩放、全屏、下载、打印／PDF 和网站权限。
- **按工作区分组的标签页**，显示在可调整宽度的侧边栏中。标签页可拖动排序、在窗口间移动或拖出成新窗口；右键菜单可复制、固定、静音、移动和关闭标签页，并支持 Chromium 键盘快捷键。
- **数据由你掌控。** 带文件夹的书签管理器、可搜索的历史记录、清除浏览数据、从 Chromium 浏览器或书签文件导入，以及可选择搜索引擎、主题和语言的设置窗口。
- **扩展。** 在控制中心添加的已解压 Manifest V3 Chromium 扩展，会在重启后于持久工作区中运行。智能体无法安装、配置或读取扩展。
- **支持英语和简体中文**、系统／浅色／深色主题，以及简短的欢迎导览。

### 与智能体协作

- **独立工作区。** 持久工作区各自保存 Cookie 和本地存储。你可以创建、重命名、配置工作区，或把它共享给已配对客户端；智能体工作结束后，可经确认一次关闭工作区的全部标签页。仅供用户使用的隐私工作区采用内存配置文件。
- **每个标签页只有一个控制者。** 每个标签页／控制组只有一个可写控制者，智能体自己创建的标签页自动归它控制。实时交接可以把标签页及其弹出窗口交给其他已授权的工作会话或你本人，已加载的页面、表单状态、滚动位置和连接全部原样保留。
- **你随时可以介入。** 在智能体的页面上点击、输入、滚动或拖动，会暂停该智能体，但控制权不变。排队中的操作会被取消，智能体必须重新获取页面证据才能继续。**Take ownership（接管）** 可以让你保留控制权。
- **有据可依的页面证据。** 对当前视口内已渲染文本的有界观察、只含网页内容的截图，以及基于元素或坐标的操作；页面一旦变化，旧操作就会被拒绝。
- **限定文件访问与恢复。** 原生文件／文件夹授权与不透明上传句柄、工作区下载元数据、工作会话重连与退役，以及用于检查不确定操作、且不会自动重放的操作日志。

### 一切尽在掌握

- **已保存账号。** 各工作区共用的 Windows DPAPI 加密凭据库，支持从 Chrome、Edge 和 Firefox 导入 CSV，提供 Chrome 风格的 Save/Update 提示，并在登录框下方显示填充列表。智能体只能凭你针对精确 HTTPS 来源授权的不透明账号 ID 登录。账号分为**默认**（作为一组授权给客户端或工作区）和**敏感**（需逐个授权）。
- **Xenon Controls** 包含 Clients（客户端）、Workspaces（工作区）、Passwords（密码）、Extensions（扩展）和 Developer mode（开发者模式）几个部分。你可以在这里撤销访问、管理资源、处理你触发的网站对话框，或用 **Stop all agents（停止所有智能体）** 结束本次运行中的智能体控制。锁定 Windows 会同时锁定凭据库并停止智能体控制。
- **更新由你决定。** 提供当前用户安装程序和便携 ZIP 包，从固定发行仓库进行原生更新检查，并校验大小和 SHA-256。安装始终需要你决定，并正常关闭浏览器。

当前页面的边框颜色表示控制权：**青绿色**表示有可用的智能体控制者，**橙色**表示因你的输入而暂停，**灰色**表示由你控制或智能体不可用。智能体请求帮助时，对应标签页会在侧边栏中闪烁橙色。

## 快速开始

1. **安装。** 从[最新发行版本](https://github.com/Xero-05/xenon-browser/releases/latest)下载未签名的 Windows 安装程序及其 `.sha256` 文件，校验无误后，在本地固定驱动器上选一个空的、可写的程序文件夹。[快速入门](docs/GETTING_STARTED.md#1-install-xenon)里有具体命令。发行包已包含 Node 和 MCP 适配器。
2. **打开 Xenon Browser。** 从开始菜单启动。首次安装会先让你选择 **English** 或 **简体中文**，然后提供可跳过的快速导览；以后可通过 **菜单 → 帮助 → 快速导览** 再次查看。
3. **打开 Xenon Controls。** 使用 **Ctrl+Shift+X**、工具栏中的滑块按钮，或网页右键菜单。
4. **配对 MCP 宿主。** 按照[配对与宿主配置](docs/GETTING_STARTED.md#3-pair-your-mcp-host)操作。妥善保管生成的配对文件；对每个独立信任的宿主分别配对。
5. **授予访问权限。** 配置客户端权限，然后共享一个已有工作区，或允许自动创建工作区。要让智能体操作页面，客户端策略和工作区必须同时允许。

新客户端默认**只读**，自动创建工作区处于关闭状态，配额为**四个已连接工作会话**和**四个自动创建的工作区**。客户端策略决定权限上限，各工作区还能进一步收紧。共享工作区永远不会授予凭据库访问权限。

允许页面交互和自动创建工作区之后，可以这样问你的智能体：

> 用 Xenon 在新工作区打开 https://example.com，查看可见页面，并告诉我页面主标题。保持标签页打开，完成后让工作会话退役。

常见流程是 `xenon_worker_create` → `xenon_tab_create` → `xenon_observe`，再按需执行绑定页面证据的操作，最后调用 `xenon_worker_retire`。退役会释放工作会话配额，同时保留标签页、工作区和网站状态；已退役的工作会话无法再恢复。

想用便携版？先校验 ZIP 的校验和，再解压**完整压缩包**并运行 `Xenon.exe`。可执行文件、DLL、资源、语言目录、适配器和随附的 Node 运行时必须放在一起。

## MCP 工具

Xenon 通过严格校验参数的 TypeScript MCP stdio 适配器提供 **27 个工具**。响应包含结构化 JSON 和 JSON 文本，截图还会附带 MCP 图像内容。[智能体指南](docs/AGENT_GUIDE.md)提供可直接复制的宿主指令，[MCP 参考](docs/MCP.md)说明参数、页面证据和错误处理。

| 工具 | 用途 |
| --- | --- |
| `xenon_worker_create`, `xenon_workers`, `xenon_worker_resume`, `xenon_worker_retire` | 创建、列出、重连和退役逻辑工作会话。 |
| `xenon_workspaces` | 列出已授权给该配对客户端的工作区。 |
| `xenon_tabs`, `xenon_tab_create`, `xenon_tab_close` | 在授权范围内列出、打开和关闭标签页。 |
| `xenon_control_status`, `xenon_control` | 查看控制权，并凭当前控制权版本号获取、释放或交接控制权。 |
| `xenon_activity` | 读取可信的用户活动与暂停元数据，不含输入内容和页面文本。支持的宿主还可订阅 `xenon://control/activity`。 |
| `xenon_attention` | 请用户前往你控制的标签页：其边框会闪烁橙色，用户开启后还会显示可打开该标签页的 Windows 通知。完成后清除请求。 |
| `xenon_observe`, `xenon_screenshot` | 获取有界视口证据和只含网页内容的图像，并检查文档身份与证据是否仍然有效。 |
| `xenon_inspect` | 在同一个 `observationId` 下返回已渲染节点、允许的网页截图和控制状态。采集期间发生变化的证据不会返回；这是经过校验的采集区间，而非原子快照。 |
| `xenon_navigate`, `xenon_interact`, `xenon_wait`, `xenon_dialog` | 导航、执行有限时长的完整操作、等待已渲染文本，以及处理智能体控制的 JavaScript 对话框。原生安全提示仍由用户处理。 |
| `xenon_batch` | 在一次调用中按顺序执行最多 16 个已观察到的填写、选择、勾选或点击操作，并报告每一步的结果。 |
| `xenon_accounts`, `xenon_login` | 列出已授权账号的元数据，并请求受保护登录；不会返回凭据库中的用户名或密码。 |
| `xenon_folders`, `xenon_files`, `xenon_upload`, `xenon_downloads` | 查找已批准的句柄、选择已授权的上传文件，以及查看下载元数据。 |
| `xenon_operation` | 在超时或断开连接后检查保留的变更操作，再决定下一步。 |

观察和文本等待使用同一套已渲染视口过滤规则，并报告覆盖范围、遗漏和截断情况。隐藏标签和视口外的文本不会返回；想看更多内容，需要滚动后重新观察。元素操作要使用新获取的元素引用，坐标操作要使用匹配的截图及其报告的 CSS 像素比例。受支持的日期输入框接受 `YYYY-MM-DD`。

浏览器默认允许**同时连接 16 个工作会话**和 **64 个标签页**，并额外受各客户端配额限制。你可以在启动时把全局工作会话上限设为 1–256，MCP 调用者无法更改。这些是并发上限，不是累计创建上限。不同标签页可以并行运行，同一标签页内的输入按顺序执行。

## 工作区、会话与控制权

- **复用工作区**可以继续使用相同的 Cookie、网站登录状态或已记住的 MFA。共享工作区会让获准客户端用上这些会话。独立配置文件能隔离浏览器存储，但使用同一远程账号的会话仍可能改动同一份线上数据。
- **暂停**通常在你最后一次页面活动约两秒后结束；如果按键或鼠标按钮还没释放、IME 正在组合输入，或你打开的对话框仍在显示，暂停会相应延长。控制权在此期间保持不变。智能体要继续操作，必须先确认暂停已解除并重新获取页面证据。**Take ownership（接管）**、**Give to agent（交给智能体）** 和实时交接都属于明确的控制权变更。
- **关闭标签页或 Xenon** 不会删除已保存的工作区及其 Cookie（包括会话 Cookie）。**Restore last session（恢复上次会话）** 会以你的控制权重新加载符合条件的已保存地址，但不包括隐私和受保护页面；它不会恢复表单状态、渲染进程内存或旧的智能体操作。
- **Remove workspace（移除工作区）** 需要确认，且不能移除 Personal；它会关闭该工作区的标签页、取消下载并撤销其授权。共享的凭据库账号、已完成的下载和原始上传文件仍会保留。配置文件清理范围有限，遇到锁定或不安全的路径时可能延后执行；这既不算安全擦除，也不等于网站退出登录。

## 使用限制

- **平台与宿主：** 支持 Windows x64 和外部本地 stdio MCP 宿主。只能远程连接的宿主需要其他传输方式；视觉任务需要支持图像的客户端和模型。不内置模型，不提供云同步或已签名安装程序，也不保证兼容专有 DRM 或任意扩展。发行包使用标准 CEF 引擎，可以播放 WebM 等开放格式，但无法播放 H.264 或 AAC 媒体。
- **凭据：** 已保存账号功能只覆盖有限类型的 HTTPS 表单。MFA、通行密钥、CAPTCHA、嵌入式登录组件和特殊流程都需要你亲自处理；智能体无法发起通行密钥提示。受保护登录期间不会返回观察结果和截图，原生 Save/Fill/Resume 确认也无法通过 MCP 接受。测试夹具的覆盖不代表已验证真实鼠标键盘操作下的提示，也不代表兼容所有真实网站。
- **浏览功能：** 屏幕共享始终共享所有显示器，无法只共享单个窗口或标签页，并且只能在你控制的标签页中经原生提示确认后开始。智能体无法发起屏幕共享或打开其他应用的链接，也无法截取正在共享屏幕的标签页。导入只读取书签和历史记录；密码请使用 CSV 导入。只有你本人可以在 Windows 设置中选择默认浏览器。
- **文件与结果：** 不支持目录上传、File System Access API 选择器和跨框架委托文件选择器。选中文件不代表服务器已经收到文件，操作已派发也不代表网站任务成功。再次操作之前，请先检查结果不确定的操作和页面状态。
- **信任：** 所有网站文本、图像、对话框和下载名称都是不可信内容。过滤隐藏内容并不能让模型免于提示注入，也无法识别所有可见秘密。授权工作区就等于开放对应的页面会话。DPAPI 无法防御以同一 Windows 用户身份运行的恶意软件。Xenon 尚未经过独立安全审计；请阅读[安全边界](docs/SECURITY.md)。
- **更新与验证：** 更新需要你决定安装并重启，且不会保留正在运行的渲染进程状态。发布的哈希依据发行元数据验证下载内容，不能证明发布者身份。合成测试和原生测试驱动的覆盖情况与实际界面操作覆盖分别记录；完整的菜单／对话框、高对比度和多 DPI 验收矩阵尚未完成。参见[测试说明](docs/TESTING.md)。

## 从源代码构建

Xenon 使用 **C++20**、**CEF 的沙箱化 Chrome 运行时**、**原生能力代理**和**严格校验参数的 TypeScript MCP 适配器**。宿主通过 stdio 启动适配器，适配器再经由当前用户的 Windows 命名管道连接代理。客户端、工作区、控制权、账号和文件授权检查全部由原生代码执行。这里没有模型指定的 JavaScript、原始 CDP、隐藏 DOM 提取或操作系统控制接口。

准备好 Windows 10/11 x64、带 Windows SDK 和 CMake 的 Visual Studio 2022 C++ Build Tools、Node 24、npm、Git 和 7-Zip 之后（参见[前置条件](docs/BUILD.md)）：

```powershell
./scripts/bootstrap.ps1
npm.cmd ci
./scripts/build.ps1 -Test
./build/app/Release/Xenon.exe
```

请把匹配的运行时文件放在可执行文件旁边。参照 [TESTING.md](docs/TESTING.md) 为你的改动选择检查，并遵循[贡献者指令](AGENTS.md)。不要针对共享输出并发运行构建或桌面测试套件。

## 文档

| 指南 | 内容 |
| --- | --- |
| [快速入门](docs/GETTING_STARTED.md)／[用户指南](docs/USER_GUIDE.md) | 安装、配对、日常浏览、账号、工作区和故障排查。 |
| [智能体指南](docs/AGENT_GUIDE.md)／[MCP 参考](docs/MCP.md) | 宿主指令、工具流程、活动、交接和恢复。 |
| [架构](docs/ARCHITECTURE.md)／[安全](docs/SECURITY.md) | 实现、信任边界、授权和当前限制。 |
| [构建](docs/BUILD.md)／[品牌资源](docs/BRANDING.md)／[签名](docs/SIGNING.md) | 依赖、打包、启动程序资源和签名前期准备。 |
| [测试](docs/TESTING.md)／[发行说明](docs/RELEASE_NOTES.md) | 可复现检查、各版本验证证据和待完成的验收工作。 |
| [贡献指南](CONTRIBUTING.md) | 公开贡献流程。 |

以上指南目前为英文。改动功能、版本、截图或限制时，请同步更新 [English](README.md) 和[简体中文](README.zh-CN.md) 两份 README。

## 许可证与安全

Xenon 源代码采用 [Apache-2.0](LICENSE) 许可证。CEF、Chromium、Node 及其他依赖保留各自的许可证，参见[第三方声明](THIRD_PARTY_NOTICES.md)。请通过[私密安全报告渠道](SECURITY.md)报告漏洞。
