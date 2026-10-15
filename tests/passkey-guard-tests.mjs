// Executes the production main-world passkey wrapper against a synthetic
// CredentialsContainer. These tests do not claim browser, Windows Hello or
// native gate acceptance; they cover the script's forwarding contract.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { webcrypto } from 'node:crypto';
import test from 'node:test';
import vm from 'node:vm';

const header = await readFile(new URL('../native/include/xenon/passkey_guard.hpp', import.meta.url), 'utf8');
const source = header.match(/passkey_guard_script\s*=\s*R"PASSKEYJS\(([\s\S]*?)\)PASSKEYJS"/u)?.[1];
assert(source, 'Actual production wrapper must be present');

function deferred() {
  let resolve, reject;
  const promise = new Promise((ok, fail) => { resolve = ok; reject = fail; });
  return { promise, resolve, reject };
}
function fixture({ binding = true, throwOnCall = false } = {}) {
  const messages = [], calls = [], timers = [];
  const record = (operation, self, args) => {
    const call = { operation, self, args: [...args], ...deferred() };
    calls.push(call);
    if (throwOnCall) throw new TypeError('synthetic synchronous failure');
    return call.promise;
  };
  const sandbox = { DOMException, crypto: webcrypto, setTimeout: (fn, ms) => { timers.push({ fn, ms }); return timers.length; }, __record: record };
  if (binding) sandbox.__xenon_passkey = payload => { messages.push(JSON.parse(payload)); };
  const context = vm.createContext(sandbox);
  vm.runInContext(`class CredentialsContainer {
    create(options) { return __record('create', this, arguments); }
    get(options) { return __record('get', this, arguments); }
    store(credential) { return __record('store', this, arguments); }
  }
  globalThis.CredentialsContainer = CredentialsContainer;
  globalThis.navigator = { credentials: new CredentialsContainer() };`, context);
  const install = () => vm.runInContext(source, context);
  install();
  const run = code => vm.runInContext(code, context);
  const decide = (id, allowed) => run(`globalThis.__xenonPasskeyDecision(${JSON.stringify(id)}, ${allowed})`);
  return { context, messages, calls, timers, install, run, decide };
}
const settle = () => new Promise(resolve => setImmediate(resolve));
const publicKeyOptions = extra => ({ publicKey: { challenge: new Uint8Array([1, 2, 3]), rpId: 'example.invalid', user: { name: 'SYNTHETIC_USER' } }, ...extra });

test('non-passkey requests pass straight through without a gate message', async () => {
  const f = fixture();
  assert.equal(f.run('typeof globalThis.__xenon_passkey'), 'undefined', 'The binding is hidden from page scripts');
  const options = { password: true };
  f.context.passwordOptions = options;
  f.run('navigator.credentials.get(passwordOptions)');
  f.run('navigator.credentials.get()');
  f.run('navigator.credentials.store({})');
  assert.equal(f.calls.length, 3);
  assert.equal(f.calls[0].args[0], options);
  assert.equal(f.messages.length, 0);
});

test('a passkey request waits for the native decision before reaching Chromium', async () => {
  const f = fixture();
  f.context.options = publicKeyOptions();
  const result = f.run('navigator.credentials.get(options)');
  await settle();
  assert.equal(f.calls.length, 0, 'The browser API is not called before the decision');
  assert.equal(f.messages.length, 1);
  const [request] = f.messages;
  assert.deepEqual(Object.keys(request).sort(), ['id', 'mediation', 'operation', 'type']);
  assert.equal(request.type, 'request'); assert.equal(request.operation, 'get'); assert.equal(request.mediation, 'optional');
  assert.match(request.id, /^[0-9a-f]{24}-1$/u);
  assert(!JSON.stringify(f.messages).includes('SYNTHETIC_USER'), 'Request options never leave the page');
  f.decide(request.id, true);
  await settle();
  assert.equal(f.calls.length, 1);
  assert.equal(f.calls[0].self, f.run('navigator.credentials'), 'The original receiver is preserved');
  assert.equal(f.calls[0].args[0], f.context.options, 'The original options object is forwarded');
  assert.equal(f.messages.length, 1, 'No settled notice before the ceremony ends');
  const credential = { id: 'synthetic' };
  f.calls[0].resolve(credential);
  assert.equal(await result, credential);
  assert.deepEqual(f.messages[1], { type: 'settled', id: request.id });
});

test('a refused request rejects with NotAllowedError and never reaches Chromium', async () => {
  const f = fixture();
  f.context.options = publicKeyOptions();
  const result = f.run('navigator.credentials.create(options)');
  await settle();
  assert.equal(f.messages[0].operation, 'create');
  f.decide(f.messages[0].id, false);
  await assert.rejects(result, error => error instanceof DOMException && error.name === 'NotAllowedError' && /person using the browser/u.test(error.message));
  assert.equal(f.calls.length, 0);
  assert.equal(f.messages.length, 1, 'A refusal sends no settled notice');
});

