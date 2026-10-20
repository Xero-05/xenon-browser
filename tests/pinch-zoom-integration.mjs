// Screenshot coordinates, rendered evidence and element actions under pinch
// zoom, through real MCP against a synthetic page.
// A person pinches with a touchscreen or precision touchpad; Xenon's input hooks
// do not see that gesture. This suite stands in for it with the unshipped
// XenonAuthTest driver, which asks Chromium for a synthetic touchpad pinch. It
// uses a generated marked profile and paired client, never a user profile.
// The last checks map window pixels through the pinch: a person's pointer
// (recorded by the same driver, without input), the agent pointer overlay and
// the human inspector's page picking and highlight. Native window messages and
// window rectangles are read from this disposable process only.
import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';
import { startVisibilityFixture } from './fixtures/visibility-fixture.mjs';
import { colorBox, decodePng } from './fixtures/screenshot-pixels.mjs';

const root = resolve(import.meta.dirname, '..');
const run = `pinch-zoom-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const pipeName = `xenon-test-${run}`;
const pipe = '\\\\.\\pipe\\' + pipeName;
const profile = resolve(root, '.cache', run);
const binary = resolve(process.env.XENON_AUTH_TEST_BROWSER ?? resolve(root, 'build/auth-fixture/Release/XenonAuthTest.exe'));
const applicationDll = resolve(binary, '../XenonAuthTest.dll');
const configPath = resolve(profile, 'client-config.json');
const hashDll = async () => createHash('sha256').update(await readFile(applicationDll)).digest('hex');
const runFile = promisify(execFile);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const results = [];
let browser, fixture, client, tab, applicationDllSha256, applicationDllSha256AtEnd, driverId = 0;

const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipe);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => { socket.destroy(); resolve(false); });
});
async function until(action, timeoutMs = 20_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) { const value = await action(); if (value) return value; await sleep(120); }
  throw new Error('Expected fixture state did not appear');
}
async function check(name, action) {
  const started = Date.now();
  try {
    const details = await action();
    results.push({ name, passed: true, elapsedMs: Date.now() - started, details });
    console.log(`PASS ${name}`);
  } catch (error) {
    results.push({ name, passed: false, elapsedMs: Date.now() - started, error: error.message });
    console.log(`FAIL ${name}: ${error.message}`);
  }
}
const scope = () => ({ agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId });
const mutation = () => ({ ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
const raw = (name, args = {}) => client.callTool({ name: `xenon_${name}`, arguments: args });
async function tool(name, args = {}) {
  const response = await raw(name, args);
  assert(!response.isError, `${name}: ${JSON.stringify(response.structuredContent ?? response.content)}`);
  return response.structuredContent;
}
// One fixed native gesture per request; the result holds viewport numbers only.
async function driver(action, scale, point = {}) {
  const id = ++driverId, path = resolve(profile, 'page-scale-result.json');
  await writeFile(resolve(profile, 'page-scale-request.json'), JSON.stringify({ id, action, tabId: tab.tabId, ...(scale ? { scale } : {}), ...point }));
  const reply = await until(async () => {
    try { const value = JSON.parse(await readFile(path, 'utf8')); return value.id === id ? value : false; } catch { return false; }
  }, 10_000);
  assert(reply.ok, `Page scale driver ${action}: ${JSON.stringify(reply.error)}`);
  return reply.result;
}
// The compositor applies a pinch before Blink reports it; wait for two equal readings.
async function settle(scale) {
  let previous;
  return until(async () => {
    const metrics = (await driver('metrics')).before;
    const settled = Math.abs(metrics.cssVisualViewport.scale - scale) < 0.01 && JSON.stringify(metrics) === previous;
    previous = JSON.stringify(metrics);
    return settled && metrics;
  }, 10_000);
}
async function capture() {
  const response = await raw('screenshot', scope());
  assert(!response.isError, `screenshot: ${JSON.stringify(response.structuredContent ?? response.content)}`);
  const png = decodePng(Buffer.from(response.content.find(item => item.type === 'image').data, 'base64')), meta = response.structuredContent;
  assert.equal(png.width, meta.imageWidth); assert.equal(png.height, meta.imageHeight);
  return { png, meta };
}
const colors = {
  'pinch-center': (r, g, b) => r > 180 && g < 90 && b > 180,
  'pinch-left': (r, g, b) => r < 90 && g > 180 && b > 180,
  'pinch-corner': (r, g, b) => r > 180 && g > 180 && b < 90,
};
const imageCenter = (box, meta) => ({ x: (box.left + box.right + 1) / 2 / meta.scaleX, y: (box.top + box.bottom + 1) / 2 / meta.scaleY });
const clicks = id => fixture.clicks()[id] ?? 0;
// Page-window client pixels per input (visual-viewport CSS) pixel: display
// scale times browser zoom times pinch, the screenshot scale.
const clientScale = metrics => metrics.visualViewport.clientWidth / metrics.cssVisualViewport.clientWidth * metrics.cssVisualViewport.scale;
const powershell = (script, ...args) => runFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', resolve(root, 'tests', script), ...args], { windowsHide: true });
const shell = (script, ...args) => powershell(`fixtures/${script}`, '-ProcessId', String(browser.pid), ...args);
const windows = async () => JSON.parse((await shell('pointer-overlay-windows.ps1')).stdout);
const elements = async (action = 'state', name, image) => JSON.parse((await powershell('developer-elements-driver.ps1', '-BrowserPid', String(browser.pid), '-Action', action,
  ...(name ? ['-Name', name] : []), ...(image ? ['-Image', resolve(profile, image)] : []))).stdout || '{}');
const selected = async () => JSON.parse((await elements('selection-state')).details || '{}');
async function clicked(id, count) {
  await until(() => clicks(id) === count, 5000).catch(() => { throw new Error(`${id} click count is ${clicks(id)}, expected ${count}`); });
}

if (process.platform !== 'win32') throw new Error('Live pinch-zoom regression requires Windows.');
if (await alivePipe()) throw new Error('The randomly allocated pinch test pipe is already in use.');
try {
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_PINCH_FIXTURE\n');
  const identity = { clientId: `pinch_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex') };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic pinch client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [], accountGrants: [], operations: [] }));
  await writePrivateConfig(configPath, { ...identity, pipe });
  fixture = await startVisibilityFixture();
  applicationDllSha256 = await hashDll();
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`, '--test-page-scale'], { windowsHide: true, stdio: 'ignore' });
  let launchError;
  browser.once('error', error => { launchError = error; });
  await until(async () => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Browser exited before the private pipe became ready');
    return alivePipe();
  }, 45_000);
  client = new Client({ name: 'Xenon pinch-zoom regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' }));
  const worker = await tool('worker_create', { name: 'Pinch-zoom coordinates' });
  tab = { ...worker, ...await tool('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `${fixture.origin}/pinch` }) };
  await until(async () => {
    try { const response = await raw('observe', { ...scope(), maxNodes: 100 }); return !response.isError && response.structuredContent.nodes?.some(node => node.name === 'Viewport fixture ready'); }
    catch { return false; }
  });
  const unpinched = await settle(1);

  await check('A pinch after the screenshot invalidates its coordinates', async () => {
    const { png, meta } = await capture();
    const box = colorBox(png, colors['pinch-left']);
    assert(box, 'The left target is in the unpinched screenshot');
    const point = imageCenter(box, meta);
    const pinch = await driver('pinch', 2);
    const pinched = await settle(2);
    // The pinch moves only the visual viewport, so the layout viewport alone cannot reveal it.
    assert.deepEqual(pinched.cssLayoutViewport, unpinched.cssLayoutViewport, 'The fixture pinch also changed the layout viewport');
    const stale = await raw('interact', { ...mutation(), observationId: meta.observationId, action: 'click', ...point });
    await sleep(300);
    assert.equal(stale.structuredContent?.error?.code, 'stale_viewport', `Coordinates from before the pinch were accepted: ${JSON.stringify({ reply: stale.structuredContent, clicks: fixture.clicks() })}`);
    assert.equal(clicks('pinch-left'), 0);
    return { unpinchedPoint: point, pinch, rejected: stale.structuredContent.error.code };
  });

  await check('Pinched screenshot pixels land on their targets', async () => {
    const { png, meta } = await capture();
    const metrics = await settle(2);
    const zoom = metrics.visualViewport.clientWidth / metrics.cssVisualViewport.clientWidth;
    const pinch = metrics.cssVisualViewport.scale;
    assert(Math.abs(meta.scaleX - zoom * pinch) < 1e-3, `Screenshot scale ${meta.scaleX} must be CSS-to-physical zoom ${zoom} times pinch ${pinch}`);
    assert(Math.abs(meta.viewport.width - metrics.cssVisualViewport.clientWidth) < 0.01, 'Screenshot viewport is the visual viewport');
    const hits = [];
    for (const id of ['pinch-center', 'pinch-left']) {
      const box = colorBox(png, colors[id]);
      assert(box, `${id} is in the pinched screenshot`);
      const point = imageCenter(box, meta);
      await tool('interact', { ...mutation(), observationId: meta.observationId, action: 'click', ...point });
      await clicked(id, 1);
      hits.push({ id, imageBox: box, ...point });
    }
    return { scaleX: meta.scaleX, zoom, pinch, imageWidth: png.width, imageHeight: png.height, viewport: meta.viewport, hits };
  });

  await check('Coordinates outside the pinched view are rejected before input', async () => {
    const { png, meta } = await capture();
    const metrics = await settle(2);
    assert(!colorBox(png, colors['pinch-corner']), 'The corner target must be outside the pinched view');
    // The corner target's center in visual-viewport CSS pixels: inside the layout
    // viewport (the old bound) but past the visual viewport.
    const layout = metrics.cssLayoutViewport, visual = metrics.cssVisualViewport;
    const corner = { x: layout.clientWidth - 44 - visual.offsetX, y: layout.clientHeight - 44 - visual.offsetY };
    assert(corner.x < layout.clientWidth && corner.y < layout.clientHeight && corner.x >= visual.clientWidth && corner.y >= visual.clientHeight, `Unexpected pinch geometry: ${JSON.stringify({ corner, layout, visual })}`);
    const click = await raw('interact', { ...mutation(), observationId: meta.observationId, action: 'click', ...corner });
    await sleep(300);
    assert.equal(click.structuredContent?.error?.code, 'invalid_coordinates', `An unseen coordinate was accepted: ${JSON.stringify({ corner, reply: click.structuredContent, clicks: fixture.clicks() })}`);
    const center = imageCenter(colorBox(png, colors['pinch-center']), meta);
    const drag = await raw('interact', { ...mutation(), observationId: meta.observationId, action: 'drag', fromX: center.x, fromY: center.y, toX: corner.x, toY: corner.y });
    assert.equal(drag.structuredContent?.error?.code, 'invalid_coordinates', `A drag to an unseen coordinate was accepted: ${JSON.stringify(drag.structuredContent)}`);
    // The vertical scrollbar stays at the view's right edge, outside the visual viewport.
    const scrollbar = await raw('interact', { ...mutation(), observationId: meta.observationId, action: 'click', x: (png.width - 2) / meta.scaleX, y: visual.clientHeight / 2 });
    assert.equal(scrollbar.structuredContent?.error?.code, 'invalid_coordinates', 'A scrollbar pixel became page input while pinched');
    await sleep(300);
    assert.equal(clicks('pinch-corner'), 0, 'An unseen target received input');
    const after = await settle(2);
    assert.deepEqual(after, metrics, 'A rejected coordinate scrolled or panned the page');
    return { corner, rejectedClick: true, rejectedDrag: true, rejectedScrollbar: true, metrics };
  });

  // Rendered evidence is what the pinched view shows: the visual viewport.
  await check('Pinched observation reports only the magnified view', async () => {
    const metrics = await settle(2);
    const observation = await tool('observe', { ...scope(), maxNodes: 300 });
    const visual = metrics.cssVisualViewport, node = name => observation.nodes.find(item => item.name === name);
    const evidence = { viewport: observation.viewport, visual, layout: metrics.cssLayoutViewport, coverage: observation.coverage,
      shown: node('Pinch shown action'), unseen: node('Pinch unseen action'), heading: node('Viewport fixture ready') };
    assert(evidence.shown?.ref && evidence.shown.visibilityStatus === 'verified', `The control inside the magnified view is not actionable: ${JSON.stringify(evidence)}`);
    assert(!evidence.unseen && !evidence.heading, `Content outside the magnified view was reported as rendered: ${JSON.stringify(evidence)}`);
    for (const [key, value] of [['pageX', visual.pageX], ['pageY', visual.pageY], ['width', visual.clientWidth], ['height', visual.clientHeight]])
      assert(Math.abs(observation.viewport[key] - value) < 0.01, `Observation viewport ${key} must be the visual viewport: ${JSON.stringify(evidence)}`);
    return evidence;
  });

  await check('Inspection works while pinched and its bounds address the magnified view', async () => {
    const metrics = await settle(2);
    const attempts = [];
    let inspection;
    for (let attempt = 0; attempt < 5 && !inspection; ++attempt) {
      const response = await raw('inspect', { ...scope(), maxNodes: 300 });
      if (!response.isError) inspection = response.structuredContent;
      else { attempts.push(response.structuredContent?.error?.code ?? JSON.stringify(response.content)); await sleep(250); }
    }
    assert(inspection, `Inspection failed on every attempt while pinched: ${JSON.stringify({ attempts, visual: metrics.cssVisualViewport, layout: metrics.cssLayoutViewport })}`);
    const visual = metrics.cssVisualViewport;
    assert(Math.abs(inspection.viewport.width - visual.clientWidth) < 0.01 && Math.abs(inspection.viewport.pageX - visual.pageX) < 0.01,
      `Inspection viewport must be the visual viewport: ${JSON.stringify({ viewport: inspection.viewport, visual })}`);
    const shown = inspection.nodes.find(node => node.name === 'Pinch shown action' && node.ref);
    assert(shown, 'Inspection lost the control inside the magnified view');
    // Root-frame bounds minus the viewport offset are coordinate-input CSS pixels.
    const x = shown.bounds.x - inspection.viewport.pageX + shown.bounds.width / 2, y = shown.bounds.y - inspection.viewport.pageY + shown.bounds.height / 2;
    const before = clicks('pinch-shown');
    await tool('interact', { ...mutation(), observationId: inspection.observationId, action: 'click', x, y });
    await clicked('pinch-shown', before + 1);
    return { attempts, viewport: inspection.viewport, scaleX: inspection.scaleX, bounds: shown.bounds, click: { x, y } };
  });

  await check('Unpinching restores the unpinched coordinate space', async () => {
    await driver('pinch', 0.5);
    const metrics = await settle(1);
    const { png, meta } = await capture();
    const box = colorBox(png, colors['pinch-corner']);
    assert(box, 'The corner target is back in view');
    await tool('interact', { ...mutation(), observationId: meta.observationId, action: 'click', ...imageCenter(box, meta) });
    await clicked('pinch-corner', 1);
    return { scaleX: meta.scaleX, viewport: meta.viewport, layout: metrics.cssLayoutViewport };
  });
  // A reference observed before a pinch. A person's pinch pauses the tab and
  // requires fresh evidence, but the driver's synthetic pinch is not seen as
  // human input, so the broker accepts the reference and the native rendered
  // check must reject it.
  await check('An element reference that a pinch moves out of view is rejected before input', async () => {
    await settle(1);
    const observation = await tool('observe', { ...scope(), maxNodes: 300 });
    const unseen = observation.nodes.find(node => node.name === 'Pinch unseen action' && node.ref);
    assert(unseen, 'The top-right control is not actionable before the pinch');
    await driver('pinch', 2);
    const metrics = await settle(2);
    fixture.resetOutsidePointer();
    const before = clicks('pinch-unseen');
    const response = await raw('interact', { ...mutation(), observationId: observation.observationId, action: 'click', elementRef: unseen.ref });
    await sleep(400);
    const evidence = { reply: response.structuredContent, clicks: fixture.clicks(), outsidePointer: fixture.outsidePointer(), bounds: unseen.bounds, visual: metrics.cssVisualViewport };
    assert(response.isError, `A reference outside the magnified view was clicked: ${JSON.stringify(evidence)}`);
    assert.equal(clicks('pinch-unseen'), before, `The unseen control received input: ${JSON.stringify(evidence)}`);
    assert.deepEqual(fixture.outsidePointer(), {}, `Pointer events reached the page outside the magnified view: ${JSON.stringify(evidence)}`);
    return { rejected: response.structuredContent?.error?.code, bounds: unseen.bounds, visual: metrics.cssVisualViewport };
  });

  // The previous agent click was at the corner target, unpinched. That point is
  // now outside the magnified view; the move toward the center must not pass there.
  await check('Agent pointer movement stays inside the pinched view', async () => {
    const metrics = await settle(2);
    const { png, meta } = await capture();
    const box = colorBox(png, colors['pinch-center']);
    assert(box, 'The center target is in the pinched screenshot');
    fixture.resetOutsidePointer();
    const before = clicks('pinch-center'), point = imageCenter(box, meta);
    await tool('interact', { ...mutation(), observationId: meta.observationId, action: 'click', ...point });
    await clicked('pinch-center', before + 1);
    await sleep(400);
    const evidence = { outsidePointer: fixture.outsidePointer(), clicks: fixture.clicks(), point, visual: metrics.cssVisualViewport };
    assert.deepEqual(evidence.outsidePointer, {}, `Pointer events reached the page outside the magnified view: ${JSON.stringify(evidence)}`);
    return evidence;
  });

  // At 3x the layout viewport's center is outside the magnified view.
  await check('Scrolling while pinched sends the wheel inside the magnified view', async () => {
    await driver('pinch', 1.5);
    const metrics = await settle(3);
    const observation = await tool('observe', { ...scope(), maxNodes: 100 });
    fixture.resetOutsidePointer();
    await tool('interact', { ...mutation(), observationId: observation.observationId, action: 'scroll', direction: 'down', amount: 120 });
    const after = await until(async () => {
      const current = (await driver('metrics')).before;
      return current.cssVisualViewport.pageY > metrics.cssVisualViewport.pageY && current;
    }, 5000).catch(() => null);
    await sleep(300);
    const evidence = { outsidePointer: fixture.outsidePointer(), visual: metrics.cssVisualViewport, after: after?.cssVisualViewport ?? null };
    assert.deepEqual(evidence.outsidePointer, {}, `The wheel reached the page outside the magnified view: ${JSON.stringify(evidence)}`);
    assert(after, `The wheel did not scroll the pinched page: ${JSON.stringify(evidence)}`);
    return evidence;
  });

  // The pointer overlay and inspector follow the selected tab of a window.
  // The agent's automatic workspace follows the default one.
  let agentClick;
  await check('Agent movement starts at a person\'s pointer position while pinched', async () => {
    const { stdout } = await shell('select-shell-tab.ps1', '-Group', '-1', '-Index', '0');
    assert.match(stdout, /selected workspace row (\d+) of (\d+)/);
    await driver('pinch', 1 / 3);
    await settle(1);
    await driver('pinch', 2);
    const metrics = await settle(2);
    const { png, meta } = await capture();
    const box = colorBox(png, colors['pinch-center']);
    assert(box, 'The center target is in the pinched screenshot');
    // A passive hover at window pixels. At 2x it is half as far into the page
    // in input coordinates as in window coordinates.
    const visual = metrics.cssVisualViewport;
    const client = { x: Math.round(visual.clientWidth * 0.2 * meta.scaleX), y: Math.round(visual.clientHeight * 0.25 * meta.scaleY) };
    const person = { x: client.x / meta.scaleX, y: client.y / meta.scaleY };
    await driver('hover', undefined, { clientX: client.x, clientY: client.y });
    fixture.resetPointerPath(); fixture.resetOutsidePointer();
    const before = clicks('pinch-center');
    agentClick = imageCenter(box, meta);
    await tool('interact', { ...mutation(), observationId: meta.observationId, action: 'click', ...agentClick });
    await clicked('pinch-center', before + 1);
    await sleep(400);
    const path = fixture.pointerPath();
    const evidence = { selection: stdout.trim(), client, person, first: path[0], last: path.at(-1), moves: path.length, click: agentClick, outsidePointer: fixture.outsidePointer(), visual, scaleX: meta.scaleX };
    assert(path.length, `The page saw no agent pointer movement: ${JSON.stringify(evidence)}`);
    assert(Math.hypot(path[0].x - person.x, path[0].y - person.y) < 6, `Agent movement did not start at the person's pointer: ${JSON.stringify(evidence)}`);
    assert.deepEqual(evidence.outsidePointer, {}, `Pointer events reached the page outside the magnified view: ${JSON.stringify(evidence)}`);
    return evidence;
  });

  await check('The agent pointer overlay is drawn where the pinched page receives input', async () => {
    assert(agentClick, 'The previous check made no agent click');
    const metrics = await settle(2), scale = clientScale(metrics);
    let shown, drawn, expected;
    const deadline = Date.now() + 4000;
    do {
      shown = await windows();
      if (shown.cursor?.visible && shown.page) {
        // The pointer image's hotspot sits 12 DIPs inside its window.
        const hotspot = Math.round(12 * shown.dpi / 96);
        drawn = { x: shown.cursor.left + hotspot, y: shown.cursor.top + hotspot };
        expected = { x: shown.page.left + agentClick.x * scale, y: shown.page.top + agentClick.y * scale };
        if (Math.hypot(drawn.x - expected.x, drawn.y - expected.y) <= 3) break;
      }
      await sleep(150);
    } while (Date.now() < deadline);
    const evidence = { click: agentClick, scale, drawn, expected, windows: shown };
    assert(drawn, `The agent pointer overlay is not shown: ${JSON.stringify(evidence)}`);
    assert(Math.hypot(drawn.x - expected.x, drawn.y - expected.y) <= 3, `The agent pointer overlay is not at the pinched input point: ${JSON.stringify(evidence)}`);
    return evidence;
  });

  await check('The inspector picks the element under a pinched window point', async () => {
    // The panel narrows the page, so it opens unpinched and the pinch follows.
    await driver('pinch', 0.5);
    await settle(1);
    await elements('open');
    await until(async () => { const state = await elements(); return state.visible && state.labels.includes('Pinch shown action') && state; }, 20_000);
    await driver('pinch', 2);
    const metrics = await settle(2), visual = metrics.cssVisualViewport;
    const listed = await until(async () => {
      const state = await elements(), node = name => state.nodes.find(item => item.name === name);
      return node('Pinch shown action') && node('Pinch second action') && Math.abs(node('Pinch shown action').viewport.width - visual.clientWidth) < 0.01 && state;
    }, 20_000);
    const node = listed.nodes.find(item => item.name === 'Pinch shown action');
    // Start from another row, so only a correct pick selects the target.
    await elements('select-element', 'Pinch second action');
    await until(async () => (await selected()).name === 'Pinch second action', 10_000);
    await elements('pick');
    await sleep(500);
    const scale = clientScale(metrics);
    await elements('page-pick', JSON.stringify({ bounds: node.bounds, viewport: node.viewport, clientScale: scale }));
    const details = await until(async () => { const value = await selected(); return value.name === 'Pinch shown action' && value.highlighted && value.highlightBounds && value; }, 10_000).catch(() => null);
    const evidence = { node: { bounds: node.bounds, viewport: node.viewport }, visual, scale, selected: details ? { name: details.name, highlightBounds: details.highlightBounds } : await selected() };
    assert(details, `Picking the pinched point did not select the element under it: ${JSON.stringify(evidence)}`);
    const bounds = details.highlightBounds;
    assert(Math.abs(bounds.pageScale - visual.scale) < 0.01, `The highlight must carry the pinch scale: ${JSON.stringify(evidence)}`);
    assert(Math.abs(bounds.x - (node.bounds.x - node.viewport.pageX)) < 2 && Math.abs(bounds.y - (node.bounds.y - node.viewport.pageY)) < 2,
      `Highlight bounds must be input coordinates: ${JSON.stringify(evidence)}`);
    return evidence;
  });

  // Selected from the list, so the outline is checked even if picking failed.
  await check('The inspector highlight is drawn around the pinched element', async () => {
    const metrics = await settle(2), scale = clientScale(metrics);
    await elements('select-element', 'Pinch shown action');
    const details = await until(async () => { const value = await selected(); return value.name === 'Pinch shown action' && value.highlighted && value.highlightBounds && value; }, 10_000);
    const bounds = details.highlightBounds;
    const isHighlight = (r, g, b) => Math.abs(r - 35) < 24 && Math.abs(g - 140) < 24 && b > 225;
    let shown, found, expected;
    const deadline = Date.now() + 5000;
    do {
      await elements('selection-state', undefined, 'pinch-highlight.png');
      shown = await windows();
      const area = shown.highlight;
      if (area?.visible) {
        const png = decodePng(await readFile(resolve(profile, 'pinch-highlight.png')));
        // Only the page area: the shell's own controls may use similar blues.
        found = undefined;
        for (let y = Math.max(0, area.top - shown.shell.top); y < area.bottom - shown.shell.top && y < png.height; y++)
          for (let x = Math.max(0, area.left - shown.shell.left); x < area.right - shown.shell.left && x < png.width; x++) {
            const i = (y * png.width + x) * png.channels;
            if (!isHighlight(png.pixels[i], png.pixels[i + 1], png.pixels[i + 2])) continue;
            found = found ? { left: Math.min(found.left, x), top: Math.min(found.top, y), right: Math.max(found.right, x), bottom: Math.max(found.bottom, y) } : { left: x, top: y, right: x, bottom: y };
          }
        const left = area.left - shown.shell.left, top = area.top - shown.shell.top;
        expected = { left: left + bounds.x * scale, top: top + bounds.y * scale, right: left + (bounds.x + bounds.width) * scale, bottom: top + (bounds.y + bounds.height) * scale };
        if (found && ['left', 'top', 'right', 'bottom'].every(edge => Math.abs(found[edge] - expected[edge]) <= 4)) break;
      }
      await sleep(300);
    } while (Date.now() < deadline);
    const evidence = { highlightBounds: bounds, scale, found, expected, windows: shown };
    assert(found, `No highlight rectangle was drawn on the page: ${JSON.stringify(evidence)}`);
    assert(['left', 'top', 'right', 'bottom'].every(edge => Math.abs(found[edge] - expected[edge]) <= 4), `The highlight is not drawn around the pinched element: ${JSON.stringify(evidence)}`);
    return evidence;
  });
} catch (error) {
  results.push({ name: 'Pinch-zoom harness', passed: false, error: error.message });
  console.log(`FAIL pinch-zoom harness: ${error.message}`);
} finally {
  if (client) await client.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null) {
    try {
      await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true });
      await until(() => browser.exitCode !== null || browser.signalCode !== null, 10_000);
      await until(async () => !(await alivePipe()), 15_000);
    } catch (error) { results.push({ name: 'Spawned process cleanup', passed: false, error: error.message }); }
  }
  if (fixture) await fixture.close();
  try {
    applicationDllSha256AtEnd = await hashDll();
    assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'Browser DLL changed during pinch-zoom validation');
  } catch (error) { results.push({ name: 'Tested binary stability', passed: false, error: error.message }); }
  const report = { run, capturedAt: new Date().toISOString(), binary: 'build/auth-fixture/Release/XenonAuthTest.exe', driver: 'XenonAuthTest synthetic touchpad pinch',
    applicationDllSha256, applicationDllSha256AtEnd, profile, passed: results.length >= 13 && results.every(result => result.passed), results };
  await mkdir(resolve(root, 'out'), { recursive: true });
  const reportPath = resolve(root, 'out/pinch-zoom-integration-results.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2));
  console.log(`Report: ${reportPath}`);
  process.exitCode = report.passed ? 0 : 1;
}
