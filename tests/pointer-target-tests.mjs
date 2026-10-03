// Deterministic checks of the exact fixed native target script. No OS input.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const header = await readFile(new URL('../native/include/xenon/pointer_target.hpp', import.meta.url), 'utf8');
const source = header.match(/R"JS\(([\s\S]*?)\)JS"/u)?.[1];
assert(source, 'Use the production native script');
function fixture({ rect = { x: 10, y: 20, width: 100, height: 40 }, hit, changed, detached = false, timeout = false, shadow = false, opacity = 1, ancestorOpacity = 1 } = {}) {
  let frames = 0;
  const bounds = value => ({ ...value, left: value.x, top: value.y, right: value.x + value.width, bottom: value.y + value.height });
  const blocker = {};
  const element = {
    isConnected: !detached,
    getBoundingClientRect: () => bounds(frames >= 2 && changed ? changed : rect),
    contains: candidate => candidate === element,
    focus: () => { throw new Error('Target inspection must not focus the page'); },
  };
  const document = { elementFromPoint: (x, y) => hit?.(x, y) === false ? blocker : element };
  element.ownerDocument = document;
  element.getRootNode = () => document;
  const ancestor = { getRootNode: () => document };element.parentElement=ancestor;
  if (shadow) {
    const host = { getRootNode: () => document, contains: candidate => candidate === host };
    document.elementFromPoint = () => host;
    element.getRootNode = () => ({ host, elementFromPoint: () => element });
  }
  const fn = vm.runInNewContext(`(${source})`, {
    innerWidth: 800, innerHeight: 600,
    getComputedStyle: node => ({ display: 'block', visibility: 'visible', opacity: node === element ? opacity : ancestorOpacity }),
    requestAnimationFrame: callback => { if (!timeout) { ++frames; callback(); } },
    setTimeout: callback => { if (timeout) callback(); },
  });
  return (fx = .3, fy = .3, fallback = true) => fn.call(element, fx, fy, fallback);
}

test('randomized targets stay inside verified visible interiors', async () => {
  for (let seed = 0; seed < 128; ++seed) {
    const result = await fixture()(.25 + (seed % 31) / 62, .25 + (seed % 23) / 46);
    assert(result.x > 10 && result.x < 110 && result.y > 20 && result.y < 60);
    assert.equal(result.error, undefined);
  }
});
test('partly covered randomized spot falls back to an unobscured center', async () => {
  const result = await fixture({ hit: (_x, y) => y >= 36 })(.3, .25);
  assert.equal(result.sampleFx, .5); assert.equal(result.sampleFy, .5);
  assert.equal(result.x, 60); assert.equal(result.y, 40);
});
test('fully covered targets fail closed', async () => {
  assert.equal((await fixture({ hit: () => false })()).error, 'occluded');
});
test('final revalidation does not select a different point', async () => {
  const result = await fixture({ hit: (x) => x >= 50 })(.3, .3, false);
  assert.equal(result.error, 'occluded');
});
test('clipped targets retain exact coordinates on revalidation', async () => {
  const pick = fixture({ rect: { x: -20, y: 20, width: 40, height: 40 } });
  const first = await pick(); const second = await pick(first.sampleFx, first.sampleFy, false);
  assert(first.x > 0); assert.equal(first.x, second.x); assert.equal(first.y, second.y);
});
test('tiny targets use their visible center', async () => {
  const result = await fixture({ rect: { x: 10, y: 20, width: 4, height: 6 } })();
  assert.equal(result.x, 12); assert.equal(result.y, 23);
});
test('stale moving geometry rejects before press', async () => {
  assert.equal((await fixture({ changed: { x: 11, y: 20, width: 100, height: 40 } })()).error, 'layout_moving');
});
test('detached targets reject before press', async () => {
  assert.equal((await fixture({ detached: true })()).error, 'detached');
});
test('offscreen targets reject before press', async () => {
  assert.equal((await fixture({ rect: { x: 900, y: 20, width: 40, height: 40 } })()).error, 'outside_viewport');
});
test('empty geometry rejects before press', async () => {
  assert.equal((await fixture({ rect: { x: 10, y: 20, width: 0, height: 40 } })()).error, 'not_visible');
});
test('bounded render wait rejects unavailable frames', async () => {
  assert.equal((await fixture({ timeout: true })()).error, 'render_unready');
});
test('shadow roots validate both host and actual target', async () => {
  assert.equal((await fixture({ shadow: true })()).error, undefined);
});
test('opacity changes reject an invisible target before press', async () => {
  assert.equal((await fixture({ opacity: 0 })()).error, 'not_visible');
});
test('ancestor opacity cannot be overridden by the target', async () => {
  assert.equal((await fixture({ ancestorOpacity: 0 })()).error, 'not_visible');
});
