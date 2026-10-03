// Public, synthetic credentials and a deliberately test-only certificate.
// This server binds loopback only and must never be exposed or deployed.
import https from 'node:https';
import { readFile } from 'node:fs/promises';
import { randomBytes } from 'node:crypto';

const username = 'XENON_TEST_USERNAME_CANARY_8e9a@example.invalid';
const password = 'XENON_TEST_PASSWORD_CANARY_73ab!';
const port = 18766;
const states = new Map();
const key = await readFile(new URL('./auth-key.pem', import.meta.url));
const cert = await readFile(new URL('./auth-cert.pem', import.meta.url));
const esc = value => String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('"', '&quot;');
function state(id) {
  if (!states.has(id)) states.set(id, { attempts: 0, getSubmissions: 0, credentialQueryReceived: false, usernameAccepted: false, authenticated: false, events: [], requests: 0 });
  return states.get(id);
}
function page(id, mode, stage = 'both', { method = 'post', readOnly = '' } = {}) {
  const usernameInput = stage === 'password' ? '' : `<label>Account<input name="username" autocomplete="username"${readOnly === 'username' ? ' readonly' : ''}></label>`;
  const passwordInput = stage === 'username' ? '' : `<label>Secret password<input name="password" type="password" autocomplete="current-password"${readOnly === 'password' ? ' readonly' : ''}></label>`;
  const destination = stage === 'username' ? '/username' : '/submit';
  const hold = mode === 'hold' && stage !== 'username';
  const rejectedFixture = method !== 'post' || readOnly !== '';
  return `<!doctype html><html><head><meta charset="utf-8"><title>Authentication fixture</title></head><body>
<h1>${rejectedFixture ? 'Initializing authentication fixture' : stage === 'username' ? 'Username-first fixture' : stage === 'password' ? 'Password step fixture' : 'Authentication fixture ready'}</h1>
<p>Only public synthetic test accounts are accepted here.</p>
<form id="login" method="${method}" action="${destination}?case=${encodeURIComponent(id)}&mode=${encodeURIComponent(mode)}">
${method === 'get' ? `<input type="hidden" name="case" value="${esc(id)}">` : ''}
${usernameInput}${passwordInput}<button type="submit"${mode === 'named' ? ' name="_eventId_proceed" value=""' : ''}>${stage === 'username' ? 'Next' : 'Sign in'}</button></form>
<p id="status"></p><button id="reveal" type="button">Reveal test password</button>
<script>
const id=${JSON.stringify(id)},mode=${JSON.stringify(mode)};
const report=(type,field)=>fetch('/event?case='+encodeURIComponent(id),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type,field,time:performance.now()})});
for(const type of ['input','change','submit','focusin','keydown','mousedown'])document.addEventListener(type,e=>report(type,e.target.name||e.target.id||e.target.tagName));
document.addEventListener('input',e=>{if(e.target.name==='password')console.log('Public synthetic console canary',e.target.value)});
document.querySelector('#reveal').onclick=()=>{const p=document.querySelector('[name=password]');if(p)p.type=p.type==='password'?'text':'password'};
${hold ? `document.querySelector('#login').addEventListener('submit',async e=>{e.preventDefault();const response=await fetch(e.target.action,{method:'POST',body:new URLSearchParams(new FormData(e.target))});const result=await response.json();document.querySelector('#status').textContent=result.authenticated?'Waiting for human MFA':'Login rejected';});` : ''}
${rejectedFixture ? "document.querySelector('h1').textContent='Authentication fixture ready';" : ''}
</script></body></html>`;
}
async function body(req) {
  let value = '';
  for await (const chunk of req) { value += chunk; if (value.length > 65536) throw new Error('Too large'); }
  return value;
}
const server = https.createServer({ key, cert }, async (req, res) => {
  try {
    const url = new URL(req.url, `https://127.0.0.1:${port}`);
    const id = url.searchParams.get('case') ?? 'default';
    const mode = url.searchParams.get('mode') ?? 'normal';
    const current = state(id); current.requests++;
    res.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/health') { res.end('ok'); return; }
    if (url.pathname === '/telemetry') {
      res.setHeader('Content-Type', 'application/json');
      res.end(JSON.stringify(current)); return;
    }
    if (url.pathname === '/event' && req.method === 'POST') {
      const event = JSON.parse(await body(req));
      current.events.push({ type: String(event.type).slice(0, 32), field: String(event.field).slice(0, 64), time: Number(event.time) });
      if (current.events.length > 1000) current.events.shift();
      res.end('ok'); return;
    }
    if (url.pathname === '/upload-check' && req.method === 'POST') {
      const data = JSON.parse(await body(req));
      current.uploadAccepted = data.name === 'approved-report.txt' && data.content === 'Xenon synthetic approved upload.\n';
      res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify({ accepted: current.uploadAccepted })); return;
    }
    if (url.pathname === '/username' && req.method === 'POST') {
      current.usernameAccepted = new URLSearchParams(await body(req)).get('username') === username;
      res.writeHead(303, { Location: `/password?case=${encodeURIComponent(id)}&mode=${encodeURIComponent(mode)}` }); res.end(); return;
    }
    if (url.pathname === '/submit' && req.method === 'GET') {
      current.getSubmissions++;
      current.credentialQueryReceived = url.searchParams.get('username') === username || url.searchParams.get('password') === password;
      res.setHeader('Content-Type', 'text/html; charset=utf-8');
      res.end('<!doctype html><html><body><h1>Unexpected GET credential submission</h1></body></html>'); return;
    }
    if (url.pathname === '/submit' && req.method === 'POST') {
      const data = new URLSearchParams(await body(req)); current.attempts++;
      current.submitterAccepted = data.has('_eventId_proceed');
      current.authenticated = (data.get('username') === username || current.usernameAccepted) && data.get('password') === password && (mode !== 'named' || current.submitterAccepted);
      // Model a Web Flow login: without the button event, stay on the login
      // screen without claiming a credential error, MFA or CAPTCHA.
      if (mode === 'named' && !current.submitterAccepted) { res.setHeader('Content-Type', 'text/html; charset=utf-8'); res.end(page(id, mode)); return; }
      if (current.authenticated) res.setHeader('Set-Cookie', `xenon_auth_test=${randomBytes(12).toString('hex')}; Secure; HttpOnly; SameSite=Strict; Path=/`);
      if (mode === 'hold') { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify({ authenticated: current.authenticated })); return; }
      res.writeHead(303, { Location: `/result?case=${encodeURIComponent(id)}` }); res.end(); return;
    }
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    if (url.pathname === '/result') {
      res.end(`<!doctype html><html><body><h1>${current.authenticated ? 'Synthetic sign-in completed' : 'Synthetic sign-in rejected'}</h1><p>No credentials appear on this result page.</p></body></html>`); return;
    }
    if (url.pathname === '/username-first') { res.end(page(id, mode, 'username')); return; }
    if (url.pathname === '/get-login') { res.end(page(id, 'normal', 'both', { method: 'get' })); return; }
    if (url.pathname === '/readonly-login') { res.end(page(id, 'normal', 'both', { readOnly: url.searchParams.get('field') })); return; }
    if (url.pathname === '/password') { res.end(page(id, mode, 'password')); return; }
    if (url.pathname === '/safe') { res.end(`<!doctype html><html><body><h1>Safe visual fixture</h1><p>Credentials are absent.</p><button id="ordinary">Ordinary control</button>
<script>document.querySelector('#ordinary').onclick=()=>fetch('/event?case='+encodeURIComponent(${JSON.stringify(id)}),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:'ordinary-click',field:'ordinary',time:performance.now()})});</script></body></html>`); return; }
    if (url.pathname === '/human-auth-sim') {
      const otp = url.searchParams.get('kind') === 'otp';
      res.end(`<!doctype html><html><body><h1 id="ready">Initializing synthetic input fixture</h1>
<label>Authentication value<input id="sensitive" type="${otp ? 'text' : 'password'}" ${otp ? 'autocomplete="section-login one-time-code"' : ''}></label>
<button id="simulate" type="button">Simulate authentication input</button><p id="status">Ready for a synthetic input event.</p>
<script>const report=(type,field)=>fetch('/event?case='+encodeURIComponent(${JSON.stringify(id)}),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type,field,time:performance.now()})});for(const type of ['mousedown','mouseup','click','focusin'])document.addEventListener(type,event=>report(type,event.target.id||event.target.tagName));document.querySelector('#simulate').onclick=()=>{const field=document.querySelector('#sensitive');field.value=${JSON.stringify(otp ? 'XENON_TEST_OTP_CANARY_92df' : password)};field.dispatchEvent(new Event('input',{bubbles:true}));setTimeout(()=>{field.type='text';field.removeAttribute('autocomplete');document.querySelector('#status').textContent='Synthetic value revealed; observers must remain sealed.';report('fixture-revealed',${JSON.stringify(otp ? 'otp' : 'password')})},100)};document.querySelector('#ready').textContent='Synthetic auth input fixture';</script></body></html>`); return;
    }
    if (url.pathname === '/upload') {
      res.end(`<!doctype html><html><body><h1>Approved upload fixture</h1><label>Approved fixture file<input id="upload" type="file"></label><p id="status"></p>
<script>document.querySelector('#upload').addEventListener('change',async e=>{const file=e.target.files[0];if(!file)return;const response=await fetch('/upload-check?case='+encodeURIComponent(${JSON.stringify(id)}),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:file.name,content:await file.text()})});const result=await response.json();document.querySelector('#status').textContent=result.accepted?'Approved upload received':'Upload rejected'});</script></body></html>`); return;
    }
    res.end(page(id, mode));
  } catch { res.writeHead(400).end('Invalid fixture request'); }
});
server.listen(port, '127.0.0.1', () => process.stdout.write('Synthetic HTTPS auth fixture listening on loopback:18766\n'));
const stop = () => { server.closeAllConnections(); server.close(() => process.exit(0)); };
process.on('SIGTERM', stop); process.on('SIGINT', stop);
