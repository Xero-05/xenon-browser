// Runs the exact production isolated-world script, with a deterministic DOM and
// clock. These are renderer logic tests, not proof of native physical input or
// end-to-end Save/Update UI behavior. No browser or user profile is opened.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const header = await readFile(new URL('../native/include/xenon/login_monitor.hpp', import.meta.url), 'utf8');
const source = header.match(/login_capture_script\s*=\s*R"XENONJS\(([\s\S]*?)\)XENONJS"/u)?.[1];
assert.ok(source, 'Production login_capture_script raw string must be extractable');
const canaryUser = 'SYNTHETIC_CAPTURE_ACCOUNT@example.invalid';
const canaryPassword = 'SYNTHETIC_CAPTURE_PASSWORD_7f94';

function fixture({ protocol = 'https:', subframe = false } = {}) {
  const messages = [], calls = [], inputs = [], nodes = [], listeners = new Map(), observers = [];
  let now = 0, taskId = 0, tokenId = 0;
  const tasks = new Map();
  class Element {
    constructor() { this.isConnected = true; this.disabled = false; this.readOnly = false; this.hidden = false; this.textContent = ''; this.attributes = new Map(); this.style = { visibility: 'visible', display: 'block', opacity: '1' }; }
    getClientRects() { return this.isConnected && !this.hidden && this.style.display !== 'none' && (!this.form || this.form.isConnected && !this.form.hidden && this.form.style.display !== 'none') ? [{ width: 120, height: 30 }] : []; }
    getBoundingClientRect() { return this.getClientRects()[0] || { width: 0, height: 0 }; }
    getAttribute(name) { return this.attributes.get(name) ?? null; }
    hasAttribute(name) { return this.attributes.has(name); }
    setAttribute(name, value) { this.attributes.set(name, String(value)); }
  }
  class Input extends Element {
    constructor({ type = 'text', autocomplete = '', value = '' } = {}) { super(); this.type = type; this.autocomplete = autocomplete; this.value = value; this.form = null; inputs.push(this); }
  }
  class Form extends Element {
    constructor() { super(); this.method = 'post'; this.target = ''; this.action = `${protocol}//capture.example.invalid/login`; this.elements = []; }
    add(input) { input.form = this; this.elements.push(input); return input; }
  }
  const document = {
    querySelectorAll(selector) {
      if (selector === 'input') return inputs.filter(input => input.isConnected && (!input.form || input.form.isConnected));
      if (selector === '[aria-invalid="true"],[role="alert"]') return nodes.concat(inputs).filter(node => node.isConnected && (node.getAttribute('aria-invalid') === 'true' || node.getAttribute('role') === 'alert'));
      throw new Error(`Unsupported synthetic DOM selector: ${selector}`);
    },
    addEventListener(name, callback) { if (!listeners.has(name)) listeners.set(name, []); listeners.get(name).push(callback); },
  };
  const context = vm.createContext({
    document, HTMLInputElement: Input, HTMLFormElement: Form, URL,
    location: { protocol, origin: `${protocol}//capture.example.invalid`, href: `${protocol}//capture.example.invalid/login` },
    crypto: { randomUUID: () => `synthetic-form-${++tokenId}` },
    getComputedStyle: element => ({ ...element.style, visibility: element.form?.style.visibility === 'hidden' ? 'hidden' : element.style.visibility }),
    MutationObserver: class { constructor(callback) { observers.push(callback); } observe() {} },
    setTimeout(callback, delay) { const id = ++taskId; tasks.set(id, { at: now + delay, callback }); return id; },
    clearTimeout(id) { tasks.delete(id); },
    __xenon_login_capture(payload) { calls.push('capture'); messages.push(JSON.parse(payload)); },
    __xenon_mark_sensitive(marker) { assert.equal(marker, 'protected'); calls.push('protected'); },
  });
  context.window = context; context.top = subframe ? {} : context;
  const form = new Form();
  const username = form.add(new Input({ autocomplete: 'username', value: canaryUser }));
  const password = form.add(new Input({ type: 'password', autocomplete: 'current-password', value: canaryPassword }));
  function install() { vm.runInContext(source, context, { timeout: 1000 }); }
  function dispatch(type, target, { trusted = true, submitter = null } = {}) {
    for (const listener of listeners.get(type) || []) listener({ isTrusted: trusted, target, submitter, composedPath: () => [target] });
  }
  function mutate(fn) { fn(); for (const observer of observers) observer([]); }
  function advance(ms) {
    const until = now + ms;
    for (;;) {
      const next = [...tasks.entries()].filter(([, task]) => task.at <= until).sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0];
      if (!next) break;
      const [id, task] = next; tasks.delete(id); now = task.at; task.callback();
    }
    now = until;
  }
  function error({ invalid = false, text = 'Sign-in failed' } = {}) {
    const node = new Element(); node.textContent = text; node.setAttribute(invalid ? 'aria-invalid' : 'role', invalid ? 'true' : 'alert'); nodes.push(node); return node;
  }
  return { context, form, username, password, Input, Form, Element, messages, calls, listeners, install, dispatch, mutate, advance, error };
}

