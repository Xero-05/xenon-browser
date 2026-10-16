// Synthetic loopback pages for tests/site-info-integration.ps1. /cookies sets
// two fixture cookies; every request logs which fixture cookies it carried.
import http from 'node:http';

const server = http.createServer((req, res) => {
  const path = new URL(req.url, 'http://127.0.0.1').pathname;
  const sent = (req.headers.cookie ?? '').split(';').map(part => part.trim().split('=')[0]).filter(name => name.startsWith('xenon_fixture_')).sort();
  process.stdout.write(`GET ${path} COOKIES ${sent.join(',') || '-'}\n`);
  const headers = { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' };
  if (path === '/cookies') headers['Set-Cookie'] = ['xenon_fixture_a=synthetic; Path=/; Max-Age=3600', 'xenon_fixture_b=synthetic; Path=/; Max-Age=3600; HttpOnly'];
  res.writeHead(200, headers);
  res.end(`<!doctype html><title>Site info fixture ${path}</title><h1>Site info fixture ${path}</h1>`);
});
server.listen(0, '127.0.0.1', () => process.stdout.write(`PORT ${server.address().port}\n`));
