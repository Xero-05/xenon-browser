// Run the exact production classifier without executing any page activation.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const header = await readFile(new URL('../native/include/xenon/upload_entry.hpp', import.meta.url), 'utf8');
const source = header.match(/R"JS\(([\s\S]*?)\)JS"/u)?.[1];
assert(source);
const classify = vm.runInNewContext(`(${source})`);
const element = (tagName = 'SPAN', properties = {}) => ({
  nodeType: 1, isConnected: true, tagName,
  getAttribute: () => null, matches: () => false, getRootNode: () => ({}),
  click: () => { throw new Error('Classification must not activate the page'); },
  querySelector: () => { throw new Error('Classification must not search for inputs'); },
  ...properties,
});
const text = parentElement => ({ nodeType: 3, isConnected: true, parentElement });

test('observed StaticText can be an upload entry without semantic button markup', () => {
  assert.equal(classify.call(text(element()), true), 'entry');
  assert.equal(classify.call(element(), true), 'entry');
  assert.equal(classify.call(text(element()), false), 'unsupported');
  assert.equal(classify.call(element(), false), 'unsupported');
});
test('existing file inputs and semantic entries retain their classification', () => {
  assert.equal(classify.call(element('INPUT', { type: 'file' }), false), 'input');
  assert.equal(classify.call(element('INPUT', { type: 'file', webkitdirectory: true }), false), 'directory');
  for (const tag of ['BUTTON', 'A', 'LABEL']) assert.equal(classify.call(element(tag), false), 'entry');
});
test('disabled, inert and aria-disabled ancestors block text entry activation', () => {
  for (const properties of [{ disabled: true }, { inert: true }, { getAttribute: key => key === 'aria-disabled' ? 'true' : null }, { matches: () => true }]) {
    const parent = element('DIV', properties);
    assert.equal(classify.call(text(element('SPAN', { parentElement: parent })), true), 'unsupported');
  }
});
test('shadow host disability is checked and malformed deep chains fail closed', () => {
  const host = element('DIV', { disabled: true });
  assert.equal(classify.call(text(element('SPAN', { getRootNode: () => ({ host }) })), true), 'unsupported');
  const cycle = element(); cycle.parentElement = cycle;
  assert.equal(classify.call(text(cycle), true), 'unsupported');
});
test('detached text and missing parents cannot be upload entries', () => {
  assert.equal(classify.call({ ...text(element()), isConnected: false }, true), 'unsupported');
  assert.equal(classify.call(text(null), true), 'unsupported');
});
