// Execute the exact production protected-login function against synthetic forms.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const header = await readFile(new URL('../native/include/xenon/protected_login.hpp', import.meta.url), 'utf8');
const source = header.match(/protected_login_script\s*=\s*R"LOGINJS\(([\s\S]*?)\)LOGINJS"/u)?.[1];
assert(source);
const ORIGIN = 'https://login.example.invalid', USER = 'SYNTHETIC_USER', PASSWORD = 'SYNTHETIC_PASSWORD';
function fixture({ phase = 'credentials', buttons = 1, buttonTag = 'BUTTON' } = {}) {
  const fields = [], writes = [], submissions = [], events = [];
  let eventHook, overlay, valid = true;
  const document = {
    querySelectorAll(selector) { assert.equal(selector, 'input'); return fields.filter(e => e.tagName === 'INPUT' && e.isConnected); },
    elementFromPoint(x, y) { return overlay ?? fields.find(e => e.isConnected && x >= e.rect.left && x <= e.rect.right && y >= e.rect.top && y <= e.rect.bottom); },
  };
  class Element {
    constructor(tagName, type) {
      this.tagName = tagName; this.type = type; this.isConnected = true; this.ownerDocument = document;
      this.parentElement = null; this.disabled = false; this.readOnly = false; this.autocomplete = '';
      this.style = { display: 'block', visibility: 'visible', opacity: '1' }; this.attributes = {};
      const top = 20 + fields.length * 40; this.rect = { left: 20, right: 220, top, bottom: top + 30, width: 200, height: 30 };
      this._value = ''; fields.push(this);
    }
    getClientRects() { return this.isConnected ? [this.rect] : []; }
    getBoundingClientRect() { return this.rect; }
    contains(node) { return node === this; }
    matches(selector) { assert.equal(selector, ':disabled'); return this.disabled || this.form?.disabled === true; }
    hasAttribute(name) { return Object.hasOwn(this.attributes, name); }
    dispatchEvent(event) { events.push(event.type); eventHook?.(this, event); return true; }
  }
  class Input extends Element {
    constructor(type = 'text') { super('INPUT', type); }
    get value() { return this._value; }
    set value(value) { this._value = value; writes.push({ field: this, value }); }
  }
  class Form {
    constructor() { this.isConnected = true; this.ownerDocument = document; this.elements = []; this.method = 'post'; this.action = ORIGIN + '/login'; this.target = ''; }
    add(e) { e.form = this; this.elements.push(e); return e; }
    checkValidity() { return valid; }
    requestSubmit(button) { submissions.push({ button, name: button?.name, value: button?.value }); }
  }
  const form = new Form();
  const user = phase === 'password' ? null : form.add(new Input());
  const password = phase === 'username' ? null : form.add(new Input('password'));
  for (let i = 0; i < buttons; ++i) {
    const button = form.add(buttonTag === 'INPUT' ? new Input('submit') : new Element('BUTTON', 'submit'));
    button.name = '_eventId_proceed'; button.value = 'Login';
  }
  const button = form.elements.find(e => e.type === 'submit');
  const context = vm.createContext({ document, HTMLInputElement: Input, HTMLFormElement: Form, URL, innerWidth: 800, innerHeight: 600,
    location: { origin: ORIGIN, protocol: 'https:', href: ORIGIN + '/login' }, getComputedStyle: e => e.style,
    Event: class { constructor(type) { this.type = type; } },
  });
  context.window = context; context.top = context;
  const login = vm.runInContext(`(${source})`, context);
  return { form, user, password, button, fields, context, writes, events, submissions,
    login: (submit = true, usernameDone = phase === 'password') => JSON.parse(JSON.stringify(login(USER, PASSWORD, ORIGIN, submit, usernameDone))),
    onEvent: hook => { eventHook = hook; }, cover: e => { overlay = e; }, invalid: () => { valid = false; },
  };
}

test('named button and input submitters are passed to requestSubmit with their form data', () => {
  for (const buttonTag of ['BUTTON', 'INPUT']) {
    const f = fixture({ buttonTag });
    assert.deepEqual(f.login(), { filled: true, submitted: true });
    assert.equal(f.submissions.length, 1); assert.equal(f.submissions[0].button, f.button);
    assert.equal(f.submissions[0].name, '_eventId_proceed'); assert.equal(f.submissions[0].value, 'Login');
  }
});
test('no-button forms and both username-first phases remain supported', () => {
  assert.deepEqual(fixture({ buttons: 0 }).login(), { filled: true, submitted: true });
  assert.deepEqual(fixture({ phase: 'username' }).login(), { usernameFilled: true, submitted: true });
  assert.deepEqual(fixture({ phase: 'password' }).login(), { filled: true, submitted: true });
  assert.deepEqual(fixture({ phase: 'username' }).login(true, true), { waiting: true });
});
test('fill-only use never selects or submits an ambiguous button', () => {
  const f = fixture({ buttons: 2 }); assert.deepEqual(f.login(false), { filled: true, submitted: false });
  assert.equal(f.submissions.length, 0);
});
test('multiple visible submitters fail before any credential is written', () => {
  const f = fixture({ buttons: 2 }); assert.equal(f.login().error, 'ambiguous_submitter'); assert.equal(f.writes.length, 0);
});
test('unsafe form and submitter overrides are rejected before filling', () => {
  for (const change of [f => { f.form.method = 'get'; }, f => { f.form.target = '_blank'; },
    f => { f.form.action = 'https://other.example.invalid/login'; },
    f => { f.button.attributes.formmethod = ''; f.button.formMethod = 'get'; },
    f => { f.button.attributes.formaction = ''; f.button.formAction = 'https://other.example.invalid/login'; },
    f => { f.button.attributes.formtarget = ''; f.button.formTarget = '_blank'; }]) {
    const f = fixture(); change(f); assert(f.login().error); assert.equal(f.writes.length, 0); assert.equal(f.submissions.length, 0);
  }
});
test('disabled-fieldset, read-only, covered and OTP fields cannot receive credentials', () => {
  for (const change of [f => { f.form.disabled = true; }, f => { f.password.readOnly = true; },
    f => { f.cover({}); }, f => { f.user.autocomplete = 'section-login one-time-code'; }]) {
    const f = fixture(); change(f); assert(f.login().error); assert.equal(f.writes.length, 0);
  }
});
test('handlers changing fields or submission destination stop submission', () => {
  for (const change of [f => { f.password.isConnected = false; }, f => { f.user._value = ''; },
    f => { f.form.action = ORIGIN + '/changed'; }, f => { f.button.attributes.formmethod = ''; f.button.formMethod = 'get'; },
    f => { f.button.form = null; }]) {
    const f = fixture(); f.onEvent(() => change(f)); assert.equal(f.login().error, 'changed_after_fill'); assert.equal(f.submissions.length, 0);
  }
});
test('browser validation preventing submission is reported as filled only', () => {
  const f = fixture(); f.invalid(); assert.deepEqual(f.login(), { filled: true, submitted: false }); assert.equal(f.submissions.length, 0);
});
test('native submission respects form and submitter validation opt-outs', () => {
  for (const change of [f => { f.form.noValidate = true; }, f => { f.button.formNoValidate = true; }]) {
    const f = fixture(); f.invalid(); change(f); assert.deepEqual(f.login(), { filled: true, submitted: true }); assert.equal(f.submissions.length, 1);
  }
});
test('fixed outcomes never serialize the credentials or page strings', () => {
  const f = fixture(); const result = JSON.stringify(f.login()); assert(!result.includes(USER)); assert(!result.includes(PASSWORD));
});
