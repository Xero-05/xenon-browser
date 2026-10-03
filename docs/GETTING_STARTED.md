# Get started with Xenon

This guide uses **0.1.0-alpha.14**, an unsigned Windows x64 alpha with the native shell, reorganized Controls and read-only defaults for new paired clients. See [release notes](RELEASE_NOTES.md).

You need Windows 10 or 11 with a desktop session. Browsing needs no model account. Agent use additionally needs an MCP host that can launch a local stdio server; image support is needed for visual tasks.

The release includes the browser, its matching Chromium/CEF runtime, the MCP adapter and Node. You do not need to install Node separately to run the packaged version.

## 1. Install Xenon

Download these two assets from the [alpha.14 release](https://github.com/Xero-05/xenon-browser/releases/tag/v0.1.0-alpha.14):

- `Xenon-0.1.0-alpha.14-windows-x64-setup-unsigned.exe`
- `Xenon-0.1.0-alpha.14-windows-x64-setup-unsigned.exe.sha256`

Use the release asset, rather than GitHub's automatically generated **Source code** archive. Source archives require a [build](BUILD.md).

In PowerShell, change to the folder containing both downloaded files and verify the checksum:

```powershell
$installer = '.\Xenon-0.1.0-alpha.14-windows-x64-setup-unsigned.exe'
$expected = (Get-Content -LiteralPath ($installer + '.sha256') -Raw).Trim().Split(' ')[0]
$actual = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
if ($actual -ine $expected) { throw 'Checksum mismatch. Do not run this installer.' }
Write-Host 'Checksum matches.'
```

This checks the installer against the published checksum; it is not a publisher signature. The alpha is unsigned, so Windows may show an unknown-publisher or reputation warning. Verify the release source before deciding to run it; normal use does not require administrator privileges or disabling Windows protection.

Run the installer. On a fresh installation, choose an empty, dedicated folder on a local fixed drive that your Windows account can write to; the default is `%LOCALAPPDATA%\Programs\Xenon Browser`. Setup rejects network/removable drives, drive roots, linked folders and locations that contain or overlap Xenon's default browser-data folder. Review the destination shown before installing.

Setup adds a Start menu shortcut and offers a desktop shortcut. Browser data stays separately in `%LOCALAPPDATA%\Xenon Browser`; choosing a program folder does not move it.

Updates reuse your existing installation folder. To relocate an existing installation, including alpha.10, stop connected Xenon MCP adapters, close Xenon, uninstall it through Windows **Installed apps**, then run the new installer and choose another folder. Uninstall keeps your browser data and pairing files. Update your MCP host's Node and adapter paths after relocation. Setup will not force-close your work.

The portable ZIP and its checksum remain available on the same release. To use them, verify the ZIP's SHA-256 the same way and extract **all** files into a stable folder you own. Keep the DLLs, `locales`, resource files, `adapter`, `runtime` and `node_modules` together. A portable copy's update flow opens the installer: choose a program folder if no installed copy exists, or update the existing installed copy. It does not automatically replace the portable folder.

Once this release is installed, future releases can be downloaded through **Xenon Controls → Check for updates**. You do not need to manually download and unpack another ZIP. See [updates and local data](USER_GUIDE.md#updates-and-local-data).

## 2. Open the browser

Open **Xenon Browser** from the Start menu, or run `Xenon.exe` in its installation folder. A fresh launch opens one blank tab in **Personal**. Use the address bar to visit a website. For a source build, run `build/app/Release/Xenon.exe`.

Press **Ctrl+Shift+X** while a Xenon browser window is active to open **Xenon Controls**, or use its toolbar button or webpage context-menu item. Its Clients, Workspaces and Passwords sections manage pairings, access, control ownership, saved accounts and file permissions.

Closing Controls hides it; it does not exit the browser. Use **Menu → Exit Xenon** to close the application. Starting Xenon again while it is running returns to that existing session. A new application launch starts blank; cookies and local storage remain, so revisiting a website may still find you signed in.

## 3. Pair your MCP host

Keep Xenon open. For an installation in the default folder, run these commands in PowerShell:

```powershell
Set-Location "$env:LOCALAPPDATA\Programs\Xenon Browser"
.\runtime\node.exe .\adapter\dist\src\cli.js pair --name "My agent host" --output "$env:LOCALAPPDATA\Xenon-agent.json"
```

If you chose another installation folder, replace the `Set-Location` path with that folder. For a portable ZIP, use the extracted folder containing `Xenon.exe`. Run the same pairing command, then use that folder's `runtime` and `adapter` paths in your host configuration below.

For a source build, run `npm.cmd run build` in the repository, then `node .\adapter\dist\src\cli.js pair --name "My agent host" --output "$env:LOCALAPPDATA\Xenon-agent.json"`. Configure the host with the absolute paths to your Node 24 executable and this checkout's `adapter/dist/src/cli.js`.

In Controls' **Clients** section, select **My agent host** under **Pairing requests**, then click **Approve**. The terminal waits up to five minutes for this approval. When pairing succeeds, it saves a private configuration at the output path. The saved client starts read-only, with automatic workspace creation disabled and quotas of four workers and four automatic workspaces.

That file contains a pairing token. Keep it outside repositories, shared folders, messages and model prompts. The output file must not already exist. Pair separately, using another name and output path, for each independently trusted host.

## 4. Configure the host

Add Xenon as a local stdio MCP server in your host. Setting names vary by host; a common configuration looks like this:

```json
{
  "mcpServers": {
    "xenon": {
      "command": "C:\\Users\\you\\AppData\\Local\\Programs\\Xenon Browser\\runtime\\node.exe",
      "args": [
        "C:\\Users\\you\\AppData\\Local\\Programs\\Xenon Browser\\adapter\\dist\\src\\cli.js",
        "serve",
        "--config",
        "C:\\Users\\you\\AppData\\Local\\Xenon-agent.json"
      ]
    }
  }
}
```

Replace all three example paths with your actual absolute paths. The Node and adapter paths must use your chosen installation folder, or your portable extraction folder; the pairing-file path is the output path you selected above. Keep each argument separate, including paths containing spaces. Use `node.exe` as the command; the host starts the adapter itself. Start the browser before connecting the host.

The adapter talks to a private Windows named pipe. It is not an HTTP server, and no model API key belongs in the Xenon pairing file. See [MCP setup](MCP.md) for custom pipes, protocol details and reconnection.

## 5. Try a small task

For the new-workspace example below, select the paired client in **Clients → Configure**, enable page interaction and automatic workspace creation, then **Save**. These native permissions are deliberate opt-ins. Uploads, downloads and saved-account use can stay disabled for this example.

Ask the connected agent:

> Use Xenon to open https://example.com in a new workspace, inspect the visible page, and tell me its heading. Keep the tab open.

The expected tool sequence is:

1. `xenon_worker_create` creates a logical worker and, by default, its own workspace.
2. `xenon_tab_create` opens a tab that the worker automatically owns.
3. `xenon_observe` supplies current rendered evidence; subsequent actions use the returned handles.
4. `xenon_worker_retire` retires a finished worker while preserving its tab and workspace.

Controls shows the workspace, worker and tab. To reuse an existing human website session, select the paired client in Clients, then the workspace in Workspaces and choose **Share read-only**. Configure that workspace to permit interaction when needed; its effective access is still bounded by the client's policy. Give the agent the authorized workspace ID to use. Sharing includes the website sessions already present there.

Try typing in an agent-owned page. The owner stays the same, but its actions pause while you interact and for about two seconds afterward. To keep control indefinitely, select the tab and click **Take ownership**; use **Give to agent** when ready.

## Next steps

- [User guide](USER_GUIDE.md): workspaces, passwords, imports, MFA, files and troubleshooting.
- [Agent guide](AGENT_GUIDE.md): copyable host instructions and safe tool-use workflow.
- [MCP reference](MCP.md): tool workflow, evidence, activity notifications and uncertain outcomes.
- [Security boundaries](SECURITY.md): what grants permit and what Xenon cannot protect against.
- [Validation status](TESTING.md): tested fixtures and remaining human/real-site acceptance work.

For a source checkout, follow [BUILD.md](BUILD.md); use `third_party\node\node.exe` or an installed compatible Node 24 runtime instead of the packaged `runtime\node.exe`.
