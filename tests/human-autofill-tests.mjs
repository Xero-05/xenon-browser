// Executes the production native-only isolated-world scripts in a synthetic
// DOM. These tests do not claim native UI, physical-focus or browser acceptance.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const header = await readFile(new URL('../native/include/xenon/human_autofill.hpp', import.meta.url), 'utf8');
const validation = header.match(/human_autofill_validation_script\s*=\s*R"HUMANJS\(([\s\S]*?)\)HUMANJS"/u)?.[1];
assert(validation, 'Actual production validator must be present');
function production(name) {
  const start = header.indexOf(`inline const std::string ${name} =`);
  assert(start >= 0, `Actual production constant ${name} must be present`);
  const declaration = header.slice(start).split(/\ninline |\n\} \/\/ namespace/u)[0];
  const parts = [...declaration.matchAll(/R"HUMANJS\(([\s\S]*?)\)HUMANJS"|\bhuman_autofill_validation_script\b/gu)];
  assert.equal(parts.length, 3, 'Production script must concatenate its two wrappers and shared validator');
  return parts.map(part => part[1] ?? validation).join('');
}
const inspectSource = production('human_autofill_inspect_script');
const fillSource = production('human_autofill_fill_script');
const ORIGIN = 'https://login.example.invalid';
const USER = 'SYNTHETIC_AUTOFILL_ACCOUNT@example.invalid';
const PASSWORD = 'SYNTHETIC_AUTOFILL_PASSWORD_0a73';
const plain = value => JSON.parse(JSON.stringify(value));

function fixture({ phase = 'credentials', autocomplete = true, subframe = false, unowned = false } = {}) {
  const inputs = [], forms = [], writes = [], events = [];
  let overlay = null, submitCount = 0, eventHook;
  const document = {
    activeElement: null,
    querySelectorAll(selector) { assert.equal(selector, 'input'); return inputs.filter(input => input.isConnected); },
    elementFromPoint(x, y) {
      if (overlay) return overlay;
      return inputs.find(input => input.isConnected && input.type !== 'hidden' && !input.hidden &&
        x >= input.rect.left && x <= input.rect.right && y >= input.rect.top && y <= input.rect.bottom) ?? null;
    },
  };
  class Element {
    constructor() {
      this.isConnected = true; this.ownerDocument = document; this.parentElement = null; this.hidden = false; this.inert = false;
      this.style = { display: 'block', visibility: 'visible', opacity: '1', contentVisibility: 'visible' };
      this.rect = { left: 20, right: 220, top: 20, bottom: 50, width: 200, height: 30 };
    }
    getClientRects() { return this.isConnected && !this.hidden && this.style.display !== 'none' && this.type !== 'hidden' ? [this.rect] : []; }
    getBoundingClientRect() { return this.rect; }
    contains(other) { return other === this; }
    matches(selector) { assert.equal(selector, ':disabled'); return this.disabled || this.parentElement?.disabled === true; }
  }
  class Input extends Element {
    constructor({ type = 'text', autocomplete = '', value = '' } = {}) {
      super(); this.type = type; this.autocomplete = autocomplete; this.disabled = false; this.readOnly = false; this.form = null; this._value = value;
      const top = 20 + 45 * inputs.length; this.rect = { left: 20, right: 220, top, bottom: top + 30, width: 200, height: 30 }; inputs.push(this);
    }
    get value() { return this._value; }
    set value(value) { writes.push({ field: this, value }); this._value = value; }
    dispatchEvent(event) { events.push({ field: this, type: event.type }); eventHook?.(this, event); return true; }
  }
  class Form extends Element {
    constructor() { super(); this.method = 'post'; this.action = ORIGIN + '/login'; this.target = ''; forms.push(this); }
    add(input) { input.form = this; input.parentElement = this; return input; }
    submit() { ++submitCount; }
    requestSubmit() { ++submitCount; }
  }
  const form = new Form();
  const add = input => unowned ? input : form.add(input);
  const user = phase === 'password' ? null : add(new Input({ autocomplete: autocomplete ? 'username' : '' }));
  const password = phase === 'username' ? null : add(new Input({ type: 'password', autocomplete: autocomplete ? 'current-password' : '' }));
  document.activeElement = user ?? password;
  const context = vm.createContext({ document, HTMLInputElement: Input, URL, innerWidth: 800, innerHeight: 600,
    location: { origin: ORIGIN, protocol: 'https:', href: ORIGIN + '/login' }, getComputedStyle: node => node.style,
    Event: class { constructor(type, options) { this.type = type; this.bubbles = options.bubbles; } },
  });
  context.window = context; context.top = subframe ? {} : context;
  const inspect = vm.runInContext(`(${inspectSource})`, context, { timeout: 1000 });
  const fill = vm.runInContext(`(${fillSource})`, context, { timeout: 1000 });
  return { document, context, form, user, password, Input, Form, Element, inputs, writes, events,
    offer: (focused = false, origin = ORIGIN) => inspect(origin, focused),
    fill: (offer, username = USER, password = PASSWORD, origin = ORIGIN) => fill.call(offer, username, password, origin),
    overlay(value) { overlay = value; }, onEvent(value) { eventHook = value; }, submits: () => submitCount,
  };
}
function deniedBeforeWrite(f, offer, expected) {
  f.writes.length = 0; f.events.length = 0;
  const result = f.fill(offer);
  assert.equal(result.filled, false); if (expected) assert.equal(result.error, expected);
  assert.deepEqual(f.writes, []); assert.deepEqual(f.events, []); assert.equal(f.submits(), 0);
  assert(!JSON.stringify(result).includes(USER)); assert(!JSON.stringify(result).includes(PASSWORD));
  return result;
}

