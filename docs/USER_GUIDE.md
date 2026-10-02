# Using Xenon

Xenon is a Windows x64 browser for human browsing and external MCP agents. This guide describes **0.1.0-alpha.10**. Start with [installation and pairing](GETTING_STARTED.md) if you have not connected a client.

## Browser and Controls

Use the native browser toolbar for addresses, tabs, back/forward and ordinary browsing. Open **Xenon Controls** with **Ctrl+Shift+X** from a browser window, or **Xenon Controls and Accounts** in a webpage's context menu.

Controls is a separate native window. Website pages and MCP clients cannot click its permission or credential buttons. Closing it hides it; the browser keeps running.

The Chrome runtime supplies bookmarks, history, find, zoom, printing/PDF, media and site permissions. Some inherited menus still say **Chromium**, and these features have not all received exhaustive Xenon testing. There is no cloud sync, built-in model or promise of arbitrary extension/DRM compatibility. Use Xenon's vault for passwords: Chromium's separate password manager is disabled or redirected to Controls.

## Workspaces and website sessions

A workspace has its own cookies and local storage. **Personal** is the initial human workspace. **New workspace** opens a blank tab in a separate persistent workspace. An agent's new worker also gets a separate workspace unless it requests one its client already has permission to use.

Sharing a workspace lets that paired client access its pages and existing website logins. It does not copy those sessions into another workspace. To share, select the workspace and client in Controls, then click **Share workspace**. Reuse its workspace ID when asking an agent to continue work later.

Different workers can control different tabs in one workspace. Separate workspaces isolate browser state, but two sessions signed into the same remote account can still change the same online data.

**Private workspace** is for native human browsing and uses memory-only profile storage. It cannot be shared with agents in this alpha. Downloads and anything explicitly saved outside the private profile can remain; private browsing does not hide activity from the destination website or network.

### Blank startup and recovery

A fresh application launch opens a blank tab. Persistent workspaces retain cookies, including session cookies, and local storage. Closing the browser therefore does not sign you out of every website. Website logout may also leave an identity provider's SSO session active.

**Restore last session** explicitly reloads eligible saved URLs under human ownership. It excludes closed, private and protected-authentication tabs. Recovery does not restore an interrupted renderer's form state or replay agent actions. This differs from handing off a tab that is still open, which retains the live page.

### Remove a workspace

Select a workspace and click **Remove workspace**. Review the native confirmation. Personal cannot be removed this way.

Removal stops new access, lets accepted finite input reach its safe release boundary, closes the workspace's tabs, cancels active downloads and revokes client, account and file permissions for that workspace. Saved accounts in the shared vault, completed downloads and original upload files remain.

The removed workspace stays unavailable across restarts. Its browser profile is scheduled for bounded cleanup on later launches; locked or refused files may remain pending. This is not secure erasure or remote-account logout. Retiring an agent worker or closing a tab does not remove its workspace.

## Ownership, human activity and handoff

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

