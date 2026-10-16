// Synthetic loopback pages for tests/browser-essentials-integration.ps1. Every
// request logs its path and the fixture cookies it carried. The test's link
// scheme is this server's argument; no request text is placed in a page
// without HTML escaping.
import http from 'node:http';
import zlib from 'node:zlib';

// A 16x16 solid red PNG, so the test can find the site icon in the tab list.
function redIcon() {
  const crc = (buffer) => { let c = ~0; for (const byte of buffer) { c ^= byte; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1)); } return ~c >>> 0; };
  const chunk = (type, data) => { const body = Buffer.concat([Buffer.from(type), data]); const out = Buffer.alloc(12 + data.length); out.writeUInt32BE(data.length, 0); body.copy(out, 4); out.writeUInt32BE(crc(body), 8 + data.length); return out; };
  const header = Buffer.alloc(13); header.writeUInt32BE(16, 0); header.writeUInt32BE(16, 4); header[8] = 8; header[9] = 2;
  const rows = Buffer.alloc(16 * (1 + 16 * 3)); for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) rows[y * 49 + 1 + x * 3] = 255;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header), chunk('IDAT', zlib.deflateSync(rows)), chunk('IEND', Buffer.alloc(0))]);
}
const icon = redIcon();
const scheme = /^[a-z][a-z0-9+.-]{1,63}$/.test(process.argv[2] ?? '') ? process.argv[2] : 'xenon-essentials-unset';
const escapeHtml = text => String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
const page = (title, body = '') => `<!doctype html><title>${escapeHtml(title)}</title><body style="margin:0">${body}<h1>${escapeHtml(title)}</h1></body>`;

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  const sent = (req.headers.cookie ?? '').split(';').map(part => part.trim().split('=')[0]).filter(name => name.startsWith('xenon_essentials')).sort();
  process.stdout.write(`GET ${url.pathname} COOKIES ${sent.join(',') || '-'} QUERY ${url.search || '-'}\n`);
  const headers = { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' };
  if (url.pathname === '/icon.png') { res.writeHead(200, { 'Content-Type': 'image/png', 'Cache-Control': 'no-store' }); res.end(icon); return; }
  if (url.pathname === '/favicon.ico') { res.writeHead(404); res.end(); return; }
  let html;
  if (url.pathname === '/icon-page') html = `<!doctype html><title>Essentials icon</title><link rel="icon" href="/icon.png"><h1>Essentials icon</h1>`;
  else if (url.pathname === '/cookie') { headers['Set-Cookie'] = ['xenon_essentials=synthetic; Path=/; Max-Age=3600']; html = page('Essentials cookie'); }
  // A page that asks Windows to open another app as soon as it loads.
  else if (url.pathname === '/app-link') html = page('Essentials app link', `<script>location.href=${JSON.stringify(scheme + ':probe value "quoted"&x=1')}</script>`);
  else if (url.pathname === '/denied-link') html = page('Essentials denied link', `<script>location.href='ms-msdt:/id PCWDiagnostic'</script>`);
  // The whole page is a button that asks to share the screen.
  else if (url.pathname === '/share') html = page('Essentials share', `<button id="share" style="position:fixed;inset:0;width:100%;height:100%" onclick="navigator.mediaDevices.getDisplayMedia({video:true}).then(()=>document.title='Share allowed',e=>document.title='Share '+e.name)">Share</button>`);
  else html = page(`Essentials ${url.pathname.slice(1)}`);
  res.writeHead(200, headers);
  res.end(html);
});
server.listen(0, '127.0.0.1', () => process.stdout.write(`PORT ${server.address().port}\n`));