test('conventional and public CWL-shaped POST forms fill once without submission', () => {
  for (const autocomplete of [true, false]) {
    const f = fixture({ autocomplete });
    f.form.add(new f.Input({ type: 'hidden', value: 'PUBLIC_CSRF_FIXTURE' }));
    const offer = f.offer(true); assert.equal(offer.eligible, true); assert.equal(offer.phase, 'credentials');
    assert.equal(offer.document, f.document); assert.equal(offer.user, f.user); assert.equal(offer.password, f.password);
    assert.deepEqual(plain(f.fill(offer)), { filled: true, phase: 'credentials', submitted: false });
    assert.equal(f.user.value, USER); assert.equal(f.password.value, PASSWORD); assert.equal(f.submits(), 0);
    assert.deepEqual(f.events.map(event => event.type), ['input', 'change', 'input', 'change']);
    deniedBeforeWrite(f, offer, 'stale_offer');
  }
});

for (const phase of ['username', 'password']) test(`explicit ${phase} phase fills only that field`, () => {
  const f = fixture({ phase }); const offer = f.offer(true);
  assert.equal(offer.eligible, true); assert.equal(offer.phase, phase);
  const result = f.fill(offer); assert.equal(result.filled, true); assert.equal(result.phase, phase); assert.equal(result.submitted, false);
  assert.equal(f.writes.length, 1); assert.equal(f.writes[0].value, phase === 'username' ? USER : PASSWORD); assert.equal(f.submits(), 0);
});

test('username-first and password-only need explicit autocomplete semantics', () => {
  for (const phase of ['username', 'password']) assert.equal(fixture({ phase, autocomplete: false }).offer().eligible, false);
});

test('focus-only requires the actual nonnull eligible input', () => {
  for (const phase of ['credentials', 'username', 'password']) {
    const f = fixture({ phase }); assert.equal(f.offer(true).eligible, true);
    for (const other of [null, f.form, new f.Element()]) { f.document.activeElement = other; assert.equal(f.offer(true).eligible, false); }
    assert.equal(f.offer(false).eligible, true);
  }
});

test('only top-level exact HTTPS origin can offer or fill', () => {
  assert.equal(fixture({ subframe: true }).offer().eligible, false);
  for (const change of [f => { f.context.location.protocol = 'http:'; }, f => { f.context.location.origin = 'https://other.example.invalid'; }]) {
    const f = fixture(); const offer = f.offer(); change(f); assert.equal(f.offer().eligible, false); deniedBeforeWrite(f, offer, 'origin');
  }
  const f = fixture(); assert.equal(f.offer(false, ORIGIN + '/').eligible, false); assert.equal(f.fill(f.offer(), USER, PASSWORD, 'https://other.example.invalid').filled, false);
});

