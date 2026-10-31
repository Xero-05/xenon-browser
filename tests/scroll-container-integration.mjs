// Real MCP + production CEF against synthetic loopback application forms.
// Lower-page fields must receive verified refs after the page scrolls, whether
// the window or an inner overflow container scrolls, behind sticky headers,
// and on large documents. Withholding cases are checked in the same run.
// --diagnose records per-scroll omission counts without asserting acceptance.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..');
const buildRoot = resolve(root, process.env.XENON_TEST_BUILD_DIR ?? 'build');
const run = 'scroll-container-' + Date.now() + '-' + randomBytes(4).toString('hex');
const profile = resolve(root, '.cache', 'auth-integration-' + run);
const pipeName = 'xenon-test-' + run, slash = String.fromCharCode(92), pipe = slash + slash + '.' + slash + 'pipe' + slash + pipeName;
const binary = resolve(buildRoot, 'app/Release/Xenon.exe'), dll = resolve(buildRoot, 'app/Release/Xenon.dll');
const diagnose = process.argv.includes('--diagnose');
const workspaceId = 'auth_fixture_shared';
const results = [], calls = [], servers = [], states = new Map(), scrollTraces = {};
const runFile = promisify(execFile), sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const hashDll = async () => sha256(await readFile(dll));
let browser, client, worker, fileId, folderId, origin, applicationDllSha256, applicationDllSha256AtEnd, lastObservation;

async function until(action, timeout = 15000, label = 'Fixture condition') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) { const value = await action(); if (value) return value; await sleep(100); }
  throw new Error(label + ' timed out');
}
async function check(name, action) {
  const started = Date.now();
  try { const details = await action(); results.push({ name, passed: true, elapsedMs: Date.now() - started, details }); console.log('PASS ' + name); }
  catch (error) { results.push({ name, passed: false, elapsedMs: Date.now() - started, error: error.message }); console.log('FAIL ' + name + ': ' + error.message); if (!diagnose) throw error; }
}
async function raw(name, args = {}) {
  const started = Date.now();
  const response = await client.callTool({ name: 'xenon_' + name, arguments: args });
  calls.push({ name: 'xenon_' + name, args, elapsedMs: Date.now() - started, isError: !!response.isError,
    // Keep logs small: node lists are summarized, never website payloads beyond synthetic names.
    result: name === 'observe' || name === 'inspect' ? summarize(response.structuredContent) : response.structuredContent });
  return response;
}
async function tool(name, args = {}) {
  const response = await raw(name, args);
  assert(!response.isError, name + ': ' + JSON.stringify(response.structuredContent ?? response.content)); return response.structuredContent;
}
const summarize = value => value?.nodes ? { nodes: value.nodes.length, refs: value.nodes.filter(node => node.ref).length, viewport: value.viewport,
  coverage: value.coverage, truncated: value.truncated, omitted: value.visibility?.omitted, error: value.error } : value;
const workerScope = value => ({ agentSessionId: value.agentSessionId, workspaceId: value.workspaceId });
const scope = tab => ({ ...workerScope(tab), tabId: tab.tabId });
const mutation = tab => ({ ...scope(tab), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
async function observe(tab, method = 'observe') {
  const deadline = Date.now() + 5000;
  for (;;) {
    const response = await raw(method, { ...scope(tab), maxNodes: 1000 }); lastObservation = response;
    if (response.isError && ['privacy_guard_initializing', 'page_changed', 'inspection_changed'].includes(response.structuredContent?.error?.code) && Date.now() < deadline) { await sleep(100); continue; }
    assert(!response.isError, method + ': ' + JSON.stringify(response.structuredContent ?? response.content)); return response.structuredContent;
  }
}
const scrollOffset = caseId => states.get(caseId)?.scrollTop ?? 0;
const refNode = (observation, name, tag) => observation.nodes.find(node => node.name === name && node.ref && (!tag || node.tag === tag));
// Scroll with the page's ordinary wheel gesture until the named control has a
// verified ref. The trace records what each observation proved.
async function scrollTo(tab, name, tag, limit = 30) {
  const trace = (scrollTraces[tab.caseId] ??= []);
  let observation = await observe(tab);
  for (let index = 0; index <= limit; index++) {
    const node = refNode(observation, name, tag);
    trace.push({ target: name, scrollTop: scrollOffset(tab.caseId), ...summarize(observation), found: !!node });
    if (node) return { observation, node };
    const before = scrollOffset(tab.caseId);
    await tool('interact', { ...mutation(tab), observationId: observation.observationId, action: 'scroll', direction: 'down', amount: 300 });
    await until(() => scrollOffset(tab.caseId) !== before, 3000).catch(() => {});
    await sleep(150); observation = await observe(tab);
    if (scrollOffset(tab.caseId) === before && index > 0) { trace.push({ target: name, stalledAt: before, ...summarize(observation) }); break; }
  }
  throw new Error('No verified ref for rendered ' + name + ' after scrolling: ' + JSON.stringify(trace.slice(-3)));
}
const MenuZoomIn = 2101, MenuZoomReset = 2103;
// Posts window messages to this run's own browser process only.
const shell = (script, ...args) => runFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
  resolve(root, 'tests/fixtures', script), '-ProcessId', String(browser.pid), ...args], { windowsHide: true });
