// Synthetic loopback pages for tests/omnibox-integration.ps1. Titles are fixture text only.
import http from 'node:http';

const pages = {
  '/alpha': 'Alpha synthetic page',
  '/beta': 'Beta synthetic page',
  '/docs/guide': 'Synthetic omnibox guide',
};
const server = http.createServer((req, res) => {
  const path = new URL(req.url, 'http://127.0.0.1').pathname;
  const title = pages[path] ?? 'Synthetic root';
  process.stdout.write(`GET ${path}\n`);
  res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(`<!doctype html><title>${title}</title><h1>${title}</h1>`);
});
server.listen(0, '127.0.0.1', () => process.stdout.write(`PORT ${server.address().port}\n`));