test('GET, cross-origin, credential-bearing action URLs and external targets are rejected', () => {
  for (const change of [f => { f.form.method = 'get'; }, f => { f.form.action = 'https://other.example.invalid/login'; },
    f => { f.form.action = 'http://login.example.invalid/login'; }, f => { f.form.action = 'https://user:pass@login.example.invalid/login'; },
    f => { f.form.target = '_blank'; }]) {
    const f = fixture(); const offer = f.offer(); change(f); assert.equal(f.offer().eligible, false); deniedBeforeWrite(f, offer, 'form');
  }
});

test('readonly, disabled and disabled-fieldset credential controls never offer or fill', () => {
  for (const field of ['user', 'password']) for (const property of ['readOnly', 'disabled']) {
    const f = fixture(); const offer = f.offer(); f[field][property] = true;
    assert.equal(f.offer().eligible, false); deniedBeforeWrite(f, offer, 'not_actionable');
  }
  const f = fixture(); f.form.disabled = true; assert.equal(f.offer().eligible, false);
});

test('OTP and new-password inputs are never treated as current login credentials', () => {
  for (const field of ['user', 'password']) for (const token of ['one-time-code', 'new-password']) {
    const f = fixture(); const offer = f.offer(); f[field].autocomplete = `section-login ${token}`;
    assert.equal(f.offer().eligible, false); deniedBeforeWrite(f, offer);
  }
  const f = fixture(); f.form.add(new f.Input({ autocomplete: 'one-time-code' })); assert.equal(f.offer().eligible, false);
});

test('multiple visible usernames or passwords fail closed', () => {
  for (const type of ['text', 'password']) {
    const f = fixture(); const offer = f.offer(); f.form.add(new f.Input({ type, autocomplete: type === 'text' ? 'username' : '' }));
    assert.equal(f.offer().eligible, false); deniedBeforeWrite(f, offer, 'ambiguous');
  }
});

test('a unique explicit username takes precedence over unrelated text fields', () => {
  for (const phase of ['credentials', 'username']) for (const unowned of [false, true]) {
    const f = fixture({ phase, unowned });
    const auxiliary = new f.Input({ value: 'HUMAN_OTHER_TEXT' }); if (!unowned) f.form.add(auxiliary);
    const offer = f.offer(true); assert.equal(offer.eligible, true); assert.equal(offer.user, f.user);
    assert.equal(f.fill(offer).filled, true); assert.equal(auxiliary.value, 'HUMAN_OTHER_TEXT');
    assert.equal(f.writes.filter(write => write.field === auxiliary).length, 0);
  }
  const ambiguous = fixture({ autocomplete: false }); ambiguous.form.add(new ambiguous.Input());
  assert.equal(ambiguous.offer().eligible, false);
});

test('Google-shaped unowned username with webauthn and hidden password decoy fills only the username', () => {
  const f = fixture({ phase: 'username', unowned: true }); f.user.autocomplete = 'username webauthn';
  const decoy = new f.Input({ type: 'password' }); decoy.hidden = true;
  const offer = f.offer(true); assert.equal(offer.eligible, true); assert.equal(offer.form, null); assert.equal(offer.phase, 'username');
  assert.deepEqual(plain(f.fill(offer)), { filled: true, phase: 'username', submitted: false });
  assert.equal(f.user.value, USER); assert.equal(decoy.value, ''); assert.equal(f.writes.length, 1); assert.equal(f.submits(), 0);
});

test('explicit unowned current-password and two-field phases fill without submission', () => {
  for (const phase of ['password', 'credentials']) {
    const f = fixture({ phase, unowned: true }); const offer = f.offer(true);
    assert.equal(offer.eligible, true); assert.equal(offer.form, null); assert.equal(offer.phase, phase);
    assert.deepEqual(plain(f.fill(offer)), { filled: true, phase, submitted: false });
    assert.equal(f.password.value, PASSWORD); assert.equal(f.writes.length, phase === 'password' ? 1 : 2); assert.equal(f.submits(), 0);
    deniedBeforeWrite(f, offer, 'stale_offer');
  }
});

