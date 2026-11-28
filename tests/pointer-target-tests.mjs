// Deterministic checks of the exact fixed native target script. No OS input.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const header = await readFile(new URL('../native/include/xenon/pointer_target.hpp', import.meta.url), 'utf8');
const source = header.match(/R"JS\(([\s\S]*?)\)JS"/u)?.[1];
assert(source, 'Use the production native script');
function fixture({ rect = { x: 10, y: 20, width: 100, height: 40 }, hit, changed, detached = false, timeout = false, shadow = false, opacity = 1, ancestorOpacity = 1, pointerEvents = 'auto', ancestorPointerEvents = 'auto', hitAncestor = false, fileOverlay = null, text = false, fragmented = false, emptyRects = false, childOverlay = false } = {}) {
  let frames = 0;
  const bounds = value => ({ ...value, left: value.x, top: value.y, right: value.x + value.width, bottom: value.y + value.height });
  const blocker = { nodeType: 1, tagName: 'DIV', getRootNode: () => document };
  const element = {
    nodeType: 1,
    isConnected: !detached,
    getBoundingClientRect: () => bounds(frames >= 2 && changed ? changed : rect),
    contains: candidate => candidate === element || childOverlay && candidate === blocker,
    focus: () => { throw new Error('Target inspection must not focus the page'); },
  };
  const ancestor = { nodeType: 1, tagName: 'DIV', getRootNode: () => document, contains: candidate => candidate === ancestor || candidate === element };
  element.parentElement = ancestor;
  let overlayNode = null;
  const document = {
    elementFromPoint: (x, y) => {
      if (overlayNode && hit?.(x, y) !== false) return overlayNode;
      if (hitAncestor && hit?.(x, y) !== false) return ancestor;
      return childOverlay || hit?.(x, y) === false ? blocker : element;
    },
  };
  if (fileOverlay) {
    overlayNode = {
      nodeType: 1,
      tagName: fileOverlay.tagName ?? 'INPUT',
      type: fileOverlay.type ?? 'file',
      disabled: !!fileOverlay.disabled,
      inert: !!fileOverlay.inert,
      webkitdirectory: !!fileOverlay.webkitdirectory,
      ownerDocument: fileOverlay.otherDocument ? {} : document,
      parentElement: ancestor,
      getAttribute: key => (key === 'aria-disabled' && fileOverlay.ariaDisabled ? 'true' : null),
      matches: () => !!fileOverlay.disabledPseudo,
      getRootNode: () => document,
    };
  }
  element.ownerDocument = document;
  element.getRootNode = () => document;
  const target = text ? { nodeType: 3, isConnected: !detached, parentElement: element, ownerDocument: document } : element;
  document.createRange = () => ({
    selectNodeContents: node => assert.equal(node, target),
    getClientRects: () => {
      if (emptyRects) return [];
      const box = bounds(frames >= 2 && changed ? changed : rect);
      return fragmented ? [box, box] : [box];
    },
  });
  let shadowRow = null;
  if (shadow) {
    const host = { nodeType: 1, getRootNode: () => document, contains: candidate => candidate === host };
    shadowRow = { nodeType: 1, tagName: 'LI', getRootNode: () => shadowRoot, contains: candidate => candidate === shadowRow || candidate === element };
    const shadowRoot = {
      host,
      elementFromPoint: () => (overlayNode ? overlayNode : hitAncestor ? shadowRow : element),
    };
    element.parentElement = shadowRow;
    element.getRootNode = () => shadowRoot;
    if (overlayNode) {
      overlayNode.parentElement = shadowRow;
      overlayNode.getRootNode = () => shadowRoot;
    }
    document.elementFromPoint = () => host;
  }
  const fn = vm.runInNewContext(`(${source})`, {
    innerWidth: 800, innerHeight: 600,
    getComputedStyle: node => {
      if (node === overlayNode) return { display: 'block', visibility: 'visible', opacity: fileOverlay.opacity ?? 0, pointerEvents: 'auto' };
      if (node === element) return { display: 'block', visibility: 'visible', opacity, pointerEvents };
      return { display: 'block', visibility: 'visible', opacity: ancestorOpacity, pointerEvents: ancestorPointerEvents };
    },
    requestAnimationFrame: callback => { if (!timeout) { ++frames; callback(); } },
    setTimeout: callback => { if (timeout) callback(); },
  });
  return (fx = .3, fy = .3, fallback = true, allowFileOverlay = false, immediatePoint = null) =>
    fn.call(target, fx, fy, fallback, allowFileOverlay, immediatePoint);
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

test('DOM text uses its exact rendered range and retains the same point on revalidation', async () => {
  const pick = fixture({ text: true, rect: { x: -20, y: 20, width: 40, height: 15 } });
  const first = await pick(), second = await pick(first.sampleFx, first.sampleFy, false);
  assert.equal(first.error, undefined); assert(first.x > 0 && first.x < 20);
  assert.equal(first.x, second.x); assert.equal(first.y, second.y);
});
test('text hidden by a descendant overlay cannot use the parent container hit area', async () => {
  assert.equal((await fixture({ text: true, childOverlay: true })()).error, 'occluded');
});
test('text with ambiguous fragmented geometry is refused', async () => {
  assert.equal((await fixture({ text: true, fragmented: true })()).error, 'fragmented_text');
});
test('moving or invisible text is refused before press', async () => {
  assert.equal((await fixture({ text: true, changed: { x: 11, y: 20, width: 100, height: 40 } })()).error, 'layout_moving');
  assert.equal((await fixture({ text: true, opacity: 0 })()).error, 'not_visible');
  assert.equal((await fixture({ text: true, emptyRects: true })()).error, 'not_visible');
  assert.equal((await fixture({ text: true, detached: true })()).error, 'detached');
  assert.equal((await fixture({ text: true, detached: true, emptyRects: true })()).error, 'detached');
});
test('pointer-events:none text wrapper passes hits to its clickable ancestor in light and shadow DOM', async () => {
  const light = await fixture({ text: true, pointerEvents: 'none', hitAncestor: true })();
  assert.equal(light.error, undefined);
  assert(light.x > 10 && light.x < 110 && light.y > 20 && light.y < 60);

  const shadow = await fixture({ text: true, shadow: true, pointerEvents: 'none', hitAncestor: true })();
  assert.equal(shadow.error, undefined);
  assert(shadow.x > 10 && shadow.x < 110 && shadow.y > 20 && shadow.y < 60);

  // Ancestor hitting without pointer-events:none (for example z-index behind opaque parent) stays rejected.
  assert.equal((await fixture({ text: true, pointerEvents: 'auto', hitAncestor: true })()).error, 'occluded');
  assert.equal((await fixture({ text: true, shadow: true, pointerEvents: 'auto', hitAncestor: true })()).error, 'occluded');
});
test('transparent same-document file input overlay is allowed only for upload activation', async () => {
  const pick = fixture({ text: true, fileOverlay: { opacity: 0 } });
  // Ordinary click (allowFileOverlay=false) must refuse the overlay.
  assert.equal((await pick(.5, .5, true, false)).error, 'occluded');
  // Upload activation (allowFileOverlay=true) accepts the exact text hit area.
  const allowed = await pick(.5, .5, true, true);
  assert.equal(allowed.error, undefined);
  assert(allowed.x > 10 && allowed.x < 110);

  // Shadow DOM transparent file input overlay also works only when allowFileOverlay=true.
  const shadowPick = fixture({ text: true, shadow: true, fileOverlay: { opacity: 0 } });
  assert.equal((await shadowPick(.5, .5, true, false)).error, 'occluded');
  assert.equal((await shadowPick(.5, .5, true, true)).error, undefined);

  // Disabled, directory, cross-document, opaque or non-file-input overlays stay rejected even when allowFileOverlay=true.
  for (const bad of [
    { opacity: 0, disabled: true },
    { opacity: 0, ariaDisabled: true },
    { opacity: 0, webkitdirectory: true },
    { opacity: 0, otherDocument: true },
    { opacity: 0.5 },
    { opacity: 0, tagName: 'BUTTON', type: 'button' },
    { opacity: 0, type: 'text' },
  ]) {
    assert.equal((await fixture({ text: true, fileOverlay: bad })(.5, .5, true, true)).error, 'occluded');
  }
});
test('immediatePoint revalidation checks exact point without waiting on rAF and fails closed on occlusion or movement', async () => {
  const ok = await fixture({ text: true, timeout: true, rect: { x: 12, y: 20, width: 110, height: 40 } })(.5, .5, false, true, { x: 60, y: 40 });
  assert.equal(ok.error, undefined);
  assert.equal(ok.x, 60);
  assert.equal(ok.y, 40);

  assert.equal((await fixture({ text: true, timeout: true, rect: { x: 100, y: 20, width: 100, height: 40 } })(.5, .5, false, true, { x: 60, y: 40 })).error, 'layout_moving');
  assert.equal((await fixture({ text: true, timeout: true, hit: () => false })(.5, .5, false, true, { x: 60, y: 40 })).error, 'occluded');
  assert.equal((await fixture({ text: true, timeout: true, detached: true })(.5, .5, false, true, { x: 60, y: 40 })).error, 'detached');
});
