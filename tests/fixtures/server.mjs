import http from 'node:http';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';

const port = Number(process.env.XENON_FIXTURE_PORT ?? 18765);
const html = await readFile(new URL('./workbench.html', import.meta.url));
const sockets = new Set();
const states = new Map();
const gestures = new Map();
const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://127.0.0.1:${port}`);
  res.setHeader('Cache-Control', 'no-store');
  if(url.pathname === '/gesture') {
    const id=url.searchParams.get('id');
    if(req.method==='POST') gestures.set(id, url.searchParams.get('phase'));
    res.setHeader('Content-Type','application/json');res.end(JSON.stringify({phase:gestures.get(id)??'none'}));return;
  }
  if (url.pathname === '/telemetry' && req.method === 'POST') {
    let body = ''; req.on('data', chunk => { body += chunk; if(body.length > 65536) req.destroy(); });
    req.on('end', () => { try { states.set(url.searchParams.get('id'), JSON.parse(body)); res.end('ok'); } catch { res.writeHead(400).end(); } }); return;
  }
  if (url.pathname === '/telemetry') { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(url.searchParams.has('all') ? Object.fromEntries(states) : states.get(url.searchParams.get('id')) ?? null)); return; }
  if (url.pathname === '/slow') {
    setTimeout(() => { res.setHeader('Content-Type', 'text/plain'); res.end('Slow request completed'); }, 5000); return;
  }
  if (url.pathname === '/download') {
    res.writeHead(200, { 'Content-Type': 'text/plain', 'Content-Disposition': 'attachment; filename="xenon-report.txt"' });
    res.end('Xenon download fixture\nNo personal data.\n'); return;
  }
  if (url.pathname === '/frame') {
    res.setHeader('Content-Type', 'text/html');
    const label = url.searchParams.get('kind') === 'cross' ? 'Cross origin' : 'Same origin';
    res.end(`<!doctype html><html><body><h2>Frame content</h2><button onclick="this.textContent='${label} frame clicked'">${label} frame button</button></body></html>`); return;
  }
  if (url.pathname === '/cookie') {
    if (url.searchParams.has('set')) res.setHeader('Set-Cookie', `workspace=${encodeURIComponent(url.searchParams.get('set'))}; Path=/; SameSite=Lax`);
    res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify({ cookie: req.headers.cookie ?? '' })); return;
  }
  if (url.pathname === '/health') { res.end('ok'); return; }
  res.setHeader('Content-Type', 'text/html; charset=utf-8'); res.end(html);
});
server.on('upgrade', (request, socket) => {
  const key = request.headers['sec-websocket-key'];
  if (typeof key !== 'string') { socket.destroy(); return; }
  const accept = createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  sockets.add(socket);
  let sequence = 0;
  const timer = setInterval(() => { const payload = Buffer.from(String(++sequence)); socket.write(Buffer.concat([Buffer.from([0x81, payload.length]), payload])); }, 250);
  socket.on('error', () => {});
  socket.on('close', () => { clearInterval(timer); sockets.delete(socket); });
});
server.listen(port, '127.0.0.1', () => process.stdout.write(`Fixture server: http://127.0.0.1:${port}\n`));
function stop() { for (const socket of sockets) socket.destroy(); server.close(() => process.exit(0)); }
process.on('SIGTERM', stop); process.on('SIGINT', stop);