test('unowned controls require explicit login semantics and refuse OTP, signup and ambiguity', () => {
  for (const phase of ['username', 'password', 'credentials']) assert.equal(fixture({ phase, unowned: true, autocomplete: false }).offer().eligible, false);
  for (const field of ['user', 'password']) {
    const f = fixture({ unowned: true }); f[field].autocomplete = ''; assert.equal(f.offer().eligible, false);
  }
  for (const autocomplete of ['one-time-code', 'new-password', 'username', 'current-password']) {
    const f = fixture({ unowned: true }); const offer = f.offer();
    new f.Input({ type: autocomplete === 'current-password' ? 'password' : 'text', autocomplete });
    assert.equal(f.offer().eligible, false); deniedBeforeWrite(f, offer);
  }
});

test('phone usernames and revealed current-password fields retain explicit semantics', () => {
  for (const unowned of [false, true]) {
    const f = fixture({ unowned }); f.user.type = 'tel'; f.password.type = 'text';
    const offer = f.offer(true); assert.equal(offer.eligible, true); assert.equal(offer.user, f.user); assert.equal(offer.password, f.password);
    assert.equal(f.fill(offer).filled, true); assert.equal(f.user.value, USER); assert.equal(f.password.value, PASSWORD);
  }
});

test('unowned field replacement or changed form association invalidates an existing offer', () => {
  for (const change of [f => { f.form.add(f.user); f.form.add(f.password); }, f => {
    f.user.isConnected = false; new f.Input({ autocomplete: 'username' });
  }, f => { f.password.ownerDocument = {}; }]) {
    const f = fixture({ unowned: true }); const offer = f.offer(); change(f); deniedBeforeWrite(f, offer);
  }
  const owned = fixture(); const offer = owned.offer(); owned.user.form = null; owned.password.form = null;
  deniedBeforeWrite(owned, offer, 'changed_form');
});

test('page handlers attaching unowned filled fields to a form prevent a success claim', () => {
  const f = fixture({ unowned: true }); const offer = f.offer();
  f.onEvent((field, event) => { if (field === f.password && event.type === 'change') f.form.add(f.password); });
  assert.deepEqual(plain(f.fill(offer)), { filled: false, error: 'changed_after_fill', submitted: false });
  assert.equal(f.submits(), 0);
});

test('hidden decoys are not selected and visible replacement ambiguity is rejected', () => {
  const f = fixture(); const hidden = f.form.add(new f.Input({ type: 'password' })); hidden.hidden = true;
  const offer = f.offer(); assert.equal(offer.password, f.password); assert.equal(offer.eligible, true);
  hidden.hidden = false; deniedBeforeWrite(f, offer, 'ambiguous'); assert.equal(hidden.value, '');
});

test('covered, transparent, hidden, inert and offscreen fields are not actionable', () => {
  for (const change of [f => f.overlay(new f.Element()), f => { f.form.style.opacity = '0'; },
    f => { f.form.style.visibility = 'hidden'; }, f => { f.form.inert = true; },
    f => { f.password.rect = { left: 900, right: 1100, top: 10, bottom: 40, width: 200, height: 30 }; }]) {
    const f = fixture(); const offer = f.offer(); change(f); deniedBeforeWrite(f, offer);
  }
});

test('a nonempty password suppresses offers and cannot be overwritten after offering', () => {
  const f = fixture(); const offer = f.offer(); f.password.value = 'HUMAN_TYPED_SECRET';
  assert.equal(f.offer().eligible, false); deniedBeforeWrite(f, offer, 'password_not_empty'); assert.equal(f.password.value, 'HUMAN_TYPED_SECRET');
});

test('changed human username is preserved and consumes the offer', () => {
  const f = fixture(); const offer = f.offer(); f.user.value = 'HUMAN_TYPED_ACCOUNT';
  deniedBeforeWrite(f, offer, 'changed_values'); f.user.value = ''; deniedBeforeWrite(f, offer, 'stale_offer');
});

test('existing matching username is allowed but a different typed account is preserved', () => {
  const f = fixture(); f.user.value = USER; assert.equal(f.fill(f.offer()).filled, true);
  const other = fixture(); other.user.value = 'HUMAN_OTHER_ACCOUNT'; const offer = other.offer();
  deniedBeforeWrite(other, offer, 'username_conflict'); assert.equal(other.user.value, 'HUMAN_OTHER_ACCOUNT');
});

test('disconnected and replaced bound nodes cannot receive saved credentials', () => {
  for (const field of ['user', 'password']) {
    const f = fixture(); const offer = f.offer(); const old = f[field]; old.isConnected = false;
    f.form.add(new f.Input({ type: field === 'password' ? 'password' : 'text', autocomplete: field === 'password' ? 'current-password' : 'username' }));
    deniedBeforeWrite(f, offer, 'changed_form'); assert.equal(old.value, '');
  }
});

