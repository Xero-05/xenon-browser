// Production CEF + real MCP against a synthetic scrolled application form.
// Verifies multi-line scrollable textareas below screen 1 (hollow border overlays,
// translate3d wrappers, viewport-straddling tall textareas, uncertain candidates
// under html,body{height:100%}), screenshot-grounded action:"type", and negative
// security guards (hidden/occluded/credential/stale-viewport/non-editable focus).
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
const run = 'textarea-fallback-' + Date.now() + '-' + randomBytes(4).toString('hex');
const profile = resolve(root, '.cache', run);
const pipeName = 'xenon-test-' + run;
const pipe = '\\\\.\\pipe\\' + pipeName;
const binary = resolve(buildRoot, 'app/Release/Xenon.exe');
const dll = resolve(buildRoot, 'app/Release/Xenon.dll');
const exec = promisify(execFile);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const hashDll = async () => sha256(await readFile(dll));
const results = [];
const state = { opened: false, scrollY: 0, values: {}, trusted: {}, focusAction: '' };
const prefilled = Array.from({ length: 24 }, (_, i) => `Line ${i + 1}: Synthetic prior text that overflows and scrolls inside this multi-line field.`).join('\n');

let browser, client, server, tab, applicationDllSha256, applicationDllSha256AtEnd, lastInspect;

const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipe);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => { socket.destroy(); resolve(false); });
});
async function until(action, timeout = 20_000, label = 'Condition') {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    try { const value = await action(); if (value) return value; } catch {}
    await sleep(120);
  }
  throw new Error(`${label} timed out`);
}
async function check(name, action) {
  const started = Date.now();
  try {
    const details = await action();
    results.push({ name, passed: true, elapsedMs: Date.now() - started, details });
    console.log('PASS ' + name);
  } catch (error) {
    results.push({ name, passed: false, elapsedMs: Date.now() - started, error: error.message });
    console.log('FAIL ' + name + ': ' + error.message);
  }
}
const scope = () => ({ agentSessionId: tab.agentSessionId, workspaceId: tab.workspaceId, tabId: tab.tabId });
const mutation = () => ({ ...scope(), ownershipGeneration: tab.ownershipGeneration, operationId: randomUUID() });
async function raw(name, args = {}) {
  return client.callTool({ name: 'xenon_' + name, arguments: args });
}
async function tool(name, args = {}) {
  const response = await raw(name, args);
  assert(!response.isError, `${name}: ${JSON.stringify(response.structuredContent ?? response.content)}`);
  return response.structuredContent;
}
async function inspectStable(extra = {}) {
  return until(async () => {
    const response = await raw('inspect', { ...scope(), maxNodes: 200, ...extra });
    if (!response.isError) { lastInspect = response.structuredContent; return response.structuredContent; }
    const code = response.structuredContent?.error?.code;
    if (['inspection_changed', 'privacy_guard_initializing', 'page_changed'].includes(code)) return false;
    throw new Error(`inspect failed: ${JSON.stringify(response.structuredContent ?? response.content)}`);
  }, 15_000, 'Stable inspect');
}