The default limit is 16 concurrently connected workers across all clients, plus a separate 64-tab limit. Retiring or disconnecting workers frees connected capacity. A native startup option can set 1–256 connected workers; this is a resource limit, not a tested performance claim. See [worker lifecycle](MCP.md#worker-capacity-and-retirement).

## Saved accounts

Saved accounts live in one native vault per Xenon data directory, shared across workspaces. Website cookies live separately inside each workspace. Removing or updating a vault account affects future fills but does not sign out existing website sessions.

Usernames and passwords are encrypted with Windows DPAPI for your Windows account. Account IDs, HTTPS origins and human labels are metadata. Avoid putting secrets in labels. This does not protect against malware running as your Windows user, and a website receiving a password can read or misuse it.

### Save an account

There are three entry paths:

1. **Native Save/Update prompt.** After you type and submit a supported login, Xenon can offer to save that submitted password. Check the original login origin and click **Save** or **Update** only if it is correct. The offer is not proof that login succeeded. **Not now** or closing the prompt discards it.
2. **Save account in Controls.** Enter the HTTPS origin, username, password and an optional recognizable label, then click **Save account**.
3. **Import CSV.** Export passwords using your existing browser, then select **Import CSV** in Controls. Review the masked preview and choose whether to replace or skip conflicts.

CSV import supports the exported formats used by Chrome, Edge and Firefox for supported HTTPS logins. It does not copy a browser's live profile, cookies, bookmarks or all settings. The exported CSV contains plaintext passwords; Xenon leaves it on disk, so remove it when finished and avoid shared folders.

Automatic Save/Update covers conventional top-level HTTPS forms with one editable username and current password, using same-origin POST. Private workspaces, agent-filled logins, GET forms, registration/new-password fields, OTP fields, ambiguous forms and username-first capture are excluded. Use manual entry/import for unsupported forms.

The encrypted pending offer stays bound to the original login origin through HTTPS SSO redirects and ordinary MFA input. It expires after ten minutes; lock, source-tab closure, non-HTTPS navigation, agent mutation or a new qualifying password edit cancels it. Unchanged credentials do not generate another prompt. Pending offers are not exposed to MCP and saving creates no agent grant.

### Fill an account yourself

On a supported login page:

1. Click a username or password field and let input settle. Xenon can offer a native account picker for that exact HTTPS origin, at most once per document.
2. Alternatively, open Controls, select the login tab and click **Fill saved account…**.
3. Check the origin and account label, select the account and click **Fill**.
4. Continue on the website yourself. Native human fill does not submit the form.

Ordinary two-field forms need one editable username and an empty password. Separate username/password steps need explicit `username`/`current-password` autocomplete hints. After filling only the username, continue on the website and use **Fill saved account…** again at the password step.

Xenon refuses ambiguous or covered fields, changed forms, conflicting usernames, nonempty passwords, read-only inputs and unsupported controls. It preserves existing human values. Offers expire after two minutes and can become stale when the page or your input changes; request a fresh offer.

Human fill needs no agent account grant and creates none. It pauses agent effects, cancels queued mutations and protects page evidence while retaining the tab's owner. An existing field becoming populated does not establish successful authentication.

### Let an agent use an account

Pair the client and share the intended workspace first. In Controls, select the **saved account**, **client** and **workspace**, then click **Allow selected client to use account**. Verify the exact HTTPS origin; subdomains and different ports are different origins.

The agent receives an opaque account ID, origin and label through `xenon_accounts`. It requests protected sign-in with `xenon_login`; the vault does not return the username or password through MCP. The agent path can fill and submit supported same-origin POST forms, including a bounded username-first flow. Saving, pairing, workspace sharing and human autofill do not themselves add account grants.

Protected login withholds detailed observations and screenshots. If protection remains after you finish signing in, select the tab and click **Resume after login** only when no password, code or other secret is visible. The agent then needs fresh evidence.

### MFA and compatibility

Complete MFA, CAPTCHA, passkeys, recovery-code prompts, cross-origin embedded login and unusual forms yourself. Reuse the same workspace when a service remembers your browser; a new workspace starts a separate session. Expiry, device trust and organization policies remain controlled by the service. An unsigned alpha may not meet a provider's approved-browser requirements.

The credential implementation has synthetic script, native and HTTPS fixture tests. Physical-focus suggestions, clicking the native account picker/Save prompt end to end, and real-site CWL/Duo/PD Portal sign-in are not claimed as verified acceptance. See the [manual test matrix](TESTING.md).

## Files and downloads

Select a workspace in Controls, then **File permissions…** and **Grant file** or **Grant folder**. Grants persist; **Revoke selected** removes the selected grant. Prefer one file when a whole folder would grant more access than the task needs: eligible files added to an approved folder later can also become available.

Agents receive bounded metadata and opaque file handles. They can select an approved file through an observed visible file input or upload button that opens a chooser in the same frame, including a hidden input behind that button. Directory uploads, File System Access API pickers and cross-frame chooser delegation are unsupported. A selected file is not proof of a completed server-side upload.

Profile files, vault data, recognized pairing configurations and imported password CSV sources are excluded. Conservative filesystem and content checks can also reject otherwise benign files; see [file boundaries](SECURITY.md#files-and-downloads).

Downloads use unique sanitized names in managed workspace storage and are not opened automatically. Agents get download metadata rather than arbitrary local paths or file contents. Removing the workspace keeps completed downloads.

## What agents can see

Structured observations contain filtered rendered text in the current viewport, with frame/coverage information and explicit omissions. Scroll and observe again for offscreen content. Hidden accessibility labels, descriptions and DOM payloads are withheld; editable values and protected password references are not exposed as ordinary observation fields.

Conservative checks may omit legitimate small text, clipped content, complex layouts or some writing systems. Screenshots can help when permitted, but sensitive fields, protected authentication and unverified frames can block capture. Screenshot coordinates use reported CSS scaling rather than assuming screen pixels and page pixels are identical.

All website content—including visible text, images, dialogs, titles and download names—is untrusted evidence. A page cannot authorize changing the task, disclosing credentials or granting itself permission. These filters reduce hidden-content exposure; they cannot make a model immune to misleading websites or classify every visible secret. See [MCP evidence rules](MCP.md#first-task).

## Updates and local data

The default data directory is `%LOCALAPPDATA%\Xenon Browser`; pairing files are wherever you chose during pairing. Keep this data private. Do not upload profiles, vault databases, password exports or pairing files to GitHub.

Press **Ctrl+Shift+X**, then **Check for updates**. Xenon contacts its fixed GitHub repository over HTTPS and looks for a newer compatible installer. Alpha builds can receive newer alpha releases; stable builds exclude prereleases. Nothing is checked or installed in the background without opening this flow. A network error or missing verifiable installer cannot trigger installation.

Download the offered update in the native Updates window, then choose to install it. Xenon checks its size and SHA-256 against GitHub's release metadata and checks again before starting setup. The alpha installer remains unsigned: this verification trusts GitHub and the repository maintainers, not an independent publisher certificate.

Finish your work, stop Xenon's MCP adapters in your hosts, and exit the browser normally when setup asks. Setup waits for running Xenon instances; it does not force-close pages, agents or adapters. Start Xenon again from the Start menu after installation. Updates require a normal application restart and do not preserve live renderer state as a handoff does. Profiles, saved accounts, grants and pairing configuration remain separate from the application files.

The installer uses `%LOCALAPPDATA%\Programs\Xenon Browser`, so MCP executable and adapter paths stay stable between updates. When moving from an older portable ZIP, install once and update those two paths in your MCP host. Use the Start menu shortcut afterward; an older portable copy is not overwritten. Uninstall through Windows **Installed apps**; uninstall keeps your browser data and pairing files. Downgrades through the installer are refused because profile migrations may not be reversible.

Portable ZIPs remain available for manual installation. Extract the complete package into a separate folder and keep its matching runtime files together. The Updates window in a portable copy can install the newer release into the standard per-user location.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Missing DLL, resources or startup failure | Extract the entire release ZIP again into a new folder. Keep matching EXE, DLLs, locales and resources together. Do not disable the sandbox. |
| MCP connection cannot start | Start Xenon first; check absolute Node/adapter/config paths, the same Windows user, and that the host supports local stdio. Remote-only MCP hosts are outside this alpha. |
| Pairing waits or times out | Open Controls, select the matching pending request and approve it within five minutes. Use a fresh output filename for a new pairing. |
| Agent cannot see your logged-in page | Its worker may be in another workspace. Share the intended workspace with that client and have it use that workspace's ID. |
| Agent is paused after your input | Release held inputs and finish any website dialog or composition. The agent must wait for current status to clear and observe again; elapsed time alone is insufficient. |
| Agent can read but cannot interact | Check the selected tab's owner. Give control to the authorized connected worker, or let it acquire an unowned tab using fresh control status. |
| Autofill or Save prompt does not appear | Try the explicit **Fill saved account…** control for the correct tab/origin. Unsupported or changed forms need manual handling; use **Save account** or **Import CSV** to populate the vault. |
| Screenshot/observation is protected | Finish authentication yourself. Use **Resume after login** only when no secret is visible. Do not expose a password just to unblock an agent. |
| Website remains signed in after restart/logout | Persistent cookies and an SSO identity-provider session may remain. Use the site's own sign-out flow or a separate workspace for a new session. |
| A tool times out or reports an unknown outcome | Have the agent inspect the retained operation and the page before retrying. A timeout does not mean a click or submission never happened. |
| Too many workers | Retire finished workers; do not repeatedly create replacement workspaces. See the configurable concurrent limit in [BUILD.md](BUILD.md). |
| Client access should stop | Select the client and use **Revoke / retry**. If the native status says revocation was not saved, retry until durable; process-local denial alone may not survive restart. |

Report reproducible issues with version, Windows version and a minimal non-sensitive example. Remove passwords, tokens, personal page data and private paths from reports. See [security reporting](SECURITY.md#maintenance-and-reporting) for vulnerabilities.