test('reparented form and document identities cannot be rebound', () => {
  for (const change of [f => { const form = new f.Form(); form.add(f.user); form.add(f.password); },
    f => { f.user.ownerDocument = {}; }, f => { f.form.ownerDocument = {}; }]) {
    const f = fixture(); const offer = f.offer(); change(f); deniedBeforeWrite(f, offer);
  }
});

test('same-origin action path and field semantics changes invalidate the offer', () => {
  for (const change of [f => { f.form.action = ORIGIN + '/different'; }, f => { f.user.type = 'email'; },
    f => { f.password.autocomplete = 'section-new current-password'; }]) {
    const f = fixture(); const offer = f.offer(); change(f); deniedBeforeWrite(f, offer, 'changed_form');
  }
});

test('both original values are set before page input handlers can replace destinations', () => {
  const f = fixture(); const offer = f.offer(); let replacement;
  f.onEvent((field, event) => { if (field === f.user && event.type === 'input') {
    assert.equal(f.password.value, PASSWORD, 'Password setter precedes page input handlers');
    f.password.isConnected = false; replacement = f.form.add(new f.Input({ type: 'password' }));
  } });
  assert.deepEqual(plain(f.fill(offer)), { filled: false, error: 'changed_after_fill', submitted: false });
  assert.equal(replacement.value, ''); assert.equal(f.submits(), 0);
});

test('page input and change handlers resetting either bound value prevent a success claim', () => {
  for (const field of ['user', 'password']) for (const type of ['input', 'change']) {
    const f = fixture(); const offer = f.offer();
    f.onEvent((target, event) => { if (target === f.password && event.type === type) f[field].value = ''; });
    const result = f.fill(offer);
    assert.deepEqual(plain(result), { filled: false, error: 'changed_after_fill', submitted: false });
    assert.equal(f[field].value, ''); assert.equal(f.submits(), 0);
    assert(!JSON.stringify(result).includes(USER)); assert(!JSON.stringify(result).includes(PASSWORD));
  }
});

test('page handlers detaching or reparenting the bound form or fields prevent a success claim', () => {
  for (const change of [f => { f.form.isConnected = false; }, f => { f.form.ownerDocument = {}; },
    f => { f.password.ownerDocument = {}; }, f => { new f.Form().add(f.password); }]) {
    const f = fixture(); const offer = f.offer();
    f.onEvent((target, event) => { if (target === f.password && event.type === 'change') change(f); });
    assert.deepEqual(plain(f.fill(offer)), { filled: false, error: 'changed_after_fill', submitted: false });
    assert.equal(f.submits(), 0);
  }
});

test('native prototype setter is used and fixed output never includes credentials', () => {
  const f = fixture(); const offer = f.offer();
  Object.defineProperty(f.user, 'value', { configurable: true, get() { return this._value; }, set() { throw Error('page override'); } });
  const result = f.fill(offer); assert.equal(result.filled, true);
  assert.deepEqual(plain(result), { filled: true, phase: 'credentials', submitted: false });
  assert(!JSON.stringify(result).includes(USER)); assert(!JSON.stringify(result).includes(PASSWORD));
});

test('safe native offer metadata does not serialize the retained username or DOM', () => {
  const f = fixture(); f.user.value = USER; const offer = f.offer();
  const metadata = vm.runInContext('(function(){return {eligible:this.eligible===true,phase:this.phase}})', f.context).call(offer);
  assert.deepEqual(plain(metadata), { eligible: true, phase: 'credentials' }); assert(!JSON.stringify(metadata).includes(USER));
  assert.equal(offer.usernameValue, USER, 'Snapshot remains only in the retained renderer object');
});

test('empty password and malformed credential arguments fail before setters', () => {
  for (const [user, password] of [[USER, ''], [null, PASSWORD], [USER, {}], [USER, 'x'.repeat(65537)]]) {
    const f = fixture(); const result = f.fill(f.offer(), user, password); assert.equal(result.filled, false);
    assert.deepEqual(f.writes, []); assert.deepEqual(f.events, []); assert.equal(f.submits(), 0);
  }
});