function pageHtml() {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Synthetic textarea fallback fixture</title><style>
    *{box-sizing:border-box}
    html,body{height:100%;margin:0;background:#fff;color:#142237;font:15px system-ui}
    body{overflow-x:hidden}
    .spacer{height:1100px;padding:16px}
    .form{max-width:700px;margin:0 auto;padding:16px}
    .row{margin:10px 0}
    label{display:block;margin-bottom:4px;font-weight:600}
    input,textarea{display:block;width:100%;font:inherit;padding:6px 8px;border:1px solid #555;background:#fff;color:#111}
    textarea{height:76px;overflow-y:auto}
    .ring-wrap{position:relative}
    .ring-wrap .ring-overlay{position:absolute;inset:0;border:2px solid #0b5;background:transparent;pointer-events:none;z-index:2}
    .t3d-wrap{transform:translate3d(0px, 4px, 0px)}
    .uncertain-wrap{clip-path:inset(0px round 20%);padding:4px}
    .clip-pane{height:56px;overflow:hidden;border:1px solid #999;padding:4px}
    .clip-pane textarea{margin-top:70px;height:48px}
    .straddle-section{margin-top:24px}
    #tall-ta{height:380px}
    .offscreen-probe{position:fixed;left:-5000px;top:40px;width:200px;height:32px}
    .occluded-wrap{position:relative;width:240px;height:36px}
    .occluded-cover{position:absolute;inset:0;background:#ddd;z-index:3}
    .masked-probe{-webkit-text-security:disc}
  </style></head><body>
    <div class="spacer"><h1>Top section</h1><p>Scrolled above the application section.</p></div>
    <main class="form" id="section-1">
      <h2>Application Details</h2>
      <div class="row">
        <label for="single-title">Role Title</label>
        <input id="single-title" type="text" value="Engineer">
      </div>
      <div class="row">
        <label for="ring-ta">Key Responsibilities</label>
        <div class="ring-wrap">
          <textarea id="ring-ta" rows="4">${prefilled}</textarea>
          <div class="ring-overlay"></div>
        </div>
      </div>
      <div class="row t3d-wrap">
        <label for="t3d-ta">Project Impact</label>
        <textarea id="t3d-ta" rows="4">${prefilled}</textarea>
      </div>
      <div class="row">
        <label for="uncertain-ta">Detailed Summary</label>
        <div class="uncertain-wrap">
          <textarea id="uncertain-ta" rows="4">${prefilled}</textarea>
        </div>
      </div>
      <div class="row">
        <div class="clip-pane"><textarea id="offpane-ta">Offpane hidden</textarea></div>
      </div>
      <div class="row">
        <button id="plain-btn" type="button">Focus button</button>
        <input id="offscreen-input" class="offscreen-probe" type="text" value="untouched">
        <div class="occluded-wrap">
          <input id="occluded-input" type="text" value="untouched">
          <div class="occluded-cover"></div>
        </div>
        <input id="dynamic-cred-input" type="text" value="untouched">
        <input id="masked-input" type="text" class="masked-probe" value="untouched">
      </div>
      <div class="row straddle-section">
        <label for="tall-ta">Extended Narrative</label>
        <textarea id="tall-ta" rows="16">${prefilled}</textarea>
      </div>
    </main>
    <div class="spacer">Bottom spacer</div>
    <script>
      function report(payload){return fetch('/event',{method:'POST',body:JSON.stringify(payload)});}
      for(const el of document.querySelectorAll('input,textarea')){
        el.addEventListener('input',e=>report({type:'input',id:el.id,value:el.value,trusted:e.isTrusted}));
      }
      addEventListener('scroll',()=>report({type:'scroll',y:Math.round(scrollY)}),{passive:true});
      let applied=0;
      async function poll(){try{
        const cmd=await(await fetch('/cmd',{cache:'no-store'})).json();
        if(cmd.version<=applied)return;
        if(cmd.action==='scroll-section'){document.getElementById('section-1').scrollIntoView({block:'start'});}
        else if(cmd.action==='scroll-ring'){document.getElementById('ring-ta').closest('.row').scrollIntoView({block:'center'});}
        else if(cmd.action==='scroll-t3d'){document.getElementById('t3d-ta').closest('.row').scrollIntoView({block:'center'});}
        else if(cmd.action==='scroll-uncertain'){document.getElementById('uncertain-ta').closest('.row').scrollIntoView({block:'center'});}
        else if(cmd.action==='scroll-probes'){document.getElementById('plain-btn').closest('.row').scrollIntoView({block:'center'});}
        else if(cmd.action==='scroll-straddle'){
          const ta=document.getElementById('tall-ta');
          const top=ta.getBoundingClientRect().top+scrollY;
          scrollTo(0,Math.max(0,Math.round(top-Math.max(60,innerHeight-100))));
        }
        else if(cmd.action==='focus-offscreen'){document.getElementById('offscreen-input').focus({preventScroll:true});}
        else if(cmd.action==='focus-occluded'){document.getElementById('occluded-input').focus({preventScroll:true});}
        else if(cmd.action==='focus-otp'){const el=document.getElementById('dynamic-cred-input');el.type='text';el.autocomplete='one-time-code';el.focus({preventScroll:true});}
        else if(cmd.action==='focus-password'){const el=document.getElementById('dynamic-cred-input');el.autocomplete='';el.type='password';el.focus({preventScroll:true});}
        else if(cmd.action==='restore-cred'){const el=document.getElementById('dynamic-cred-input');el.type='text';el.autocomplete='';}
        else if(cmd.action==='focus-masked'){document.getElementById('masked-input').focus({preventScroll:true});}
        else if(cmd.action==='focus-btn'){document.getElementById('plain-btn').focus({preventScroll:true});}
        applied=cmd.version;
        await fetch('/cmd/ack',{method:'POST',body:String(applied)});
      }catch{}}
      setInterval(poll,40);
      addEventListener('load',()=>{document.getElementById('section-1').scrollIntoView({block:'start'});report({type:'opened',y:Math.round(scrollY)});},{once:true});
    </script>
  </body></html>`;
}

let cmdState = { version: 0, action: '' }, ackVersion = 0;
async function sendPageCmd(action) {
  cmdState = { version: cmdState.version + 1, action };
  await until(() => ackVersion >= cmdState.version, 5_000, `Page command ${action}`);
  await sleep(180);
}

if (process.platform !== 'win32') throw new Error('Live textarea integration requires Windows.');
if (await alivePipe()) throw new Error('Allocated private pipe is already in use.');

try {
  await mkdir(profile, { recursive: true });
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_TEXTAREA_FIXTURE\n');
  await writeFile(resolve(profile, 'ui-settings.json'), JSON.stringify({
    version: 1, language: 'en-US', theme: 'light', introductionCompleted: true, automaticUpdateChecks: false,
  }));
  const identity = { clientId: 'textarea_' + randomBytes(8).toString('hex'), token: randomBytes(32).toString('hex'), pipe };
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({
    version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic textarea client', tokenHash: sha256(identity.token) }],
    workspaces: [{ id: 'native-default', clients: [identity.clientId] }],
    accountGrants: [], operations: [],
  }));
  const configPath = resolve(profile, 'client-config.json');
  await writePrivateConfig(configPath, identity);

  server = http.createServer((req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    if (req.url === '/cmd') { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(cmdState)); return; }
    if (req.url === '/cmd/ack' && req.method === 'POST') {
      let body = ''; req.on('data', c => { body += c; }); req.on('end', () => { ackVersion = Number(body) || ackVersion; res.end('ok'); }); return;
    }
    if (req.url === '/event' && req.method === 'POST') {
      let body = ''; req.on('data', c => { body += c; });
      req.on('end', () => {
        try {
          const msg = JSON.parse(body);
          if (msg.type === 'opened') { state.opened = true; state.scrollY = msg.y; }
          if (msg.type === 'scroll') state.scrollY = msg.y;
          if (msg.type === 'input') { state.values[msg.id] = msg.value; state.trusted[msg.id] = msg.trusted; }
        } catch {}
        res.end('ok');
      });
      return;
    }
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.end(pageHtml());
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const origin = `http://127.0.0.1:${server.address().port}`;

  applicationDllSha256 = await hashDll();
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { windowsHide: true, stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; });
  await until(async () => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error(`Browser exited early (${browser.exitCode})`);
    return alivePipe();
  }, 45_000, 'Private browser pipe');

  client = new Client({ name: 'Textarea fallback regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(new StdioClientTransport({
    command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath],
    stderr: 'pipe',
  }));

  const worker = await tool('worker_create', { name: 'Textarea worker', workspaceId: 'native-default' });
  tab = { ...worker, ...await tool('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: origin + '/' }) };
  await until(() => state.opened && state.scrollY >= 900, 15_000, 'Scrolled section 1 load');

  await check('Hollow border overlay and translate3d scrollable textareas below screen 1 get verified refs and fill cleanly', async () => {
    await sendPageCmd('scroll-section');
    const topInspected = await inspectStable({ roles: ['textbox'] });
    const single = topInspected.nodes.find(n => n.tag === 'INPUT' && n.name === 'Role Title' && n.ref);
    assert(single, `Single-line input ref missing: ${JSON.stringify(topInspected.nodes)}`);

    await sendPageCmd('scroll-ring');
    const ringInspected = await inspectStable({ roles: ['textbox'] });
    const ring = ringInspected.nodes.find(n => n.tag === 'TEXTAREA' && n.name === 'Key Responsibilities' && n.ref);
    assert(ring, `Hollow-border textarea ref missing: ${JSON.stringify(ringInspected.nodes)}`);
    await tool('interact', { ...mutation(), observationId: ringInspected.observationId, action: 'fill', elementRef: ring.ref, text: 'Verified hollow-border fill.' });
    await until(() => state.values['ring-ta'] === 'Verified hollow-border fill.', 5_000, 'ring-ta fill');

    await sendPageCmd('scroll-t3d');
    const t3dInspected = await inspectStable({ roles: ['textbox'] });
    const t3d = t3dInspected.nodes.find(n => n.tag === 'TEXTAREA' && n.name === 'Project Impact' && n.ref);
    assert(t3d, `translate3d textarea ref missing: ${JSON.stringify(t3dInspected.nodes)}`);
    await tool('interact', { ...mutation(), observationId: t3dInspected.observationId, action: 'fill', elementRef: t3d.ref, text: 'Verified translate3d fill.' });
    await until(() => state.values['t3d-ta'] === 'Verified translate3d fill.', 5_000, 't3d-ta fill');
    assert.equal(state.trusted['ring-ta'], true);
    assert.equal(state.trusted['t3d-ta'], true);
    return { ringRef: true, t3dRef: true, trusted: true, viewport: t3dInspected.viewport, scaleX: t3dInspected.scaleX };
  });

  await check('Uncertain textarea below screen 1 is surfaced with its label and updated via screenshot click + Ctrl+A + Backspace + type', async () => {
    await sendPageCmd('scroll-uncertain');
    const inspected = await inspectStable({ roles: ['textbox'] });
    const uncertain = inspected.nodes.find(n => n.tag === 'TEXTAREA' && n.name === 'Detailed Summary' && n.visibilityStatus === 'uncertain');
    assert(uncertain, `Uncertain textarea below screen 1 missing from roles:["textbox"]: ${JSON.stringify(inspected.nodes)}`);
    assert(!inspected.nodes.some(n => n.tag === 'TEXTAREA' && Math.round(n.bounds.height) === 48), `Off-pane clipped textarea leaked into nodes: ${JSON.stringify(inspected.nodes)}`);
    assert(!uncertain.ref && uncertain.candidateId && uncertain.screenshotRequired === true);

    // candidateId cannot be used as elementRef
    const deniedRef = await raw('interact', { ...mutation(), observationId: inspected.observationId, action: 'fill', elementRef: uncertain.candidateId, text: 'denied' });
    assert.equal(deniedRef.isError, true, 'candidateId must not work as fill elementRef');

    // Screenshot-grounded click -> Ctrl+A -> Backspace -> type with punctuation and newline
    const cx = uncertain.bounds.x - inspected.viewport.pageX + uncertain.bounds.width / 2;
    const cy = uncertain.bounds.y - inspected.viewport.pageY + uncertain.bounds.height / 2;
    await tool('interact', { ...mutation(), observationId: inspected.observationId, action: 'click', x: cx, y: cy });
    await tool('interact', { ...mutation(), observationId: inspected.observationId, action: 'key', key: 'Ctrl+A' });
    await tool('interact', { ...mutation(), observationId: inspected.observationId, action: 'key', key: 'Backspace' });
    const payload = "Built C++ & Node.js tools (2024-2026).\nShipped 'zero-downtime' QA, CI/CD & releases.";
    await tool('interact', { ...mutation(), observationId: inspected.observationId, action: 'type', text: payload });
    await until(() => state.values['uncertain-ta'] === payload, 5_000, 'uncertain-ta typed value');
    assert.equal(state.trusted['uncertain-ta'], true, 'Input.insertText must produce trusted input events');
    return { candidateId: uncertain.candidateId, typedChars: payload.length, trusted: true };
  });

  await check('Negative security guards reject type on non-editable, offscreen, occluded, CSS-masked, stale-obs and stale-viewport focus', async () => {
    await sendPageCmd('scroll-probes');
    const shot = await inspectStable();

    // Non-editable button
    await sendPageCmd('focus-btn');
    const btnRes = await raw('interact', { ...mutation(), observationId: shot.observationId, action: 'type', text: 'blocked' });
    assert.equal(btnRes.isError, true);
    assert.equal(btnRes.structuredContent?.error?.code, 'not_editable');

    // Offscreen hidden focus
    await sendPageCmd('focus-offscreen');
    const offRes = await raw('interact', { ...mutation(), observationId: shot.observationId, action: 'type', text: 'blocked' });
    assert.equal(offRes.isError, true);
    assert.equal(offRes.structuredContent?.error?.code, 'not_actionable');
    assert.equal(state.values['offscreen-input'], undefined);

    // Occluded focus
    await sendPageCmd('focus-occluded');
    const occRes = await raw('interact', { ...mutation(), observationId: shot.observationId, action: 'type', text: 'blocked' });
    assert.equal(occRes.isError, true);
    assert.equal(occRes.structuredContent?.error?.code, 'not_actionable');
    assert.equal(state.values['occluded-input'], undefined);

    // CSS -webkit-text-security masked field (caught directly by focused_type_check_script)
    await sendPageCmd('focus-masked');
    const maskRes = await raw('interact', { ...mutation(), observationId: shot.observationId, action: 'type', text: 'secret' });
    assert.equal(maskRes.isError, true);
    assert.equal(maskRes.structuredContent?.error?.code, 'protected_field');
    assert.equal(state.values['masked-input'], undefined);

    // Plain observe (no screenshot) rejected as stale_observation
    const plainObs = await tool('observe', { ...scope(), maxNodes: 200 });
    const noShotRes = await raw('interact', { ...mutation(), observationId: plainObs.observationId, action: 'type', text: 'blocked' });
    assert.equal(noShotRes.isError, true);
    assert.equal(noShotRes.structuredContent?.error?.code, 'stale_observation');

    // Scroll after screenshot -> stale_viewport
    const shotBeforeScroll = await inspectStable();
    await sendPageCmd('scroll-straddle');
    const staleVp = await raw('interact', { ...mutation(), observationId: shotBeforeScroll.observationId, action: 'type', text: 'blocked' });
    assert.equal(staleVp.isError, true);
    assert.equal(staleVp.structuredContent?.error?.code, 'stale_viewport');
    return { rejectedCases: 6 };
  });

  await check('Tall viewport-straddling textarea receives unoccluded_visible_patch ref and fills cleanly', async () => {
    const inspected = await inspectStable({ roles: ['textbox'] });
    const tall = inspected.nodes.find(n => n.tag === 'TEXTAREA' && n.name === 'Extended Narrative' && n.ref);
    assert(tall, `Viewport-straddling tall textarea missing verified ref: ${JSON.stringify(inspected.nodes)}`);
    assert.equal(tall.geometryEvidence, 'unoccluded_visible_patch');
    await tool('interact', { ...mutation(), observationId: inspected.observationId, action: 'fill', elementRef: tall.ref, text: 'Filled straddling tall textarea.' });
    await until(() => state.values['tall-ta'] === 'Filled straddling tall textarea.', 5_000, 'tall-ta fill');
    assert.equal(state.trusted['tall-ta'], true);
    return { geometryEvidence: tall.geometryEvidence, bounds: tall.bounds, viewport: inspected.viewport };
  });

  await check('Dynamic OTP and password focus seal the tab and reject type without mutating the field', async () => {
    await sendPageCmd('scroll-section');
    const shot = await inspectStable();
    await sendPageCmd('focus-otp');
    const otpRes = await raw('interact', { ...mutation(), observationId: shot.observationId, action: 'type', text: '123456' });
    assert.equal(otpRes.isError, true);
    assert(['OBSERVATION_REQUIRED', 'protected_auth', 'protected_field'].includes(otpRes.structuredContent?.error?.code),
      `Unexpected OTP rejection: ${JSON.stringify(otpRes.structuredContent)}`);
    await sendPageCmd('focus-password');
    const pwdRes = await raw('interact', { ...mutation(), observationId: shot.observationId, action: 'type', text: 'secret123' });
    assert.equal(pwdRes.isError, true);
    assert(['OBSERVATION_REQUIRED', 'protected_auth', 'protected_field'].includes(pwdRes.structuredContent?.error?.code),
      `Unexpected password rejection: ${JSON.stringify(pwdRes.structuredContent)}`);
    assert.equal(state.values['dynamic-cred-input'], undefined);
    const sealed = await raw('observe', { ...scope(), maxNodes: 200 });
    assert.equal(sealed.isError, true);
    assert.equal(sealed.structuredContent?.error?.code, 'SENSITIVE_AUTH_IN_PROGRESS');
    return { otpRejectedCode: otpRes.structuredContent?.error?.code, passwordRejectedCode: pwdRes.structuredContent?.error?.code, sealedCode: sealed.structuredContent?.error?.code };
  });
} catch (error) {
  results.push({ name: 'Textarea fallback harness', passed: false, error: error.message });
  console.log('FAIL harness: ' + error.message);
} finally {
  if (lastInspect) await writeFile(resolve(profile, 'last-inspect.json'), JSON.stringify(lastInspect, null, 2)).catch(() => {});
  if (client) await client.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null) {
    await exec('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
    await until(() => browser.exitCode !== null || browser.signalCode !== null, 10_000).catch(() => {});
    await until(async () => !(await alivePipe()), 15_000).catch(() => {});
  }
  if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
  try {
    applicationDllSha256AtEnd = await hashDll();
    assert.equal(applicationDllSha256AtEnd, applicationDllSha256, 'Browser DLL changed during test');
  } catch (error) { results.push({ name: 'Binary stability', passed: false, error: error.message }); }
  const report = {
    run, capturedAt: new Date().toISOString(), binary: 'build/app/Release/Xenon.exe',
    applicationDllSha256, applicationDllSha256AtEnd, profile,
    passed: results.length === 5 && results.every(r => r.passed), results,
  };
  await mkdir(resolve(root, 'out'), { recursive: true });
  const reportPath = resolve(root, 'out', run + '.json');
  await writeFile(reportPath, JSON.stringify(report, null, 2));
  console.log('Report: ' + reportPath);
  process.exitCode = report.passed ? 0 : 1;
}