async function settleZoom(tab, base, factor) {
  return until(async () => {
    const response = await raw('observe', { ...scope(tab), maxNodes: 20 });
    if (response.isError) return false;
    const { viewport } = response.structuredContent;
    if (['width', 'height'].some(axis => Math.abs(base[axis] / viewport[axis] - factor) > 0.04)) return false;
    return !(await tool('control_status', scope(tab))).humanPaused && viewport;
  }, 15000, 'Browser zoom ' + factor);
}
const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipe); socket.once('connect', () => { socket.destroy(); resolve(true); }); socket.once('error', () => { socket.destroy(); resolve(false); });
});
async function launch() {
  browser = spawn(binary, ['--user-data-dir=' + profile, '--broker-pipe=' + pipeName], { windowsHide: true, stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; });
  await until(async () => { if (launchError) throw launchError; if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Owned browser exited'); return alivePipe(); }, 45000, 'Private browser pipe');
  client = new Client({ name: 'Scroll container regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', resolve(profile, 'client-config.json')], stderr: 'pipe' }));
  worker = await tool('worker_create', { name: 'Scroll container owner', workspaceId });
  const files = await tool('files', { ...workerScope(worker), folderId, limit: 1000 });
  fileId = files.files.find(item => item.name === 'approved-report.txt')?.fileId; assert(fileId);
}