test('capture installs only once in a top-level HTTPS document', () => {
  for (const options of [{ protocol: 'http:' }, { subframe: true }]) {
    const f = fixture(options); f.install(); f.dispatch('input', f.password); f.dispatch('submit', f.form);
    assert.equal(f.listeners.size, 0); assert.deepEqual(f.messages, []);
  }
  const f = fixture(); f.install(); f.install(); f.dispatch('input', f.password);
  assert.equal(f.messages.length, 1);
});

test('only trusted password editing emits a value-free form identity', () => {
  const f = fixture(); f.install();
  f.dispatch('input', f.password, { trusted: false }); f.dispatch('input', f.username);
  assert.deepEqual(f.messages, []);
  f.dispatch('input', f.password);
  assert.deepEqual(f.messages, [{ kind: 'edit', form: 'synthetic-form-1', origin: 'https://capture.example.invalid' }]);
  assert.ok(!JSON.stringify(f.messages).includes(canaryUser)); assert.ok(!JSON.stringify(f.messages).includes(canaryPassword));
});

test('trusted conventional POST submission marks privacy before native-only credential message', () => {
  const f = fixture(); f.install(); f.dispatch('input', f.password); f.dispatch('submit', f.form);
  assert.deepEqual(f.calls, ['capture', 'protected', 'capture']);
  assert.deepEqual(f.messages[1], { kind: 'submit', form: f.messages[0].form, origin: 'https://capture.example.invalid', username: canaryUser, password: canaryPassword });
  f.advance(5000); assert.equal(f.messages.length, 2, 'A still-visible password form is not a successful login');
});

test('public CWL form shape captures its username and password without autocomplete attributes', () => {
  // Anonymous GET inspection found same-origin POST, j_username text,
  // j_password password and hidden csrf_token. No real field values are used.
  const f = fixture(); f.form.action = '/idp/profile/SAML2/Redirect/SSO?execution=synthetic';
  f.username.autocomplete = ''; f.password.autocomplete = '';
  f.username.setAttribute('name', 'j_username'); f.password.setAttribute('name', 'j_password');
  f.form.add(new f.Input({ type: 'hidden', value: 'PUBLIC_CSRF_CANARY' }));
  f.install(); f.dispatch('input', f.password); f.dispatch('submit', f.form);
  assert.equal(f.messages[1].kind, 'submit'); assert.equal(f.messages[1].origin, 'https://capture.example.invalid');
  assert.equal(f.messages[1].username, canaryUser); assert.equal(f.messages[1].password, canaryPassword);
  assert.ok(!JSON.stringify(f.messages).includes('PUBLIC_CSRF_CANARY'));
});

test('synthetic website input and submit never emit credentials', () => {
  const f = fixture(); f.install(); f.dispatch('input', f.password, { trusted: false }); f.dispatch('submit', f.form, { trusted: false });
  f.mutate(() => { f.form.isConnected = false; }); f.advance(5000);
  assert.deepEqual(f.messages, []); assert.deepEqual(f.calls, []);
});

const invalidForms = {
  'GET method': f => { f.form.method = 'get'; },
  'cross-origin action': f => { f.form.action = 'https://other.example.invalid/login'; },
  'HTTP action': f => { f.form.action = 'http://capture.example.invalid/login'; },
  'URL credentials': f => { f.form.action = 'https://user:password@capture.example.invalid/login'; },
  'new-window target': f => { f.form.target = '_blank'; },
  'disconnected form': f => { f.form.isConnected = false; },
  'readonly password': f => { f.password.readOnly = true; },
  'disabled password': f => { f.password.disabled = true; },
  'readonly username': f => { f.username.readOnly = true; },
  'hidden password': f => { f.password.style.display = 'none'; },
  'new password': f => { f.password.autocomplete = 'section-login new-password'; },
  'one-time code': f => { f.form.add(new f.Input({ autocomplete: 'one-time-code' })); },
  'multiple passwords': f => { f.form.add(new f.Input({ type: 'password' })); },
  'missing username': f => { f.username.type = 'hidden'; },
  'ambiguous username': f => { f.form.add(new f.Input({ autocomplete: 'username' })); },
};
for (const [name, change] of Object.entries(invalidForms)) {
  test(`capture refuses ${name}`, () => {
    const f = fixture(); change(f); f.install(); f.dispatch('input', f.password); f.dispatch('submit', f.form);
    f.mutate(() => { f.form.isConnected = false; }); f.advance(5000);
    assert.deepEqual(f.messages, []); assert.deepEqual(f.calls, []);
  });
}

test('submission refuses all submitter action, method, and target overrides', () => {
  for (const attribute of ['formaction', 'formmethod', 'formtarget']) {
    const f = fixture(); f.install(); const submitter = new f.Element(); submitter.setAttribute(attribute, 'override');
    f.dispatch('submit', f.form, { submitter }); assert.deepEqual(f.messages, []); assert.deepEqual(f.calls, []);
  }
});

