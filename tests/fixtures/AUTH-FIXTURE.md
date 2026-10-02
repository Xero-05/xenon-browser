# Synthetic credential fixture

`auth-cert.pem` and `auth-key.pem` are public test materials, not production
credentials. The HTTPS server binds only `127.0.0.1:18766`. Never deploy this key
or add this certificate to a system/browser trust store.

The certificate DER SHA-256 is
`ebd78c975821dd1c14acb73a864cc5d7641fb5af626a4a8ad6cca54692495e06`.
A separate test browser build may pin this exact fingerprint for the fixture
origin via `XENON_TEST_FIXTURE_CERT_SHA256`. The release build must omit that
definition. No runtime setting or command-line flag enables test trust.

The native `vault_fixture_seed` executable is test-only and is not packaged. It
refuses an existing vault and accepts only `.cache/auth-integration-*` profiles
containing the `SYNTHETIC_TEST_PROFILE` marker. It seeds known canaries using the
same DPAPI/SQLite code as the browser and returns only the opaque account ID,
origin, and folder grant ID. It also grants a synthetic sibling upload folder
containing a harmless report and an arbitrarily named, nonfunctional pairing
config, which must be excluded from enumeration. The integration harness
supplies synthetic pairing and account grants before launching the isolated
test profile.

The fixture records authentication success and event metadata, never submitted
credential values. The username/password canaries are intentionally public in
the test source. The `hold` mode keeps a populated password form in the same
document for observation-quarantine and handoff tests.

The `/human-auth-sim` route populates only public canaries, dispatches synthetic
DOM input events for password and one-time-code fields, then reveals the value
and removes its sensitive attributes. It exercises the renderer input-monitor
path and sticky quarantine; it is not a test of manual Windows keyboard input.

The negative login case covers GET forms, readonly usernames, and readonly
passwords. Each must be rejected before any input/change/submit event, HTTP
submission, or credential-bearing URL. The submission path uses MCP; the
fill-only path uses a separately authenticated native worker because MCP does
not expose a submit override. Both must reach the native form guard.

The report hashes the tested DLL before launch and after shutdown. Diagnostic
canary verification traverses the entire synthetic profile and scans every
`*.log`, `LOG`, and `LOG.old` file for UTF-8 and UTF-16 canaries, with zero
directory exclusions; unreadable directories fail the run. The login fixture
deliberately emits the public password canary to the page console to verify
that production logging suppression prevents native diagnostic persistence.
