# Using Xenon

Xenon is a Windows x64 browser for human browsing and external MCP agents. This guide describes **0.1.7**; see the [release notes](RELEASE_NOTES.md#017) for what changed. Start with [installation and pairing](GETTING_STARTED.md) if you have not connected a client.

## Browser and Controls

New installations begin with language selection and a quick introduction before the first browser tab opens. Choose English or Simplified Chinese, then follow the tour or skip it. The language takes effect immediately on that first launch. **Menu → Help → Quick tour** reopens the browser introduction, and **Menu → Help → GitHub documentation** opens the getting-started guide in a new Personal tab. Existing profiles do not show the introduction automatically.

The toolbar across the top of each window works like Chrome's and Edge's: **Back**, **Forward** and **Reload** on the left, the address and search box in the middle, then the ownership chip and the **Extensions**, **Controls** and **Menu** (⋮) buttons. Hover over an icon for its label. Inside the address box, the site-information button leads (see below), a key appears when Xenon has saved accounts for the site, and the star bookmarks the page. Tabs are listed in the left sidebar, grouped under workspace names, with **New tab** at the top. Each workspace heading has a **+** that opens a tab in that workspace; pointing at the heading adds a **…** menu beside it. When the list is longer than the window, its scrollbar runs along the sidebar's outer edge, and a newly opened tab scrolls into view. The selected tab joins the page outline. Collapsing a group keeps its tabs and agents running; opening a tab in a collapsed workspace expands it. Open Controls with its toolbar button, **Ctrl+Shift+X**, or **Xenon Controls** in a webpage's context menu. An orange dot on the Controls button means a client is waiting for pairing approval; a blue dot means a Xenon update is available.

Each tab has an **X** to close it; middle-click also closes a tab. **Ctrl+W** closes the selected tab, and **Delete** closes a tab selected in the sidebar. Closing the last tab keeps its workspace and saved website data. Empty saved workspaces remain in the sidebar, including after restart; click one, or select it and press **Enter**, to open a blank human-owned tab. Startup still opens one blank Personal tab. Use the explicit session restore action to reload previously saved pages. **Ctrl+Shift+T** reopens recently closed pages during this run; protected sign-in pages are not remembered.

Press **Enter** in the address box to go; a typed address survives toolbar focus changes. **Ctrl+Enter** completes a single typed word as `www.<word>.com`. The first click into the box selects the whole address, and **Escape** restores the current address.

As you type, a list under the address box suggests pages from the selected tab's workspace history and bookmarks, plus a search for the text with your search engine (Google unless you choose another in **Settings**). Pages are matched by address and title; every typed word must match, and frequently or recently visited pages and bookmarks rank first. A site you have visited completes inline, as in Chrome and Edge: typing `git` can show `github.com` with the added letters selected, so typing continues over them and **Backspace** removes them. Deleting, pasting or undoing never completes. Without a typed `/`, the completion stops at the site name and opens its home page, keeping the scheme and port you visited, such as `http://localhost:3000/`. **Up** and **Down** move through the list and show each row's address, **Enter** or a click opens the highlighted row, and **Shift+Delete** removes every visit to a highlighted history page (its bookmark, if any, stays). With the list open, the first **Escape** closes it and restores what you typed. Private workspaces suggest only their own in-memory history and bookmarks. Matching runs on your device; Xenon sends nothing to a search provider until you open a search, and offers no remote search suggestions or suggestions when the box is empty.

The button at the left end of the address box shows the connection, as in Chrome and Edge: a lock for a secure HTTPS page, an information icon for HTTPS pages with insecure images or for HTTP pages served from this computer (such as `http://localhost`), and a **Not secure** label for other HTTP pages; certificate problems show it in red. Blank pages have no button. Click it, or Tab to it and press **Space**, for the site's information:

- **Connection is secure / not secure** explains the state. For HTTPS it shows who the certificate was issued to and by, its expiry and the protocol, and **Certificate details** opens the Windows certificate viewer.
- **Cookies and site data** opens a list of the site's cookies: name, domain, path, expiry and Secure/HttpOnly flags. Cookie values are never shown. Select cookies and choose **Delete selected**, or choose **Delete all site data…** and confirm to remove the site's cookies and its stored data (local storage, IndexedDB, cache storage and service workers). You may be signed out of the site; reload its pages afterwards.
- Settings you have changed for the site (**Location**, **Camera**, **Microphone**, **Notifications**, **Pop-ups and redirects** and **Sound**) appear with a choice of default, **Allow** or **Block**/**Mute**, and **Reset permissions** returns them all to the defaults.
- **Site settings** opens all six settings for the site, together with **Reset permissions**, **Manage cookies…** and **Delete all site data…**. **Menu → Site settings** opens the same window.

Cookies, site data and site settings belong to the tab's workspace: the same site in another workspace keeps its own. Deleting a site's cookies or data while an agent controls one of its tabs pauses that agent's input briefly, and it must observe the page again. A setting you allow also applies while an agent controls a tab of that site in the workspace. The bubble closes when the tab moves to another site, and is unavailable during a protected sign-in. Background tabs have subtle gray hover feedback, workspace disclosure uses arrowheads, and mouse selection has no dashed focus box. Keyboard navigation draws a rounded focus ring around the focused control. The page outline's top edge doubles as a loading bar. Close buttons on unselected tabs stay visible but quieter until hovered. **Find on page** floats over the page's top-right corner instead of resizing it; **Enter** finds the next match and **Shift+Enter** the previous one.

With the Windows **Animation effects** setting on, buttons and tabs fade on hover, the selected-tab outline glides to a newly chosen tab, the outline color crossfades when control changes, and the address field fades in a focus ring. With it off, or in high contrast, every change is immediate. Only Xenon's own frame animates; the page's size and position never do.

Drag the sidebar's right edge to change its width, or use **Menu → Appearance** for keyboard access to Narrower, Wider and Reset sidebar width. Its range is 180–480 logical pixels, further bounded by space for the page. Double-clicking the edge restores the default. Xenon saves the width alongside your theme preference. Dark mode uses charcoal surfaces with visible gray hover feedback; Light mode uses white surfaces. **System** follows Windows.

### Certificate warnings

When Chromium cannot verify a site's HTTPS certificate (for example, it has expired, was issued for a different address, or comes from an authority Windows does not trust, as with many local development servers), Xenon shows **Your connection is not private** instead of the site, with an error code such as `NET::ERR_CERT_AUTHORITY_INVALID`. Someone on the network may be impersonating the site, so do not sign in or enter information there.

- **Back to safety** leaves the site.
- **Advanced** explains the error and, when the error can be bypassed, offers **Proceed to … (unsafe)**. The site then opens, and the site-information button stays red. Proceed only for a site you control or whose certificate you have checked, such as your own development server. Sites that require HTTPS (HSTS) offer no bypass.
- **Learn more about this warning** opens this section in the same tab. Chromium's page points that link at Google's Chrome Help Center; Xenon replaces the address.
- Chromium's suggestion to turn on Chrome's enhanced protection is hidden. Xenon has no Chrome settings, so its link would do nothing.

### Developer mode

Drag the panel's left edge to adjust its width. Drag the grip above the code/details area to adjust the list/details split. Both sections keep a minimum usable size; the sizes remain for that browser window.

The **Name** column prefers verified rendered text. For an otherwise unlabeled rendered control, the human inspector falls back to its site-provided accessibility name, such as an ARIA label or native button caption. **Label source** distinguishes **Rendered text**, **Site-provided** and **Unlabeled**. Site-provided labels are untrusted website metadata, not verified rendered names; the code/details area records this distinction. They do not become agent observation names or action references. **No verified label** means neither source supplied a label; use **Pick from page** to match the control to its row. Editable search inputs and textareas can have the accessibility role `combobox`; the panel classifies these as **Fill / key**, while dropdown controls show **Select**.

With developer mode enabled, hover over the active website to highlight the matching rendered element and reveal its row automatically. Hovering keeps normal website clicks, scrolling and keyboard focus available. Scrolling refreshes the rendered list and rematches a stationary pointer; matching accounts for the current page scroll offset and rejects a viewport that moves during lookup. For icons or text inside a button/link, matching prefers the nearest listed control among its accessibility or DOM ancestors. The DOM fallback walks at most 32 parents and compares only element identities with the existing rendered list; it does not extract page source, attributes, labels or values. Moving into the inspector leaves the selected row available to read. Selecting a row also highlights its element on the website. Details distinguish document-coordinate bounds from viewport-coordinate highlight bounds. The code/details pane preserves scrolling during refreshes and starts at the top for a newly selected element.

**Pick from page** remains an optional way to select by clicking. Picking consumes the click, so selecting a button does not activate it. **Cancel picking** or Escape exits selection mode. Only elements in the current rendered evidence list can be selected; matching a read-only element also clears the interactable-only filter. Highlights and picking clear when changing tabs/documents, entering protected authentication, or disabling developer mode. Failed highlights are retried after fresh evidence; details show an error when current visibility or geometry cannot be verified. Diagnostic IDs belong only to this native inspector and cannot be used as agent action references.

The details include `pageVisibility`, with counts explaining partial coverage. Simple 2D translations and axis-aligned unit reflections retain Chromium's rendered coordinates. Text labels require the complete transform chain to end upright; canceling flips used by some search result layouts are supported. Rotations, non-unit scaling, perspective and uncertain paint remain omitted. Simple rectangular inset clips are supported. Distant blurred decorations no longer suppress unrelated controls across the page. Controls inside backdrop effects may have a site label even when their rendered text contrast cannot be verified. Human diagnostics can show up to eight short, untrusted computed transform examples under `untrustedUnsupportedTransforms`; these examples are withheld from agent observations. Split inline controls retain their paint ancestry and require a verified center inside an actual fragment.

Open **Menu → Developer mode** to enable it, or open **Controls → Developer mode** and click **Enable developer mode**. A right-side **Agent page elements** panel appears beside the selected website in each browser window. It refreshes the same filtered, rendered-viewport evidence used by agent observations, without taking a screenshot, and adds clearly marked site label metadata for the human inspector. The list shows roles, names, label sources, visibility and possible actions; **Interactable only** filters to controls, including uncertain candidates. **Verified visible** marks checked geometry; **Uncertain — check screenshot** marks a control whose visibility could not be proved. Uncertain rows show their reason and available verified text, with **Check screenshot** instead of an actionable target. Selecting or hovering an uncertain control shows a dashed amber outline of its current reported position. The outline is approximate and does not prove visibility or make the control actionable; verified geometry retains a solid blue outline. Known hidden controls, editable values and credentials remain excluded. Select a row for its bounds, frame and state details. With no selection, details show page visibility omissions and bounded untrusted examples of unsupported paint effects, including when the list is empty. Hidden elements and editable values are omitted. Private/internal tabs, protected authentication, held input and dialogs withhold evidence. Coverage may be partial or truncated at 1,000 elements; scroll the page to inspect another viewport. Diagnostic reads do not replace agent observation tokens or grant control: actions still require permissions and fresh agent evidence. Disabling developer mode clears and hides the side panel.

New agent requests appear in the Controls command trace, newest first. Select one to see its connection, worker/tab IDs, request parameters, pending/completed/error status, elapsed time, and broker response. Observations include the rendered evidence returned to the agent; `page.inspect` and `page.screenshot` responses also show their returned PNG when it fits the preview limit. This is captured evidence from that command, not a live video feed or the external model's reasoning.

Command-trace capture starts disabled each time Xenon runs. **Clear trace** erases the current history; **Disable developer mode** erases it and stops trace capture. The command trace remains in memory and is never written to the profile or exposed through MCP. Pairing tokens, password/secret fields, and command input values are redacted; rendered page evidence remains visible. History retains at most 128 commands and 8 MiB total, with a 2 MiB entry limit and 1 MiB of encoded PNG per preview. Oversized entries are marked truncated, and older commands are evicted. Clearing or disabling also discards replies from previously captured commands that finish later.

For filter diagnostics, open **Controls → Developer mode → Collect uncertain agent clicks**. This separate option is off after every restart. It appends local JSONL records to `visibility-diagnostics/uncertain-clicks.jsonl` under Xenon's data directory. **Open log folder** reveals the file; **Stop collecting uncertain clicks** stops new records but retains the file, and **Clear click log** empties it. Collection stops at 8 MiB until you clear the log. It does not upload anything.

Records cover primary agent coordinate clicks using fresh screenshot evidence and uncertain candidates from a matching viewport observation. Chromium's hit-test identity and accessibility ancestry must match the candidate; overlapping reported bounds alone do not qualify. Records include the filtering reason, role/tag, safe numeric/boolean filter diagnostics, bounds, viewport, click position, timestamp and product version. URLs, labels, DOM/CSS source, input values, screenshots, candidate IDs and credentials are excluded. Private tabs and protected authentication are excluded. Collection is independent of command-trace capture; stop it explicitly when finished.

A record means mouse-down and mouse-up were dispatched, not that the website accepted the action. Failed or interrupted gestures, changed documents, stale viewports, hover gestures and ordinary verified-target clicks do not produce records. Collection does not capture physical human clicks, prove a filtering false negative or automatically change filtering rules. Logs are intentionally partial when observation limits, queries or unavailable frame ancestry omit a candidate.

### The ownership chip

Left of the Extensions button, a dot in the page outline's color and a short label show who controls the selected tab: **You**, **No agent**, or the agent's name with its state. Click the chip to **Take ownership**, to give a tab you own to a connected agent working in that workspace, or to open Controls. When the tab's agent has asked for your help, the chip adds **Needs your help** and its menu shows the agent's note and **Dismiss help request**. Short status messages, such as a saved bookmark or a refused action, appear in the chip for a few seconds.

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

### Full screen

**F11** (or **Menu → Full screen**) hides Xenon's toolbar and sidebar so the page fills the screen; **F11** again returns. When a website you control asks for full screen, such as a video player's full-screen button, the page takes over the whole screen and a notice names the site; **Esc** ends it, as does switching tabs. A page in a tab an agent controls only fills its own tab area and never takes over the screen, and a page's full screen ends if an agent becomes available in that tab. While an agent is available in the selected tab, full screen keeps a thin outline in its ownership color.

### Site icons and links for other apps

Tabs show each website's icon. Pages without one show a globe.

When a website opens a link meant for another app, such as `mailto:` for your mail program or a meeting link, Xenon asks first in a small window named after the site, the app Windows has registered for that kind of link and the link itself. **Open app** passes the link to that app; **Cancel** does nothing. Only the page you are looking at can ask, and only when you control the tab or just clicked or typed in it; a link an agent's action caused is blocked, and the ownership chip says so. Links for kinds Windows uses to run programs, files, searches or diagnostics (`file:`, `shell:`, `ms-msdt:`, `search-ms:` and similar) are never offered. If no app is registered, the chip says so instead.

### Screen sharing

Video-call sites can share your screen after you allow it in a **Website permission request** window. Xenon cannot share a single window or tab: the site sees everything on all of your displays, including other apps and notifications, until sharing stops. The request appears only for the tab you are looking at, after your own click in a tab you control or in a tab where you just clicked; an agent's click cannot start it. A tab that is sharing shows a red screen icon (a red dot means a camera or microphone is in use), and agents get no screenshots of it while it shares. Stop sharing with the site's own control or the tab menu's **Stop sharing your screen (reloads the page)**.

### Bookmarks and history

**Menu → Bookmarks** (**Ctrl+Shift+O**) opens the bookmark manager for the selected tab's workspace. Folders are on the left; choose one to see its folders and bookmarks. **New folder** adds one inside the current folder and lets you name it; **Rename** (**F2**) renames the selected folder or bookmark; **Move to…** moves selected bookmarks and folders into another folder; **Delete** removes them, asking first when a folder still contains bookmarks. The search box finds bookmarks in every folder. **Open** (or a double-click) opens bookmarks in new tabs of the workspace. The star and **Ctrl+D** save new bookmarks at the top level.

**Menu → History** (**Ctrl+H**) lists the workspace's visits, newest first, with the time of each. Type in the search box to find pages by title or address; every word must match. Select visits and choose **Delete** (or press **Delete**) to remove exactly those visits, or **Open** to open them in new tabs.

### Clear browsing data

**Menu → Clear browsing data…** (**Ctrl+Shift+Del**), the History window or **Settings** clears data for the selected tab's workspace. Choose a time range, then any of: **Browsing history** and **Cookies** for that range, and, for all time, **Other site data** (local storage, IndexedDB, cache storage and service workers of the sites in the workspace's history and open tabs) and **Cached images and files**. Clearing cookies can sign you out of websites. Saved passwords, bookmarks and downloads are kept. Agents working in the workspace pause and must observe their pages again.

### Import from another browser

**Menu → Import bookmarks and history…**, or **Import…** in the bookmark manager, adds bookmarks and history to the selected tab's workspace. Choose a profile of Google Chrome, Microsoft Edge, Brave, Vivaldi or Chromium found on this computer, or **Bookmarks HTML file…** for a file exported from any browser (Firefox and Safari included). Bookmarks arrive in a new folder named after the source, keeping its folders; pages you have already bookmarked or visited are not added again. History is copied from the other browser's profile; if it cannot be read, close that browser and try again. Passwords, cookies and settings are never read. To bring passwords, export them from the other browser as a CSV file and use **Import CSV** in Controls → Passwords.

### Settings and default browser

**Menu → Settings** chooses the search engine for the address box (Google, Bing, DuckDuckGo, Brave Search, Ecosia, Startpage or Baidu), the theme and the language, and links to import, Clear browsing data and passwords. Under **Agents**, **Show a Windows notification when an agent needs your help** is off until you turn it on; see [When an agent needs your help](#when-an-agent-needs-your-help).

The installer registers Xenon with Windows as a web browser for `http` and `https` links. **Settings → Make Xenon your default browser…** opens Windows' **Default apps** settings for Xenon, where only you can choose it; Settings then shows whether Xenon is your default. A portable copy is not registered. Links that Windows or other apps open while Xenon runs appear in a new tab of the Personal workspace; if Xenon is closed, it starts with the link in its first tab.

### Keyboard shortcuts

| Keys | Action |
| --- | --- |
| **Ctrl+T**, **Ctrl+N**, **Ctrl+Shift+N** | New tab, new window, new private workspace |
| **Ctrl+W** or **Ctrl+F4**, **Ctrl+Shift+W** | Close tab, close window |
| **Ctrl+Shift+T** | Reopen closed tab |
| **Ctrl+Tab**, **Ctrl+Shift+Tab**, **Ctrl+PgDn**, **Ctrl+PgUp** | Next or previous tab in this window |
| **Ctrl+1**–**Ctrl+8**, **Ctrl+9** | Select that tab, or the last tab |
| **Ctrl+L**, **Alt+D**, **F6** | Focus the address box |
| **Up**, **Down**, **Shift+Delete** in the address box | Move through suggestions; remove a highlighted history suggestion |
| **Ctrl+R** or **F5**; **Ctrl+Shift+R** or **Ctrl+F5** | Reload; reload bypassing the cache |
| **Alt+Left**, **Alt+Right** | Back, forward |
| **Ctrl+F**, **F3**, **Shift+F3** | Find, next match, previous match |
| **Ctrl+D**, **Ctrl+H**, **Ctrl+J**, **Ctrl+P** | Bookmark, history, downloads, print |
| **Ctrl+Shift+O**, **Ctrl+Shift+Del** | Bookmark manager, Clear browsing data |
| **Ctrl++**, **Ctrl+-**, **Ctrl+0** | Zoom in, out, reset |
| **F11**; **Esc** | Full screen; end a website's full screen |
| **Ctrl+Shift+X** | Controls |

Tab-switching and focus shortcuts do not count as page input, so they do not pause an agent. Right-clicking a link in a page also offers **Open link in new tab**, **Open link in new window** and **Copy link address**; new tabs opened this way are yours and stay in the link's workspace.

Controls is a separate native window with sections in a left sidebar, rounded buttons and neutral gray list selections. Website pages and MCP clients cannot click its permission or credential buttons. Closing it hides it; the browser keeps running.

Controls has **Clients**, **Workspaces**, **Passwords** and **Extensions** sections. Pairing requests have separate Approve/Deny actions. Approved clients remain paired across launches and start read-only with automatic creation disabled, four concurrent workers and four automatic workspaces. Client configuration sets the overall ceiling; workspace configuration can narrow it. Legacy pairings retain their existing capabilities until configured. Lowering a quota blocks additional admission while allowing existing work to finish.

The Xenon menu supplies new tabs and windows, history, Clear browsing data, downloads, bookmarks, import, passwords, extensions, find, zoom, full screen, printing/PDF, site settings, **Settings** and, under **Help**, the tour, updates and third-party notices. Under **Appearance**, choose **Theme: System**, **Theme: Light** or **Theme: Dark**; System is the default. Light and Dark also set the color scheme websites receive, including blank pages; System follows Windows. Xenon's own window changes immediately, but web pages use a newly chosen theme after Xenon restarts. Native bookmarks and history migrate existing workspace records without modifying their original files. Private metadata stays in memory, and new protected-authentication visits are excluded from history. There is no cloud sync or built-in model. Use Xenon's vault for passwords.

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

The selected tab and page use teal borders for an available agent owner, orange for its temporary pause for human page input, and gray for human ownership or no available agent. Background tabs only show a teal border while their agent owner is available and unpaused; paused or unowned background tabs have no border. A tab whose agent asked for your help pulses orange instead (see [When an agent needs your help](#when-an-agent-needs-your-help)). Status text and protection labels accompany the colors. The native agent cursor stays visible while the selected page is agent-owned, parks during human input, and resumes without sending extra website input or moving the Windows mouse.

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

### When an agent needs your help

An agent can ask you to come to a tab it controls, for example to solve a CAPTCHA, finish MFA, use a passkey or approve something only you should decide. The tab's border pulses orange in the sidebar; inside a collapsed workspace, the workspace row pulses instead. If that window is in the background, its taskbar button flashes. The pulse stops once you have been at the tab: it is selected in the window you are using and you move the mouse or type. The tab then keeps a steady orange outline until the agent withdraws the request. With Windows animation effects off, the outline is steady orange from the start.

Turn on **Settings → Agents → Show a Windows notification when an agent needs your help** to also get a Windows notification when you are not in that Xenon window. It names the agent, quotes its note and shows the tab's site, never the page title. Select it to bring Xenon forward on that tab. Each tab notifies at most once a minute, and Windows Focus assist and notification settings still apply.

The note is the agent's own words, not Xenon's or the website's, and an agent can be wrong or misled by a page. Never type a password or code somewhere just because a note asks for it; use the site as you normally would. Asking for help does not pause the agent or change ownership; your page input pauses it as usual. **Dismiss help request** in the tab's right-click menu or the ownership chip withdraws a request; the agent can ask again. Taking ownership, closing the tab or the agent disconnecting also ends it.

## Saved accounts

Saved accounts live in one native vault per Xenon data directory, shared across workspaces. Website cookies live separately inside each workspace. Removing or updating a vault account affects future fills but does not sign out existing website sessions.

Usernames and passwords are encrypted with Windows DPAPI for your Windows account. Account IDs, HTTPS origins and human labels are metadata. Avoid putting secrets in labels. This does not protect against malware running as your Windows user, and a website receiving a password can read or misuse it.

### Save an account

There are three entry paths:

1. **Native Save/Update bubble.** After you type and submit a supported login, Xenon can offer to save that submitted password in a small bubble under the toolbar's key, as Chrome does. It shows the original login origin and a masked username, never the password. A new account joins the Default group unless you tick **Sensitive**. Click **Save** or **Update** only if the password is correct; the offer is not proof that login succeeded. **Not now** or **Escape** discards it.
2. **Save account in Controls.** Enter the HTTPS origin, username, password and an optional recognizable label. Tick **Sensitive account** if needed, then click **Save account**.
3. **Import CSV.** Export passwords using your existing browser, then select **Import CSV** in Controls. Review the masked preview, choose the group for newly imported accounts (Sensitive is preselected), and choose whether to replace or skip conflicts.

Updating a saved password keeps its group.

CSV import supports the exported formats used by Chrome, Edge and Firefox for supported HTTPS logins. It does not copy a browser's live profile, cookies, bookmarks or all settings. The exported CSV contains plaintext passwords; Xenon leaves it on disk, so remove it when finished and avoid shared folders.

Automatic Save/Update covers top-level HTTPS sign-in with one current password. A conventional form posting to its own site is offered when you submit it. Sign-in pages that work through script, such as Google's two-step page, are offered after you press **Enter** or the sign-in button and the page moves on (it navigates or removes the password box); if the password box stays, Xenon assumes the attempt failed and offers nothing. When the password step does not show the account, Xenon uses the account you typed on the previous step of the same site in that tab. Private workspaces, agent-filled logins, registration/new-password fields, OTP fields, ambiguous and embedded cross-site forms are excluded. Use manual entry/import for unsupported forms.

The encrypted pending offer stays bound to the original login origin through HTTPS SSO redirects and ordinary MFA input. It expires after ten minutes; lock, source-tab closure, non-HTTPS navigation, agent mutation or a new qualifying password edit cancels it. Unchanged credentials do not generate another prompt. Pending offers are not exposed to MCP. Saving creates no individual agent grant; a Default account is available only to clients and workspaces you allowed all Default passwords.

### Fill an account yourself

On a supported login page:

1. Click a username or password field. Xenon shows its saved accounts for that exact HTTPS origin in a list directly under the field, as Chrome and Edge do, each time you click the field while the password is still empty. You can also click the key in the address box, or select the login tab in Controls' Workspaces section and choose **Passwords → Fill selected login tab**.
2. Click an account, or use **Up**/**Down** and **Enter**. Clicks in the first half second after the list appears are ignored so a click meant for the page cannot choose an account. **Escape**, typing or clicking elsewhere closes the list.
3. Continue on the website yourself. Native human fill does not submit the form.

Each account row also shows its agent access: **Agents: no access**, or the names of paired clients that can currently use that account in this workspace, through an individual grant or the Default group. An agent with access gets only an opaque account handle; it never receives the username or password, which Xenon fills natively when the agent requests sign-in (see below). **Manage passwords…** and **Agent access…** at the bottom of the list open Controls' Passwords section with this site's account selected. If the page has no fillable form, the key opens that section instead.

Ordinary two-field forms need one editable username and an empty password. Separate username/password steps need explicit `username`/`current-password` autocomplete hints. After filling only the username, continue on the website and use **Fill selected login tab** again at the password step.

Xenon refuses ambiguous or covered fields, changed forms, conflicting usernames, nonempty passwords, read-only inputs and unsupported controls. It preserves existing human values. Offers expire after two minutes and can become stale when the page or your input changes; request a fresh offer.

Human fill needs no agent account grant and creates none. It pauses agent effects, cancels queued mutations and protects page evidence while retaining the tab's owner. An existing field becoming populated does not establish successful authentication.

### Let an agent use an account

This is the agent handle on a saved account. Pair the client and share the intended workspace first, and enable saved-account use in both the client and workspace configuration.

Saved accounts are in one of two groups, shown in **Passwords**:

- **Default** accounts are granted as a group. In **Clients → Configure**, tick **Allow all Default passwords** to let that client use every Default account in workspaces that inherit client accounts (automatic workspaces do). In **Workspaces → Configure**, the same option grants the Default group to the selected client in that workspace only. New logins you save as Default are then available to those agents without another grant.
- **Sensitive** accounts are granted one at a time. Choose the client and workspace in their sections, then select the saved account in **Passwords** and choose **Grant in workspace**, or **Grant to client** for an inheritable client-level resource.

Select an account and choose **Make sensitive** or **Make default** to move it. Moving an account to Sensitive takes away group access at once while keeping any individual grants. Accounts saved before groups existed start as Sensitive. A workspace's **Restrict accounts to checked items** limits both kinds. Verify the exact HTTPS origin; subdomains and different ports are different origins. Keep banking, email, administrator and other high-impact accounts Sensitive: a Default-group grant lets that agent sign in to every Default account's site.

The agent receives an opaque account ID, origin and label through `xenon_accounts`. It requests protected sign-in with `xenon_login`; the vault does not return the username or password through MCP. The agent path can fill and submit supported same-origin POST forms, including a bounded username-first flow. Pairing, workspace sharing and human autofill do not add account grants; saving adds none beyond an existing Default-group grant.

Protected login withholds detailed observations and screenshots. If protection remains after you finish signing in, select the tab and click **Resume after login** only when no password, code or other secret is visible. The agent then needs fresh evidence.

### MFA and compatibility

Complete MFA, CAPTCHA, passkeys, recovery-code prompts, cross-origin embedded login and unusual forms yourself. Passkeys use Windows Hello, a security key or your phone, and only you can start them. In a tab an agent is using, click the site's passkey button yourself; no handoff is needed, and the agent stays paused until the Windows prompt closes. Passkey requests caused by an agent are refused, and passkey suggestions in sign-in fields are off in tabs you don't control. Reuse the same workspace when a service remembers your browser; a new workspace starts a separate session. Expiry, device trust and organization policies remain controlled by the service. An unsigned build may not meet a provider's approved-browser requirements.

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
