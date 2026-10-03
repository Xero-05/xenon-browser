# Credential detection comparison

Reviewed 2026-10-03 against Xenon's pinned Chromium **154.0.8037.94** and the public Firefox implementation. This is a source audit plus synthetic regression coverage, not a claim of browser parity or successful sign-in to Google.

## Why Chromium recognizes more logins

Xenon uses CEF's Chrome runtime, but explicitly disables `credentials_enable_service` and `credentials_enable_autosignin` in `cef_engine.cpp`. Its native vault, human picker, protected agent login and Save/Update monitor use fixed isolated-world scripts. These scripts do not consume Chromium's password-form classifications. Enabling Chrome's service alone would not route its store, prompts and fill decisions through Xenon's vault, exact-origin grants and authentication quarantine. The inspected pinned CEF public headers expose no password-form classification or custom password-store callback that provides that integration.

Chromium separates per-frame extraction/filling from browser-side form lifecycle, password storage and UI. Its renderer groups unowned controls into a synthetic form and extends ownership across shadow trees. The parser combines predictions, explicit autocomplete semantics and heuristics; it has distinct filling and saving modes and distinguishes current, new and confirmation passwords. A unique explicit username can win over other text fields. See the [password-manager architecture](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/password_manager), [pinned renderer architecture](https://chromium.googlesource.com/chromium/src/+/refs/tags/154.0.8037.94/components/autofill/content/renderer/README.md) and [pinned parser](https://chromium.googlesource.com/chromium/src/+/refs/tags/154.0.8037.94/components/password_manager/core/browser/form_parsing/form_data_parser.cc).

Firefox constructs form-like groups for controls outside a form and recognizes explicit username-only inputs. Its login manager also tracks user-modified formless password controls and can infer submission when fields disappear, subject to prior user interaction. Password identity survives reveal through `hasBeenTypePassword`. This is lifecycle tracking beyond listening for an HTML `submit` event. See [LoginManagerChild](https://searchfox.org/firefox-main/source/toolkit/components/passwordmgr/LoginManagerChild.sys.mjs), particularly username-only classification and formless field modification/removal handling.

## Observed Google shape

Anonymous HTTPS GET inspection of Google's public identifier page, using a Chrome 154 user agent in a fresh HTTP session, found a text input with `autocomplete="username webauthn"`, identifier ID/name and no HTML form in the returned markup. It also contained an inaccessible password decoy with `tabindex="-1"` and `aria-hidden="true"`. The generic HTTP user agent received a different WebLite variant with a POST form. This demonstrates why one fetched variant or the presence of a password input alone is insufficient evidence of compatibility. The Chrome-shaped response is directly rejected by Xenon's previous form-required inspector. Public source inspection did not execute Google's page JavaScript, enter an identifier, inspect its password step or authenticate. Google can vary this markup by browser, session and experiment.

The regression reproduces only the field structure with generated fixture credentials. It contains no copied page scripts, Google tokens, cookies or real account data. The fixture proves recognition/filling in CEF; it does not prove Google's current event handlers accept the fill or that Google sign-in completes.

## Current support and gaps

| Case | Human saved-account fill | Granted agent login | Automatic Save/Update capture |
| --- | --- | --- | --- |
| Conventional same-origin HTTPS POST, username + password | Supported, fill-only | Supported, bounded native submission | Supported with physical edit and trusted submission |
| Unique explicit username beside other text fields | Supported | Still uses the narrower candidate count | Supported |
| Explicit username-only / current-password-only phase in a POST form | Supported, human advances | Bounded username-first flow | No username-first capture state |
| Inputs outside any form | Supported only with explicit semantics | Refused | Refused |
| Phone username | Supported | Not recognized as a username candidate | Supported |
| Password revealed as text | Supported with explicit current-password semantics | Not recognized as a password candidate | Remembered password identity |
| JavaScript login without an HTML submit event | Can fill explicit unowned controls | No generic Next/Sign-in automation | No submission inference |
| Shadow-root or embedded credentials | No traversal in the fill detector | No general embedded login support | No general shadow/frame capture |
| OTP, new-password, ambiguous controls | Refused | Refused | Refused |

Human fill now treats visible unowned controls as one conservative group. Each selected field must carry explicit username/current-password semantics. A unique marked username is preferred over unrelated text, but duplicate marked usernames or passwords remain ambiguous. Conventional forms retain the same-origin POST requirement, including for fill-only use. Every offer still binds the exact document, original nodes, form association and values; adding a form association or replacing a field invalidates it. The human must select the account in native UI and continue on the website. No agent grant is added, no submission is performed, and credentials remain outside MCP.

## Further work

1. Share value-free field classification across fill, agent login and capture, keeping their effect and provenance policies separate. Add focused-form selection and synthetic fixtures for multiple independent forms before relaxing document-wide ambiguity.
2. Add a native, bounded username-first capture transaction with document/origin binding, lock cancellation and encrypted candidate state. Submission inference needs separate trusted-input and lifecycle evidence; field disappearance alone must not create a save offer or claim successful authentication.
3. Extend shadow/frame detection together with authentication quarantine and screenshot/evidence protection. Field traversal must not get ahead of the privacy boundary or let an embedded origin inherit a top-level credential grant.
4. Evaluate an upstream CEF integration surface for Chromium's password manager if broad parity is required. It needs explicit store/UI/fill interception compatible with Xenon's native vault and confirmation. Merely enabling a preference is not that integration, and changing the pinned runtime or sandbox bootstrap requires its own dependency and security validation.

Production scripts are tested by `tests/human-autofill-tests.mjs`; native CEF, picker timing, form-less phases, hidden decoys, unrelated text preservation and stale-node denial are exercised by `tests/human-autofill-integration.mjs` using the unshipped driver and disposable profiles. Physical account-picker clicks and real Google sign-in remain manual acceptance work. See [testing](TESTING.md).