// Synthetic application form. Layout modes:
//   window     the document scrolls
//   body-overflow  html,body{height:100%} body{overflow-x:hidden}: the body's
//              overflow (computed overflow-y auto) applies to the scrolling
//              viewport, not to the body's one-screen-high box
//   root-overflow  html{height:100%;overflow-x:hidden}: the same for the root
//   container  html/body are fixed height; an inner overflow:auto pane scrolls
//              below a fixed page header (the common SPA wizard layout)
//   sticky     container plus a composited (translateZ) pane, sticky section
//              headers inside it and a sticky action bar
//   dense      container plus thousands of laid-out but invisible nodes and a
//              long rendered disclosure, like large framework-generated forms
// Withholding probes inside the pane: a hidden upload entry, one covered by an
// opaque panel, one scrolled out of the pane's clip and a disabled textarea.
function page(caseId, mode) {
  const container = ['container', 'sticky', 'dense'].includes(mode), sticky = mode === 'sticky', dense = mode === 'dense';
  const filler = (count, prefix) => Array.from({ length: count }, (_, i) => '<p class="filler">' + prefix + ' paragraph ' + (i + 1) + ' explains a synthetic policy for this practice application form.</p>').join('');
  const section = (title, body) => '<section><h2' + (sticky ? ' class="sticky"' : '') + '>' + title + '</h2>' + body + '</section>';
  const upload = (id, label, extra = '') => '<div class="upload" ' + extra + '><span id="' + id + '" class="entry">' + label + '</span><input id="' + id + '-file" type="file" hidden></div>';
  const invisible = dense ? '<div aria-hidden="true" style="position:absolute;left:0;top:0;width:1px;height:1px;overflow:hidden;opacity:0">' +
    Array.from({ length: 900 }, (_, i) => '<div class="opt"><span>Option ' + i + '</span><span>Region ' + (i % 40) + '</span></div>').join('') + '</div>' : '';
  const form = '<form id="form" onsubmit="return false">' +
    section('Personal details', '<label for="last">Last Name</label><input id="last" autocomplete="off"><label for="first">First Name</label><input id="first" autocomplete="off">' + filler(4, 'Privacy')) +
    section('Experience', filler(dense ? 10 : 6, 'Experience') + '<label for="why">Why this role?</label><textarea id="why" rows="4"></textarea>' +
      '<label for="frozen">Frozen notes</label><textarea id="frozen" rows="2" disabled></textarea>') +
    section('Skills', '<div id="skills-label" class="label">Skills</div><div class="chips" role="listbox" aria-labelledby="skills-label" aria-multiselectable="true">' +
      ['C++', 'TypeScript', 'Embedded', 'Testing'].map(skill => '<span class="chip" role="option" aria-selected="false" tabindex="0">' + skill + '</span>').join('') + '</div>' + filler(dense ? 12 : 6, 'Skills')) +
    section('Attachments', filler(dense ? 30 : 4, 'Disclosure') + upload('cover', 'UPLOAD COVER LETTER') + upload('resume', 'UPLOAD RESUME') +
      upload('hidden-entry', 'UPLOAD HIDDEN DOCUMENT', 'hidden') +
      '<div class="covered-wrap">' + upload('covered', 'UPLOAD COVERED DOCUMENT') + '<div class="cover-panel"></div></div>' +
      '<label for="notes">Additional notes</label><textarea id="notes" rows="3"></textarea>' + filler(8, 'Closing')) +
    '</form>';
  const style = '<style>*{box-sizing:border-box}html,body{margin:0;background:#fff;color:#111;font:16px system-ui}' +
    'header{height:64px;background:#0b3a5b;color:#fff;font-size:20px;padding:20px}' +
    (container ? 'html,body{height:100%;overflow:hidden}header{position:fixed;top:0;left:0;right:0;z-index:2}#pane{position:absolute;top:64px;left:0;right:0;bottom:0;overflow:auto' + (sticky ? ';transform:translateZ(0)' : '') + '}'
      : 'header{position:sticky;top:0;z-index:2}' + (mode === 'body-overflow' ? 'html,body{height:100%}body{overflow-x:hidden}' : mode === 'root-overflow' ? 'html{height:100%;overflow-x:hidden}' : '')) +
    '#form{max-width:720px;margin:0 auto;padding:16px 24px 400px}section{margin-bottom:24px}h2{background:#fff;margin:0;padding:12px 0;font-size:20px}' +
    (sticky ? '.sticky{position:sticky;top:0;z-index:1;border-bottom:1px solid #999}.actions{position:sticky;bottom:0;background:#eee;padding:12px;border-top:1px solid #999}' : '') +
    'label,.label{display:block;margin:12px 0 4px;font-weight:600}input,textarea{display:block;width:100%;font:inherit;padding:8px;border:1px solid #555;background:#fff;color:#111}' +
    '.chips{display:flex;gap:8px;padding:8px;border:1px solid #555}.chip{padding:6px 12px;border:1px solid #333;border-radius:14px;background:#fff;color:#111}.chip[aria-selected=true]{background:#dde}' +
    '.upload{margin:12px 0}.entry{display:inline-block;padding:10px 16px;border:1px solid #0b3a5b;color:#0b3a5b;font-weight:700;background:#fff}' +
    '.covered-wrap{position:relative}.cover-panel{position:absolute;inset:0;background:#000}[hidden]{display:none!important}.filler{line-height:1.5}</style>';
  const script = '<script>const caseId=' + JSON.stringify(caseId) + ';const pane=' + (container ? 'document.querySelector("#pane")' : 'document.scrollingElement') + ';' +
    'function send(v){return fetch("/event?case="+caseId,{method:"POST",body:JSON.stringify(v)})}send({type:"opened"});' +
    'let last=-1;const report=()=>{const top=Math.round(pane.scrollTop);if(top!==last){last=top;send({type:"scroll",top})}};(' + (container ? 'pane' : 'window') + ').addEventListener("scroll",report,{passive:true});' +
    'for(const id of ["why","notes"])document.getElementById(id).addEventListener("input",e=>send({type:"fill",id,value:e.target.value,trusted:e.isTrusted}));' +
    'for(const chip of document.querySelectorAll(".chip"))chip.addEventListener("click",e=>{chip.setAttribute("aria-selected",chip.getAttribute("aria-selected")!=="true");send({type:"chip",name:chip.textContent,selected:chip.getAttribute("aria-selected")==="true",trusted:e.isTrusted})});' +
    'for(const entry of document.querySelectorAll(".entry")){const file=document.getElementById(entry.id+"-file");' +
    'entry.addEventListener("click",e=>{send({type:"click",id:entry.id,trusted:e.isTrusted});file.click()});' +
    'file.addEventListener("change",async()=>{if(!file.files[0])return;const data=new FormData();data.append("file",file.files[0]);await fetch("/upload?case="+caseId+"&entry="+entry.id,{method:"POST",body:data})})}' +
    // Server-requested page-side scrolling, as a site's own "next section" link would.
    'setInterval(async()=>{const s=await(await fetch("/state?case="+caseId)).json();if(s.scrollIntoView&&!window.done){window.done=true;document.getElementById(s.scrollIntoView).scrollIntoView({block:"center"});send({type:"scrolledIntoView"})}},100);</script>';
  return '<!doctype html><meta charset="utf-8"><title>Scroll fixture ' + caseId + '</title>' + style + '<header>Synthetic careers application</header>' + invisible +
    '<div id="pane">' + form + (sticky ? '<div class="actions">Synthetic action bar</div>' : '') + '</div>' + script;
}
async function startFixtures() {
  const server = http.createServer((request, response) => {
    const url = new URL(request.url, 'http://127.0.0.1'), caseId = url.searchParams.get('case');
    if (!caseId || !/^[a-z-]+$/.test(caseId)) { response.writeHead(400).end(); return; }
    let state = states.get(caseId);
    if (!state) { state = { opened: false, scrollTop: 0, fills: {}, chips: {}, clicks: [], uploads: {} }; states.set(caseId, state); }
    response.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/state') { response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify({ scrollIntoView: state.scrollIntoView ?? null })); return; }
    if (request.method === 'POST') {
      const chunks = []; let size = 0; request.on('data', chunk => { size += chunk.length; if (size > 65536) request.destroy(); else chunks.push(chunk); });
      request.on('end', () => {
        const body = Buffer.concat(chunks);
        if (url.pathname === '/upload') {
          const entry = url.searchParams.get('entry');
          state.uploads[entry] = body.includes(Buffer.from('filename="approved-report.txt"')) && body.includes(Buffer.from('Xenon synthetic approved upload.\n'));
          response.end('ok'); return;
        }
        try {
          const value = JSON.parse(body);
          if (value.type === 'opened') state.opened = true;
          if (value.type === 'scroll') state.scrollTop = value.top;
          if (value.type === 'fill') state.fills[value.id] = { value: value.value, trusted: value.trusted };
          if (value.type === 'chip') state.chips[value.name] = { selected: value.selected, trusted: value.trusted };
          if (value.type === 'click') state.clicks.push({ id: value.id, trusted: value.trusted });
          if (value.type === 'scrolledIntoView') state.scrolledIntoView = true;
          response.end('ok');
        } catch { response.writeHead(400).end(); }
      }); return;
    }
    if (url.pathname !== '/page') { response.writeHead(404).end(); return; }
    response.setHeader('Content-Type', 'text/html; charset=utf-8'); response.end(page(caseId, url.searchParams.get('mode')));
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  servers.push(server); origin = 'http://127.0.0.1:' + server.address().port;
}
async function open(caseId, mode) {
  const tab = { ...worker, ...await tool('tab_create', { ...workerScope(worker), url: origin + '/page?case=' + caseId + '&mode=' + mode }), caseId };
  await until(() => states.get(caseId)?.opened);
  await until(async () => refNode(await observe(tab), 'Last Name', 'INPUT'), 10000, 'Top-of-form Last Name ref'); return tab;
}
async function act(tab, name, tag, action, fields = {}) {
  const { observation, node } = await scrollTo(tab, name, tag);
  return tool('interact', { ...mutation(tab), observationId: observation.observationId, action, elementRef: node.ref, ...fields });
}

if (process.platform !== 'win32') throw new Error('Scroll container regression requires Windows.');
if (await alivePipe()) throw new Error('Randomly allocated private pipe already exists.');
try {
  await mkdir(profile, { recursive: true }); await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_AUTH_FIXTURE\n');
  const seeded = JSON.parse((await runFile(resolve(buildRoot, 'Release/vault_fixture_seed.exe'), [profile], { windowsHide: true })).stdout.trim()); folderId = seeded.folderId;
  const identity = { clientId: 'scroll_' + randomBytes(8).toString('hex'), token: randomBytes(32).toString('hex'), pipe };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic scroll client', tokenHash: sha256(identity.token) }],
    workspaces: [{ id: workspaceId, clients: [identity.clientId] }], accountGrants: [], operations: [] }));
  await writePrivateConfig(resolve(profile, 'client-config.json'), identity);
  await startFixtures(); applicationDllSha256 = await hashDll(); await launch();

  // Browser zoom first: the zoomed tab is then the agent workspace's first tab.
  // Shell zoom acts on the selected tab, and Chromium applies it per host, so
  // the container case opened afterwards on the same origin is zoomed too.
  let zoomBase;
  for (const [caseId, mode] of [['zoom-window', 'window'], ['zoom-container', 'container']]) {
    await check('Lower-page fields stay actionable at 120% browser zoom: ' + mode, async () => {
      const tab = await open(caseId, mode), state = states.get(caseId);
      if (!zoomBase) {
        zoomBase = (await observe(tab)).viewport;
        await shell('select-shell-tab.ps1', '-Group', '-1', '-Index', '0');
        await shell('post-shell-command.ps1', '-Command', String(MenuZoomIn));
      }
      const zoomed = await settleZoom(tab, zoomBase, 1.2);
      await act(tab, 'Why this role?', 'TEXTAREA', 'fill', { text: 'Zoomed motivation' });
      await until(() => state.fills.why?.value === 'Zoomed motivation', 5000, 'Website textarea value');
      const resume = await scrollTo(tab, 'UPLOAD RESUME');
      const response = await raw('upload', { ...mutation(tab), observationId: resume.observation.observationId, elementRef: resume.node.ref, fileId });
      assert(!response.isError, JSON.stringify(response.structuredContent));
      await until(() => state.uploads.resume === true, 8000, 'Website received the approved file');
      return { browserZoom: zoomBase.width / zoomed.width, scrollTop: state.scrollTop, finalPageY: resume.observation.viewport.pageY };
    });
  }
  await check('Browser zoom resets before the remaining checks', async () => {
    const tab = await open('zoom-reset', 'window');
    await shell('post-shell-command.ps1', '-Command', String(MenuZoomReset));
    return { viewport: await settleZoom(tab, zoomBase, 1) };
  });

  for (const mode of ['window', 'body-overflow', 'root-overflow', 'container', 'sticky', 'dense']) {
    await check('Lower-page textarea, chips and text upload entry are actionable: ' + mode, async () => {
      const tab = await open(mode, mode), state = states.get(mode);
      await act(tab, 'Why this role?', 'TEXTAREA', 'fill', { text: 'Synthetic motivation ' + mode });
      await until(() => state.fills.why?.value === 'Synthetic motivation ' + mode, 5000, 'Website textarea value');
      const { observation, node } = await scrollTo(tab, 'TypeScript');
      await tool('interact', { ...mutation(tab), observationId: observation.observationId, action: 'click', elementRef: node.ref });
      await until(() => state.chips.TypeScript?.selected, 5000, 'Website chip selection');
      const resume = await scrollTo(tab, 'UPLOAD RESUME');
      const response = await raw('upload', { ...mutation(tab), observationId: resume.observation.observationId, elementRef: resume.node.ref, fileId });
      assert(!response.isError, JSON.stringify(response.structuredContent));
      await until(() => state.uploads.resume === true, 8000, 'Website received the approved file');
      await act(tab, 'Additional notes', 'TEXTAREA', 'fill', { text: 'Synthetic notes' });
      await until(() => state.fills.notes?.value === 'Synthetic notes', 5000, 'Website notes value');
      assert(state.scrollTop > 0 || mode === 'window', 'The fixture pane never scrolled');
      assert(state.fills.why.trusted && state.chips.TypeScript.trusted && state.clicks.every(click => click.trusted));
      assert.deepEqual(state.clicks.map(click => click.id), ['resume']);
      return { scrollTop: state.scrollTop, observations: scrollTraces[mode].length, uploads: state.uploads, chips: state.chips };
    });
    await check('Withheld controls stay unreferenced after scrolling: ' + mode, async () => {
      const caseId = mode + '-withheld', tab = await open(caseId, mode);
      const { observation } = await scrollTo(tab, 'Additional notes', 'TEXTAREA');
      const text = JSON.stringify(observation);
      assert(!text.includes('UPLOAD HIDDEN DOCUMENT'), 'display:none upload entry leaked');
      assert(!observation.nodes.some(node => node.name === 'UPLOAD COVERED DOCUMENT' && node.ref), 'Covered upload entry received a ref');
      // Content scrolled above the pane's clip is not in the rendered viewport.
      assert(!refNode(observation, 'Last Name', 'INPUT'), 'Control scrolled out of view kept a ref');
      const frozen = observation.nodes.find(node => node.name === 'Frozen notes' && node.tag === 'TEXTAREA');
      if (frozen?.ref) {
        const response = await raw('interact', { ...mutation(tab), observationId: observation.observationId, action: 'fill', elementRef: frozen.ref, text: 'must not apply' });
        assert(response.isError, 'Disabled textarea accepted a fill');
      }
      assert.equal(states.get(caseId).fills.frozen, undefined);
      assert.equal(states.get(caseId).clicks.length, 0);
      return { coveredWithheld: true, hiddenWithheld: true, outOfClipWithheld: true };
    });
  }
  await check('Page-driven scrollIntoView inside the container yields a fresh upload ref', async () => {
    const tab = await open('into-view', 'container'), state = states.get('into-view');
    state.scrollIntoView = 'cover'; await until(() => state.scrolledIntoView, 5000, 'Page scrollIntoView');
    await sleep(200); const observation = await observe(tab), node = refNode(observation, 'UPLOAD COVER LETTER');
    (scrollTraces['into-view'] ??= []).push({ target: 'UPLOAD COVER LETTER', scrollTop: state.scrollTop, ...summarize(observation), found: !!node });
    assert(node, 'scrollIntoView target has no verified ref: ' + JSON.stringify(summarize(observation)));
    const response = await raw('upload', { ...mutation(tab), observationId: observation.observationId, elementRef: node.ref, fileId });
    assert(!response.isError, JSON.stringify(response.structuredContent));
    await until(() => state.uploads.cover === true, 8000, 'Website received the approved cover letter');
    return { scrollTop: state.scrollTop, uploads: state.uploads };
  });
  await check('A ref from before a container scroll is rejected as stale geometry', async () => {
    const tab = await open('stale-scroll', 'container'), state = states.get('stale-scroll');
    const { observation, node } = await scrollTo(tab, 'Why this role?', 'TEXTAREA');
    const before = state.scrollTop;
    await tool('interact', { ...mutation(tab), observationId: observation.observationId, action: 'scroll', direction: 'down', amount: 2500 });
    await until(() => state.scrollTop > before, 3000, 'Container scrolled away');
    const response = await raw('interact', { ...mutation(tab), observationId: observation.observationId, action: 'fill', elementRef: node.ref, text: 'must not apply' });
    assert(response.isError, 'A ref scrolled out of the container accepted input');
    assert.equal(state.fills.why, undefined);
    return { rejected: response.structuredContent?.error?.code };
  });
} catch (error) {
  if (!results.some(result => !result.passed)) results.push({ name: 'Scroll container harness', passed: false, error: error.message });
  console.log('FAIL scroll container harness: ' + error.message);
} finally {
  if (client) await client.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null) {
    let terminationError; try { await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }); } catch (error) { terminationError = error; }
    try { await until(() => browser.exitCode !== null || browser.signalCode !== null, 10000, 'Owned process exit'); await until(async () => !(await alivePipe()), 15000, 'Private pipe shutdown'); }
    catch (error) { results.push({ name: 'Owned process cleanup', passed: false, error: [terminationError?.message, error.message].filter(Boolean).join('\n') }); }
  }
  for (const server of servers) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
  if (lastObservation) await writeFile(resolve(profile, 'last-observation.json'), JSON.stringify(lastObservation, null, 2));
  await writeFile(resolve(profile, 'xenon-mcp-calls.json'), JSON.stringify({ calls }, null, 2));
  try { applicationDllSha256AtEnd = await hashDll(); assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'Browser DLL changed during test'); }
  catch (error) { results.push({ name: 'Binary stability', passed: false, error: error.message }); }
  const report = { run, capturedAt: new Date().toISOString(), binary, profile, applicationDllSha256, applicationDllSha256AtEnd,
    mode: diagnose ? 'diagnosis' : 'acceptance', passed: results.length === 17 && results.every(result => result.passed), results, scrollTraces };
  await mkdir(resolve(root, 'out'), { recursive: true }); const reportPath = resolve(root, 'out', run + '.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2)); console.log('Report: ' + reportPath); process.exitCode = report.passed ? 0 : 1;
}
