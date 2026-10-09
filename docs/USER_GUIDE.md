# Using Xenon

Xenon is a Windows x64 browser for human browsing and external MCP agents. This guide describes the development version after **0.1.2**, including the [desktop toolbar, window and extension changes](RELEASE_NOTES.md#unreleased). Start with [installation and pairing](GETTING_STARTED.md) if you have not connected a client.

## Browser and Controls

New installations begin with language selection and a quick introduction before the first browser tab opens. Choose English or Simplified Chinese, then follow the tour or skip it. The language takes effect immediately on that first launch. **Menu → Help → Quick tour** reopens the browser introduction, and **Menu → Help → GitHub documentation** opens the getting-started guide in a new Personal tab. Existing profiles do not show the introduction automatically.

The toolbar across the top of each window works like Chrome's and Edge's: **Back**, **Forward** and **Reload** on the left, the address and search box in the middle, then the ownership chip and the **Extensions**, **Controls** and **Menu** (⋮) buttons. Hover over an icon for its label. Inside the address box, a key appears when Xenon has saved accounts for the site, and the star bookmarks the page. Tabs are listed in the left sidebar, grouped under workspace names, with **New tab** at the top. Each workspace heading has a **+** that opens a tab in that workspace; pointing at the heading adds a **…** menu beside it. When the list is longer than the window, its scrollbar runs along the sidebar's outer edge, and a newly opened tab scrolls into view. The selected tab joins the page outline. Collapsing a group keeps its tabs and agents running; opening a tab in a collapsed workspace expands it. Open Controls with its toolbar button, **Ctrl+Shift+X**, or **Xenon Controls** in a webpage's context menu. An orange dot on the Controls button means a client is waiting for pairing approval; a blue dot means a Xenon update is available.

Each tab has an **X** to close it; middle-click also closes a tab. **Ctrl+W** closes the selected tab, and **Delete** closes a tab selected in the sidebar. Closing the last tab keeps its workspace and saved website data. Empty saved workspaces remain in the sidebar, including after restart; click one, or select it and press **Enter**, to open a blank human-owned tab. Startup still opens one blank Personal tab. Use the explicit session restore action to reload previously saved pages. **Ctrl+Shift+T** reopens recently closed pages during this run; protected sign-in pages are not remembered.

Press **Enter** in the address box to go; a typed address survives toolbar focus changes. **Ctrl+Enter** completes a single word as `www.<word>.com`. The first click into the box selects the whole address, and **Escape** restores the current address. Background tabs have subtle gray hover feedback, workspace disclosure uses arrowheads, and mouse selection has no dashed focus box. Keyboard navigation draws a rounded focus ring around the focused control. The page outline's top edge doubles as a loading bar. Close buttons on unselected tabs stay visible but quieter until hovered. **Find on page** floats over the page's top-right corner instead of resizing it; **Enter** finds the next match and **Shift+Enter** the previous one.

With the Windows **Animation effects** setting on, buttons and tabs fade on hover, the selected-tab outline glides to a newly chosen tab, the outline color crossfades when control changes, and the address field fades in a focus ring. With it off, or in high contrast, every change is immediate. Only Xenon's own frame animates; the page's size and position never do.

Drag the sidebar's right edge to change its width, or use **Menu → Appearance** for keyboard access to Narrower, Wider and Reset sidebar width. Its range is 180–480 logical pixels, further bounded by space for the page. Double-clicking the edge restores the default. Xenon saves the width alongside your theme preference. Dark mode uses charcoal surfaces with visible gray hover feedback; Light mode uses white surfaces. **System** follows Windows.

### The ownership chip

Left of the Extensions button, a dot in the page outline's color and a short label show who controls the selected tab: **You**, **No agent**, or the agent's name with its state. Click the chip to **Take ownership**, to give a tab you own to a connected agent working in that workspace, or to open Controls. Short status messages, such as a saved bookmark or a refused action, appear in the chip for a few seconds.

### Tabs and windows

Right-click a tab for its menu:

| Command | Effect |
| --- | --- |
| **New tab below** | Opens a blank tab in the same workspace, right after this one. |
| **Reload**, **Duplicate** | Reloads the page, or opens its address again in the same workspace. |
| **Pin** / **Unpin** | Keeps the tab at the top of its workspace group and hides its close button. Pins last for this run. |
| **Mute site** / **Unmute site** | Silences the tab's audio. |
| **Copy link**, **Bookmark tab** | Copies the address or saves a bookmark. Unavailable on protected sign-in pages. |
| **Move tab to window** | Moves the live tab to a new or existing Xenon window. The page is not reloaded. |
| **Move tab to workspace** | Reopens the address in another workspace (see below). |
| **Take ownership** | Requests human ownership, as in Controls. |
| **Close tab**, **Close other tabs in workspace**, **Close tabs below** | Closes tabs in this workspace group; pinned tabs are kept by **Close other tabs**. |
| **Close all tabs in …** | Workspace cleanup, with confirmation (see below). |

Drag a tab in the sidebar to reorder it. Drop it in another Xenon window to move it there, or outside every Xenon window to open it in a new window; dragging a window's only tab moves the window. **Ctrl+N** opens a new window and **Ctrl+Shift+W** closes the current one. Closing a window closes its tabs; closing the last window exits Xenon. Moving a tab between windows keeps the live page, its agent owner and any pause exactly as they were.

Drop a tab on another workspace's heading or between its tabs to move it to that workspace. Each workspace has its own cookies and sign-ins, so a live page cannot change workspace: Xenon reopens the address in the target workspace and then closes the original. Text typed on the page is not carried over, and you are asked to confirm. A tab cannot move to another workspace while an agent controls it, a handoff is pending or agent input is still running; use **Take ownership** first, or wait for the agent to finish. Private workspace tabs and protected sign-in tabs stay where they are. While dragging, the label under the pointer says what the drop will do and a blocked drop shows a no-entry cursor.

### Clean up a workspace after agent work

To close every tab an agent session left open, choose **Close all tabs…** from the workspace heading's **…** button or right-click menu, **Menu → Close all tabs in this workspace…**, or **Controls → Workspaces → Close all tabs…**. Xenon asks for confirmation and states how many tabs will close. Queued agent actions in those tabs are canceled and refused; an action already started finishes first, then the tabs close. Unsaved changes in those tabs are lost. The workspace, its sign-ins, history, site permissions, agent access and saved passwords are kept. This is different from **Remove workspace**, which also deletes the workspace's data and permissions.

### Keyboard shortcuts

| Keys | Action |
| --- | --- |
| **Ctrl+T**, **Ctrl+N**, **Ctrl+Shift+N** | New tab, new window, new private workspace |
| **Ctrl+W** or **Ctrl+F4**, **Ctrl+Shift+W** | Close tab, close window |
| **Ctrl+Shift+T** | Reopen closed tab |
| **Ctrl+Tab**, **Ctrl+Shift+Tab**, **Ctrl+PgDn**, **Ctrl+PgUp** | Next or previous tab in this window |
| **Ctrl+1**–**Ctrl+8**, **Ctrl+9** | Select that tab, or the last tab |
| **Ctrl+L**, **Alt+D**, **F6** | Focus the address box |
| **Ctrl+R** or **F5**; **Ctrl+Shift+R** or **Ctrl+F5** | Reload; reload bypassing the cache |
| **Alt+Left**, **Alt+Right** | Back, forward |
| **Ctrl+F**, **F3**, **Shift+F3** | Find, next match, previous match |
| **Ctrl+D**, **Ctrl+H**, **Ctrl+J**, **Ctrl+P** | Bookmark, history, downloads, print |
| **Ctrl++**, **Ctrl+-**, **Ctrl+0** | Zoom in, out, reset |
| **Ctrl+Shift+X** | Controls |

Tab-switching and focus shortcuts do not count as page input, so they do not pause an agent. Right-clicking a link in a page also offers **Open link in new tab**, **Open link in new window** and **Copy link address**; new tabs opened this way are yours and stay in the link's workspace.

Controls is a separate native window with sections in a left sidebar, rounded buttons and neutral gray list selections. Website pages and MCP clients cannot click its permission or credential buttons. Closing it hides it; the browser keeps running.

Controls has **Clients**, **Workspaces**, **Passwords** and **Extensions** sections. Pairing requests have separate Approve/Deny actions. Approved clients remain paired across launches and start read-only with automatic creation disabled, four concurrent workers and four automatic workspaces. Client configuration sets the overall ceiling; workspace configuration can narrow it. Legacy pairings retain their existing capabilities until configured. Lowering a quota blocks additional admission while allowing existing work to finish.

The Xenon menu supplies new tabs and windows, history, downloads, bookmarks, passwords, extensions, find, zoom, printing/PDF, site permissions and, under **Help**, the tour, updates and third-party notices. Under **Appearance**, choose **Theme: System**, **Theme: Light** or **Theme: Dark**; System is the default. Light and Dark also set the color scheme websites receive, including blank pages; System follows Windows. Xenon's own window changes immediately, but web pages use a newly chosen theme after Xenon restarts. Native bookmarks and history migrate existing workspace records without modifying their original files. Private metadata stays in memory, and new protected-authentication visits are excluded from history. There is no cloud sync or built-in model. Use Xenon's vault for passwords.

## Workspaces and website sessions

A workspace has its own cookies and local storage. **Personal** is the initial human workspace. **Create workspace** in the Workspaces section requests a name and opens a blank human-owned tab. Double-click a workspace or choose **Configure** to rename it and manage client permissions/resources in a separate window. Selecting a workspace in Controls filters its tabs table. An agent can create an automatic workspace only when its client has native permission; otherwise it must request a shared workspace.

Sharing a workspace lets that paired client access its pages and existing website logins. It does not copy those sessions into another workspace. To share, select the workspace and client in Controls, then click **Share read-only**. Configure that workspace and the client to permit interaction or other capabilities. Reuse its workspace ID when asking an agent to continue work later.

Different workers can control different tabs in one workspace. Separate workspaces isolate browser state, but two sessions signed into the same remote account can still change the same online data.

**Private workspace** is for native human browsing and uses memory-only profile storage. It cannot be shared with agents in this release. Downloads and anything explicitly saved outside the private profile can remain; private browsing does not hide activity from the destination website or network.

### Blank startup and recovery

A fresh application launch opens a blank tab. Persistent workspaces retain cookies, including session cookies, and local storage. Closing the browser therefore does not sign you out of every website. Website logout may also leave an identity provider's SSO session active.

**Restore last session** explicitly reloads eligible saved URLs under human ownership. It excludes closed, private and protected-authentication tabs. Recovery does not restore an interrupted renderer's form state or replay agent actions. This differs from handing off a tab that is still open, which retains the live page.

### Remove a workspace

Select a workspace and click **Remove**. Review the native confirmation. Personal cannot be removed this way.

Removal stops new access, lets accepted finite input reach its safe release boundary, closes the workspace's tabs, cancels active downloads and revokes client, account and file permissions for that workspace. Saved accounts in the shared vault, completed downloads and original upload files remain.

The removed workspace stays unavailable across restarts. Its browser profile is scheduled for bounded cleanup on later launches; locked or refused files may remain pending. This is not secure erasure or remote-account logout. Retiring an agent worker or closing a tab does not remove its workspace.

## Ownership, human activity and handoff

The selected tab and page use teal borders for an available agent owner, orange for its temporary pause for human page input, and gray for human ownership or no available agent. Background tabs only show a teal border while their agent owner is available and unpaused; paused or unowned background tabs have no border. Status text and protection labels accompany the colors. The native agent cursor stays visible while the selected page is agent-owned, parks during human input, and resumes without sending extra website input or moving the Windows mouse.

Ownership gives a worker permission to interact with a tab. The worker automatically owns tabs it creates. Workspace permission also allows reads of permitted pages, so ownership is not a privacy barrier between clients deliberately sharing that workspace.

| Action | Effect |
| --- | --- |
| Click, type, scroll or drag on an agent-owned page | Temporarily pauses the agent; ownership remains unchanged. |
| Hover, switch windows or use Controls | Does not by itself count as page input. |
| **Take ownership** | Requests human ownership until you explicitly give it away. |
| **Give to agent** | Gives the selected tab's control group to the selected connected, authorized worker. |
| Agent handoff | Transfers control of the existing tab and its related popups; it does not move them to a new workspace. |
| Worker retirement | Cancels its queued work and retires that worker handle while preserving tabs, workspace and website state. |
| **Stop all agents** | Stops agent control for this browser run; restart Xenon to accept clients again. |

The temporary pause lasts about two seconds after your last page input. Held keys/buttons, text composition and a human-opened JavaScript dialog keep it paused. Already delivered input cannot be recalled; undispatched actions are canceled and never resumed automatically. Before continuing, the agent must read current status and obtain fresh evidence of the page you left.

Supporting MCP hosts can subscribe to activity updates. Other hosts can read activity/control status or notices attached to tool responses. Notifications contain pause metadata, not the text you typed, and may not interrupt a model immediately; native enforcement applies regardless.

Handoff waits for accepted finite input to finish. Its ownership commit sends no browser command and does not reload, refocus or recreate the page. Later human or agent actions can of course be visible to the website; Xenon does not promise that automation is undetectable.

For a website alert/confirm/prompt opened by your interaction, Controls can show the exact tab's dialog. Read the message, enter prompt text if needed, then choose **Accept dialog** or **Dismiss**. Browser security and permission prompts remain human tasks.

The browser defaults to 16 concurrently connected workers across all clients, plus a separate 64-tab limit. New clients also have a quota of four concurrent workers and four automatically created workspaces, configurable in Controls. Legacy clients retain their existing capabilities and migrated global worker limit until configured. Both global and client limits apply. Retiring or disconnecting workers frees connected capacity; removing an automatic workspace frees its workspace quota. A native startup option can set the global worker limit to 1–256; these are resource limits, not tested performance claims. See [worker lifecycle](MCP.md#worker-capacity-and-retirement).

## Saved accounts

Saved accounts live in one native vault per Xenon data directory, shared across workspaces. Website cookies live separately inside each workspace. Removing or updating a vault account affects future fills but does not sign out existing website sessions.

Usernames and passwords are encrypted with Windows DPAPI for your Windows account. Account IDs, HTTPS origins and human labels are metadata. Avoid putting secrets in labels. This does not protect against malware running as your Windows user, and a website receiving a password can read or misuse it.

### Save an account

There are three entry paths:

1. **Native Save/Update bubble.** After you type and submit a supported login, Xenon can offer to save that submitted password in a small bubble under the toolbar's key, as Chrome does. It shows the original login origin and a masked username, never the password. Click **Save** or **Update** only if the password is correct; the offer is not proof that login succeeded. **Not now** or **Escape** discards it.
2. **Save account in Controls.** Enter the HTTPS origin, username, password and an optional recognizable label, then click **Save account**.
3. **Import CSV.** Export passwords using your existing browser, then select **Import CSV** in Controls. Review the masked preview and choose whether to replace or skip conflicts.

CSV import supports the exported formats used by Chrome, Edge and Firefox for supported HTTPS logins. It does not copy a browser's live profile, cookies, bookmarks or all settings. The exported CSV contains plaintext passwords; Xenon leaves it on disk, so remove it when finished and avoid shared folders.

Automatic Save/Update covers top-level HTTPS sign-in with one current password. A conventional form posting to its own site is offered when you submit it. Sign-in pages that work through script, such as Google's two-step page, are offered after you press **Enter** or the sign-in button and the page moves on (it navigates or removes the password box); if the password box stays, Xenon assumes the attempt failed and offers nothing. When the password step does not show the account, Xenon uses the account you typed on the previous step of the same site in that tab. Private workspaces, agent-filled logins, registration/new-password fields, OTP fields, ambiguous and embedded cross-site forms are excluded. Use manual entry/import for unsupported forms.

The encrypted pending offer stays bound to the original login origin through HTTPS SSO redirects and ordinary MFA input. It expires after ten minutes; lock, source-tab closure, non-HTTPS navigation, agent mutation or a new qualifying password edit cancels it. Unchanged credentials do not generate another prompt. Pending offers are not exposed to MCP and saving creates no agent grant.

### Fill an account yourself

On a supported login page:

1. Click a username or password field. Xenon shows its saved accounts for that exact HTTPS origin in a list directly under the field, as Chrome and Edge do, each time you click the field while the password is still empty. You can also click the key in the address box, or select the login tab in Controls' Workspaces section and choose **Passwords → Fill selected login tab**.
2. Click an account, or use **Up**/**Down** and **Enter**. Clicks in the first half second after the list appears are ignored so a click meant for the page cannot choose an account. **Escape**, typing or clicking elsewhere closes the list.
3. Continue on the website yourself. Native human fill does not submit the form.

Each account row also shows its agent access: **Agents: no access**, or the names of paired clients granted that account for this workspace or for all of their workspaces. An agent with access gets only an opaque account handle; it never receives the username or password, which Xenon fills natively when the agent requests sign-in (see below). **Manage passwords…** and **Agent access…** at the bottom of the list open Controls' Passwords section with this site's account selected. If the page has no fillable form, the key opens that section instead.

Ordinary two-field forms need one editable username and an empty password. Separate username/password steps need explicit `username`/`current-password` autocomplete hints. After filling only the username, continue on the website and use **Fill selected login tab** again at the password step.

Xenon refuses ambiguous or covered fields, changed forms, conflicting usernames, nonempty passwords, read-only inputs and unsupported controls. It preserves existing human values. Offers expire after two minutes and can become stale when the page or your input changes; request a fresh offer.

Human fill needs no agent account grant and creates none. It pauses agent effects, cancels queued mutations and protects page evidence while retaining the tab's owner. An existing field becoming populated does not establish successful authentication.

### Let an agent use an account

This is the agent handle on a saved account. Pair the client and share the intended workspace first. Choose the client and workspace in their sections, then select the saved account in **Passwords** and choose **Grant in workspace**, or **Grant to client** for an inheritable client-level resource. Enable saved-account use in both policies. Verify the exact HTTPS origin; subdomains and different ports are different origins.

The agent receives an opaque account ID, origin and label through `xenon_accounts`. It requests protected sign-in with `xenon_login`; the vault does not return the username or password through MCP. The agent path can fill and submit supported same-origin POST forms, including a bounded username-first flow. Saving, pairing, workspace sharing and human autofill do not themselves add account grants.

Protected login withholds detailed observations and screenshots. If protection remains after you finish signing in, select the tab and click **Resume after login** only when no password, code or other secret is visible. The agent then needs fresh evidence.

### MFA and compatibility

Complete MFA, CAPTCHA, passkeys, recovery-code prompts, cross-origin embedded login and unusual forms yourself. Reuse the same workspace when a service remembers your browser; a new workspace starts a separate session. Expiry, device trust and organization policies remain controlled by the service. An unsigned build may not meet a provider's approved-browser requirements.

The credential implementation has synthetic script, native and HTTPS fixture tests. Physical-focus suggestions, clicking the native account picker/Save prompt end to end, and real-site CWL/Duo/PD Portal sign-in are not claimed as verified acceptance. See the [manual test matrix](TESTING.md).

## Extensions

Xenon can run Chromium extensions that you add yourself. In **Controls → Extensions** (or **Extensions → Manage extensions…** in the toolbar), choose **Load unpacked…** and select the extension's folder, the one that contains `manifest.json`. Xenon checks that it is a Manifest V3 extension, copies it into its own data directory and lists it. Restart Xenon to load it. **Disable**, **Enable** and **Remove** also take effect after restart; removing deletes Xenon's copy, not your original folder.

Enabled extensions load into every persistent workspace when Xenon starts. Content scripts, background service workers and network rules such as ad blockers' run as in Chromium. Private workspaces do not load extensions. **Open options** and the toolbar's **Extensions** menu open an extension's options page, or its popup page, in a new tab that you own. Agents cannot read or operate extension pages.

Limits: Chrome Web Store installation, `.crx` packages and Manifest V2 extensions are not supported, and extension toolbar buttons and popups are not shown in Xenon's toolbar. A popup page opened in a tab may not find the page you were viewing, because Xenon's pages are not part of Chromium's own tab strip. Extensions that need those features may not work.

Install only extensions you trust. An extension can read and change the websites it runs on, see what you type into them, including passwords, and change what agents see on those pages. Xenon does not review extensions, and an extension's permissions are not limited by workspace sharing or agent grants. See [extension boundary](SECURITY.md#extensions).

## Files and downloads

Select a workspace in Controls, then **Files** and **Grant file** or **Grant folder**. Client-level resources are available from **Clients → File resources** or the client configuration window; workspace configuration can inherit and restrict those selections. Enable uploads in both policies. Grants persist; **Revoke selected** removes the selected grant. Prefer one file when a whole folder would grant more access than the task needs: eligible files added to an approved folder later can also become available.

Agents receive bounded metadata and opaque file handles. They can select an approved file through an observed visible file input or upload button that opens a chooser in the same frame, including a hidden input behind that button. Directory uploads, File System Access API pickers and cross-frame chooser delegation are unsupported. A selected file is not proof of a completed server-side upload.

Profile files, vault data, recognized pairing configurations and imported password CSV sources are excluded. Conservative filesystem and content checks can also reject otherwise benign files; see [file boundaries](SECURITY.md#files-and-downloads).

Downloads use unique sanitized names in managed workspace storage and are not opened automatically. Agents get download metadata rather than arbitrary local paths or file contents. Removing the workspace keeps completed downloads.

## What agents can see

Structured observations contain filtered rendered text in the current viewport, with frame/coverage information and explicit omissions. Scroll and observe again for offscreen content. Hidden accessibility labels, descriptions and DOM payloads are withheld; editable values and protected password references are not exposed as ordinary observation fields.

Conservative checks may omit legitimate small text, clipped content, complex layouts or some writing systems. Screenshots can help when permitted, but sensitive fields, protected authentication and unverified frames can block capture. Screenshot coordinates use reported CSS scaling rather than assuming screen pixels and page pixels are identical.

All website content—including visible text, images, dialogs, titles and download names—is untrusted evidence. A page cannot authorize changing the task, disclosing credentials or granting itself permission. These filters reduce hidden-content exposure; they cannot make a model immune to misleading websites or classify every visible secret. See [MCP evidence rules](MCP.md#first-task).

## Updates and local data

The default data directory is `%LOCALAPPDATA%\Xenon Browser`; pairing files are wherever you chose during pairing. Keep this data private. Do not upload profiles, vault databases, password exports or pairing files to GitHub.

Xenon checks its fixed GitHub repository over HTTPS for a newer compatible installer about 20 seconds after it starts and every six hours while it runs. When one is available, a blue dot appears on the **Controls** button, the **⋮** menu starts with **Update Xenon to …**, and the update button in Controls names the version. Checking never downloads or installs anything. To check now, choose **Menu → Help → Check for updates**, or press **Ctrl+Shift+X**, then **Check for updates**. Turn off **Check for updates automatically** in the Updates window if you prefer manual checks only. Alpha builds can receive newer alpha releases; stable builds exclude prereleases. A network error or missing verifiable installer cannot trigger installation.

Download the offered update in the native Updates window, then choose to install it. Xenon checks its size and SHA-256 against GitHub's release metadata and checks again before starting setup. The installer remains unsigned: this verification trusts GitHub and the repository maintainers, not an independent publisher certificate.

Finish your website work and choose **Install and exit**, then confirm. Xenon reverifies setup, closes its tabs through normal application shutdown, disconnects its agents and starts setup after releasing its running marker. Close any other Xenon instances too; setup refuses installation while another instance is running. Xenon does not terminate external MCP hosts or adapters. Start Xenon again from the Start menu after installation. Updates require a normal application restart and do not preserve live renderer state as a handoff does. Profiles, saved accounts, grants and pairing configuration remain separate from the application files.

On a fresh installation, setup lets you choose an empty, dedicated folder on a local fixed drive that your Windows account can write to. The default is `%LOCALAPPDATA%\Programs\Xenon Browser`. Updates reuse the recorded installation folder, keeping MCP executable and adapter paths stable. Choosing a different program folder does not move browser data from its separate data directory.

To relocate an existing installation, including alpha.10, stop its MCP adapters, close Xenon and uninstall through Windows **Installed apps**. Uninstall keeps your browser data and pairing files. Run the new installer, choose the new folder, and change the Node and adapter paths in your MCP host to match. Downgrades through the installer are refused because profile migrations may not be reversible.

Portable ZIPs remain available for manual installation. Extract the complete package into a separate folder and keep its matching runtime files together. The Updates window in a portable copy opens setup: a fresh installation offers the folder chooser, while an existing installation is updated in its recorded folder. After moving to an installed copy, use its Start menu shortcut and update your MCP host paths; setup does not automatically replace an older portable folder.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Missing DLL, resources or startup failure | Extract the entire release ZIP again into a new folder. Keep matching EXE, DLLs, locales and resources together. Do not disable the sandbox. |
| MCP connection cannot start | Start Xenon first; check absolute Node/adapter/config paths, the same Windows user, and that the host supports local stdio. Remote-only MCP hosts are outside this release. |
| Pairing waits or times out | Open Controls, select the matching pending request and approve it within five minutes. Use a fresh output filename for a new pairing. |
| Agent cannot see your logged-in page | Its worker may be in another workspace. Share the intended workspace with that client and have it use that workspace's ID. |
| Agent is paused after your input | Release held inputs and finish any website dialog or composition. The agent must wait for current status to clear and observe again; elapsed time alone is insufficient. |
| Agent can read but cannot interact | Check the selected tab's owner. Give control to the authorized connected worker, or let it acquire an unowned tab using fresh control status. |
| A tab will not move to another workspace | An agent controls it, a handoff is pending, or it is a private or protected sign-in tab. Use **Take ownership** in the tab menu or the ownership chip, finish signing in, or wait for the agent's current action. |
| An extension is listed but not running | Its status says **On after restart**: restart Xenon. Extensions never run in private workspaces. |
| Autofill or Save prompt does not appear | Click the key in the address box, or use **Passwords → Fill selected login tab** in Controls, for the correct tab/origin. Unsupported or changed forms need manual handling; use **Save account** or **Import CSV** to populate the vault. |
| Screenshot/observation is protected | Finish authentication yourself. Use **Resume after login** only when no secret is visible. Do not expose a password just to unblock an agent. |
| Website remains signed in after restart/logout | Persistent cookies and an SSO identity-provider session may remain. Use the site's own sign-out flow or a separate workspace for a new session. |
| A tool times out or reports an unknown outcome | Have the agent inspect the retained operation and the page before retrying. A timeout does not mean a click or submission never happened. |
| Too many workers | Retire finished workers; do not repeatedly create replacement workspaces. See the configurable concurrent limit in [BUILD.md](BUILD.md). |
| Client access should stop | Select the client and use **Revoke / retry**. If the native status says revocation was not saved, retry until durable; process-local denial alone may not survive restart. |

Report reproducible issues with version, Windows version and a minimal non-sensitive example. Remove passwords, tokens, personal page data and private paths from reports. See [security reporting](SECURITY.md#maintenance-and-reporting) for vulnerabilities.
