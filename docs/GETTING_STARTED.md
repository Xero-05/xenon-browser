# Get started with Xenon

This guide uses **0.1.0-alpha.9**, an unsigned Windows x64 alpha. You need Windows 10 or 11 with a desktop session. Browsing needs no model account. Agent use additionally needs an MCP host that can launch a local stdio server; image support is needed for visual tasks.

The release includes the browser, its matching Chromium/CEF runtime, the MCP adapter and Node. You do not need to install Node separately to run the packaged version.

## 1. Download and extract

Download these two assets from the [alpha.9 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.9):

- `Xenon-0.1.0-alpha.9-windows-x64-unsigned.zip`
- `Xenon-0.1.0-alpha.9-windows-x64-unsigned.zip.sha256`

Use the release asset, rather than GitHub's automatically generated **Source code** archive. Source archives require a [build](BUILD.md).

In PowerShell, change to the folder containing both downloaded files and verify the checksum:

```powershell
$zip = '.\Xenon-0.1.0-alpha.9-windows-x64-unsigned.zip'
$expected = (Get-Content -LiteralPath ($zip + '.sha256') -Raw).Trim().Split(' ')[0]
$actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if ($actual -ine $expected) { throw 'Checksum mismatch. Do not use this archive.' }
Write-Host 'Checksum matches.'
```

This checks the archive against the published checksum; it is not a publisher signature. The alpha is unsigned, so Windows may show an unknown-publisher or reputation warning. Verify the release source before deciding to run it; normal use does not require administrator privileges or disabling Windows protection.

Extract **all** files into a stable folder you own. For example, put the folder containing `Xenon.exe` at `C:\Users\you\Apps\Xenon`. Keep the DLLs, `locales`, resource files, `adapter`, `runtime` and `node_modules` with it. Do not run the executable inside the ZIP or copy it out by itself.

## 2. Open the browser

Run `Xenon.exe`. A fresh launch opens one blank tab in **Personal**. Use the address bar to visit a website.

Press **Ctrl+Shift+X** while a Xenon browser window is active to open **Xenon Controls**. You can also right-click a webpage and select **Xenon Controls and Accounts**. This native window manages paired clients, workspaces, control ownership, saved accounts and file permissions.

Closing Controls hides it; it does not exit the browser. Use the browser menu's **Exit** to close the application. Starting Xenon again while it is running returns to that existing session. A new application launch starts blank; cookies and local storage remain, so revisiting a website may still find you signed in.

## 3. Pair your MCP host

Keep Xenon open. In PowerShell, change to the extracted folder containing `Xenon.exe`, then run:

```powershell
.\runtime\node.exe .\adapter\dist\src\cli.js pair --name "My agent host" --output "$env:LOCALAPPDATA\Xenon-agent.json"
```

In Controls, select **My agent host** under **Pending pairing requests**, then click **Approve**. The terminal waits up to five minutes for this approval. When pairing succeeds, it saves a private configuration at the output path.

That file contains a pairing token. Keep it outside repositories, shared folders, messages and model prompts. The output file must not already exist. Pair separately, using another name and output path, for each independently trusted host.

## 4. Configure the host

Add Xenon as a local stdio MCP server in your host. Setting names vary by host; a common configuration looks like this:

```json
{
  "mcpServers": {
    "xenon": {
      "command": "C:\\Users\\you\\Apps\\Xenon\\runtime\\node.exe",
      "args": [
        "C:\\Users\\you\\Apps\\Xenon\\adapter\\dist\\src\\cli.js",
        "serve",
        "--config",
        "C:\\Users\\you\\AppData\\Local\\Xenon-agent.json"
      ]
    }
  }
}
```

Replace all three example paths with your actual absolute paths. Keep each argument separate, including paths containing spaces. Use `node.exe` as the command; the host starts the adapter itself. Start the browser before connecting the host.

The adapter talks to a private Windows named pipe. It is not an HTTP server, and no model API key belongs in the Xenon pairing file. See [MCP setup](MCP.md) for custom pipes, protocol details and reconnection.

## 5. Try a small task

Ask the connected agent:

> Use Xenon to open https://example.com in a new workspace, inspect the visible page, and tell me its heading. Keep the tab open.

The expected tool sequence is:

1. `xenon_worker_create` creates a logical worker and, by default, its own workspace.
2. `xenon_tab_create` opens a tab that the worker automatically owns.
3. `xenon_observe` supplies current rendered evidence; subsequent actions use the returned handles.
4. `xenon_worker_retire` retires a finished worker while preserving its tab and workspace.

Controls shows the workspace, worker and tab. To reuse an existing human website session, select that workspace and paired client, then click **Share workspace**. Give the agent the resulting authorized workspace to use instead of creating a fresh one. Sharing includes the website sessions already present there.

Try typing in an agent-owned page. The owner stays the same, but its actions pause while you interact and for about two seconds afterward. To keep control indefinitely, select the tab and click **Take ownership**; use **Give to agent** when ready.

## Next steps

- [User guide](USER_GUIDE.md): workspaces, passwords, imports, MFA, files and troubleshooting.
- [Agent guide](AGENT_GUIDE.md): copyable host instructions and safe tool-use workflow.
- [MCP reference](MCP.md): tool workflow, evidence, activity notifications and uncertain outcomes.
- [Security boundaries](SECURITY.md): what grants permit and what Xenon cannot protect against.
- [Validation status](TESTING.md): tested fixtures and remaining human/real-site acceptance work.

For a source checkout, follow [BUILD.md](BUILD.md); use `third_party\node\node.exe` or an installed compatible Node 24 runtime instead of the packaged `runtime\node.exe`.