test('submission rejects empty or oversized passwords and oversized usernames', () => {
  for (const [field, value] of [['password', ''], ['password', 'x'.repeat(65537)], ['username', 'x'.repeat(65537)]]) {
    const f = fixture(); f[field].value = value; f.install(); f.dispatch('submit', f.form);
    assert.deepEqual(f.messages, []); assert.deepEqual(f.calls, []);
  }
});

test('a revealed password retains its sensitive identity and original form token', () => {
  const f = fixture(); f.install(); f.dispatch('input', f.password);
  f.mutate(() => { f.password.type = 'text'; f.password.autocomplete = ''; }); f.dispatch('submit', f.form);
  assert.equal(f.messages[1].password, canaryPassword); assert.equal(f.messages[0].form, f.messages[1].form);
});

test('separate forms receive separate opaque identities', () => {
  const f = fixture(); const other = new f.Form(); other.add(new f.Input({ autocomplete: 'username', value: canaryUser })); const pass = other.add(new f.Input({ type: 'password', value: canaryPassword }));
  f.install(); f.dispatch('input', f.password); f.dispatch('input', pass);
  assert.notEqual(f.messages[0].form, f.messages[1].form);
});

test('submission is an attempt and never emits a site-inferred success claim', () => {
  const f = fixture(); f.install(); f.dispatch('input', f.password); f.dispatch('submit', f.form);
  assert.equal(f.messages.length, 2, 'Native candidate is available at submission');
  f.mutate(() => { f.form.isConnected = false; }); f.advance(5000);
  assert.deepEqual(f.messages.map(message => message.kind), ['edit', 'submit']);
});

test('a wrong-password attempt remains a human save decision rather than a success claim', () => {
  for (const options of [{}, { invalid: true, text: '' }]) {
    const f = fixture(); f.install(); f.dispatch('submit', f.form); f.error(options);
    f.mutate(() => { f.form.isConnected = false; }); f.advance(5000);
    assert.equal(f.messages.length, 1); assert.equal(f.messages[0].kind, 'submit');
    assert.equal(f.messages[0].password, canaryPassword);
  }
});

test('page alerts after submission cannot claim success or cancel the native human decision', () => {
  const f = fixture(); f.install(); f.dispatch('submit', f.form); f.mutate(() => { f.form.isConnected = false; }); f.advance(900);
  f.mutate(() => { f.error(); }); f.advance(2000); assert.equal(f.messages.length, 1);
});

test('disabling or making a submitted password readonly emits no further credential message', () => {
  for (const field of ['disabled', 'readOnly']) {
    const f = fixture(); f.install(); f.dispatch('submit', f.form); f.mutate(() => { f.password[field] = true; }); f.advance(5000);
    assert.equal(f.messages.length, 1, `${field} must not emit completion or another password`);
  }
});

test('replacing the submitted form does not create another submission', () => {
  const f = fixture(); f.install(); f.dispatch('submit', f.form);
  f.mutate(() => { f.form.isConnected = false; const replacement = new f.Form(); replacement.add(new f.Input({ autocomplete: 'username' })); replacement.add(new f.Input({ type: 'password' })); });
  f.advance(5000); assert.equal(f.messages.length, 1);
});

test('ordinary MFA input never emits a password-edit cancellation or captures its value', () => {
  const f = fixture(); f.install(); f.dispatch('input', f.password); f.dispatch('submit', f.form);
  const otpForm = new f.Form(); const otp = otpForm.add(new f.Input({ autocomplete: 'one-time-code', value: 'PUBLIC_OTP_CANARY' }));
  f.mutate(() => { f.form.isConnected = false; }); f.dispatch('input', otp); f.dispatch('submit', otpForm); f.advance(5000);
  assert.deepEqual(f.messages.map(message => message.kind), ['edit', 'submit']);
  assert.ok(!JSON.stringify(f.messages).includes('PUBLIC_OTP_CANARY'));
});

test('SSO redirects never change the origin already emitted with the credential attempt', () => {
  const f = fixture(); f.install(); f.dispatch('input', f.password); f.dispatch('submit', f.form);
  for (const origin of ['https://mfa.example.invalid', 'https://portal.example.invalid']) {
    f.mutate(() => { f.form.isConnected = false; f.context.location.origin = origin; f.context.location.href = `${origin}/continue`; }); f.advance(5000);
  }
  assert.equal(f.messages.length, 2); assert.equal(f.messages[1].origin, 'https://capture.example.invalid');
});

test('a new trusted password edit emits a distinct cancellation opportunity before replacement submission', () => {
  const f = fixture(); f.install(); f.dispatch('input', f.password); f.dispatch('submit', f.form);
  f.password.value = 'NEW_SYNTHETIC_PASSWORD'; f.dispatch('input', f.password); f.dispatch('submit', f.form);
  assert.deepEqual(f.messages.map(message => message.kind), ['edit', 'submit', 'edit', 'submit']);
  assert.ok(!('password' in f.messages[2])); assert.equal(f.messages[3].password, 'NEW_SYNTHETIC_PASSWORD');
});