test('ceremony failures propagate and still report that the ceremony settled', async () => {
  const f = fixture();
  f.context.options = publicKeyOptions();
  const rejected = f.run('navigator.credentials.get(options)');
  await settle(); f.decide(f.messages[0].id, true); await settle();
  const failure = new DOMException('synthetic cancel', 'NotAllowedError');
  f.calls[0].reject(failure);
  await assert.rejects(rejected, error => error === failure);
  assert.deepEqual(f.messages[1], { type: 'settled', id: f.messages[0].id });

  const throwing = fixture({ throwOnCall: true });
  throwing.context.options = publicKeyOptions();
  const thrown = throwing.run('navigator.credentials.get(options)');
  await settle(); throwing.decide(throwing.messages[0].id, true);
  await assert.rejects(thrown, error => error instanceof TypeError);
  assert.deepEqual(throwing.messages[1], { type: 'settled', id: throwing.messages[0].id });
});

test('mediation is reported from a fixed set', async () => {
  const f = fixture();
  for (const mediation of ['conditional', 'required', 'silent', 'immediate', 'optional', 'automatic', undefined]) {
    f.context.options = publicKeyOptions(mediation === undefined ? {} : { mediation });
    f.run('navigator.credentials.get(options)');
  }
  await settle();
  assert.deepEqual(f.messages.map(message => message.mediation), ['conditional', 'required', 'silent', 'immediate', 'optional', 'optional', 'optional']);
  assert.equal(new Set(f.messages.map(message => message.id)).size, f.messages.length, 'Every request has its own ID');
});

test('forged or repeated decisions have no effect', async () => {
  const f = fixture();
  f.context.options = publicKeyOptions();
  const result = f.run('navigator.credentials.get(options)');
  await settle();
  f.decide('guessed-1', true);
  f.decide(`${f.messages[0].id}x`, true);
  await settle();
  assert.equal(f.calls.length, 0, 'Unknown IDs cannot release a request');
  f.decide(f.messages[0].id, false);
  f.decide(f.messages[0].id, true);
  await assert.rejects(result, error => error.name === 'NotAllowedError');
  assert.equal(f.calls.length, 0, 'A decision cannot be replaced');
  assert.equal(f.run('delete globalThis.__xenonPasskeyDecision'), false, 'The decision entry point cannot be removed');
  f.run('globalThis.__xenonPasskeyDecision = () => {}');
  assert.equal(f.run('typeof Object.getOwnPropertyDescriptor(globalThis, "__xenonPasskeyDecision").value'), 'function');
  assert.equal(f.run('Object.getOwnPropertyDescriptor(globalThis, "__xenonPasskeyDecision").writable'), false);
});

test('missing native binding or a missing answer fails closed', async () => {
  const unbound = fixture({ binding: false });
  unbound.context.options = publicKeyOptions();
  await assert.rejects(unbound.run('navigator.credentials.get(options)'), error => error.name === 'NotAllowedError');
  assert.equal(unbound.calls.length, 0);

  const silent = fixture();
  silent.context.options = publicKeyOptions();
  const result = silent.run('navigator.credentials.get(options)');
  await settle();
  assert.equal(silent.timers.length, 1); assert.equal(silent.timers[0].ms, 10000);
  silent.timers[0].fn();
  await assert.rejects(result, error => error.name === 'NotAllowedError');
  silent.decide(silent.messages[0].id, true);
  await settle();
  assert.equal(silent.calls.length, 0, 'A late approval after the timeout is ignored');
});

test('installation is idempotent and resists later page tampering', async () => {
  const f = fixture();
  f.install();
  f.context.__originalThen = f.run('Promise.prototype.then');
  f.run(`Map.prototype.get = () => () => {}; Map.prototype.delete = () => true;
    Promise.prototype.then = function () { throw new Error('tampered then'); };
    Promise.resolve = () => { throw new Error('tampered resolve'); };
    Array.prototype.includes = () => true; JSON.stringify = () => '{"type":"request","id":"forged","operation":"get","mediation":"optional"}';`);
  f.context.options = publicKeyOptions({ mediation: 'automatic' });
  const result = f.run('navigator.credentials.get(options)');
  await settle();
  assert.equal(f.messages.length, 1, 'A second installation does not wrap twice');
  assert.notEqual(f.messages[0].id, 'forged');
  assert.equal(f.messages[0].mediation, 'optional');
  f.decide(f.messages[0].id, true);
  await settle();
  assert.equal(f.calls.length, 1);
  f.calls[0].resolve('done');
  await settle();
  assert.deepEqual(f.messages[1], { type: 'settled', id: f.messages[0].id }, 'The wrapper settles with its captured built-ins');
  // Only this test's own await needs the page realm's original then.
  f.run('Promise.prototype.then = __originalThen');
  assert.equal(await result, 'done');
});
