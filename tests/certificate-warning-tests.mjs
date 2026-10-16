// Executes the production enhanced-protection style script against a
// synthetic document. These tests cover the script's document check and
// timing; they do not claim acceptance on Chromium's warning page in Xenon.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const header = await readFile(new URL('../native/include/xenon/site_info.hpp', import.meta.url), 'utf8');
const source = header.match(/hide_enhanced_protection_script\s*=\s*R"WARNINGJS\(([\s\S]*?)\)WARNINGJS"/u)?.[1];
assert(source, 'Actual production script must be present');

function fixture({ url = 'chrome-error://chromewebdata/', head = true, root = true } = {}) {
  const listeners = [];
  const element = name => ({ name, children: [], textContent: '', appendChild(child) { this.children.push(child); return child; } });
  const document = {
    URL: url,
    head: head ? element('head') : null,
    documentElement: root ? element('html') : null,
    createElement: name => element(name),
    addEventListener: (type, listener, options) => listeners.push({ type, listener, options }),
  };
  vm.runInContext(source, vm.createContext({ document }));
  const styles = () => [document.head, document.documentElement].filter(Boolean).flatMap(parent => parent.children);
  return { document, listeners, styles };
}

test('Chromium error documents get the hiding rule in their head', () => {
  const { listeners, styles } = fixture();
  assert.equal(listeners.length, 0);
  assert.deepEqual(styles().map(style => [style.name, style.textContent]), [['style', '#enhanced-protection-message{display:none!important}']]);
});

test('websites are never changed', () => {
  for (const url of ['https://example.invalid/', 'https://example.invalid/chrome-error://chromewebdata/', 'chrome-error://chromewebdata/other', 'about:blank']) {
    const { listeners, styles } = fixture({ url });
    assert.equal(listeners.length + styles().length, 0, url);
  }
});

test('an early run without a head uses the root element', () => {
  const { document, styles } = fixture({ head: false });
  assert.equal(styles().length, 1);
  assert.equal(document.documentElement.children.length, 1);
});

test('a run before the document has a root waits for DOMContentLoaded once', () => {
  const { document, listeners, styles } = fixture({ head: false, root: false });
  assert.equal(styles().length, 0);
  assert.equal(listeners.length, 1);
  assert.equal(listeners[0].type, 'DOMContentLoaded');
  assert.equal(listeners[0].options?.once, true);
  document.head = { name: 'head', children: [], appendChild(child) { this.children.push(child); return child; } };
  listeners[0].listener();
  assert.deepEqual(document.head.children.map(style => style.textContent), ['#enhanced-protection-message{display:none!important}']);
});
