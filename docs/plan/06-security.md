# 06 — Security Requirements

Binding requirements, scoped to what the prototype actually is: a state-sync app on a trusted-ish LAN, public AGPL repo, no accounts, no payments, no PII beyond a display name. Don't gold-plate beyond this document; don't ship less than it.

## Threat model (what we defend against)

| Threat | Defense |
|---|---|
| Malicious/buggy WebSocket client sends crafted messages | strict server-side validation, size/rate limits, role enforcement, never-crash handling (`03-server.md`) |
| Malicious/compromised *server* (or MITM on ws://) sends crafted state to clients | client-side validation + clamping of every incoming message; no network data ever used as a file path or executed |
| Uninvited LAN peer joins the session | room code required at handshake; server binds `127.0.0.1` by default — exposing it is an explicit operator action |
| Resource exhaustion (connection floods, message floods, giant frames) | max clients (8), token-bucket rate limit, 4096-byte `maxPayload`, hello timeout, heartbeat reaping |
| Supply-chain drift | pinned versions everywhere (JUCE tag, IXWebSocket tag, Catch2 tag, `package-lock.json`); `npm audit` at dependency-change time; runtime dep count kept at 1 (`ws`) |
| Secrets/credentials leaking into the public repo | none exist by design; never introduce tokens/keys into code or CI without stopping to design storage; `.gitignore` covers build dirs and local assets |

Out of scope for prototype (documented, not forgotten): user accounts/authn, authorization beyond the controller role, encrypted transport on LAN, DoS beyond per-connection limits, audio-file sandboxing beyond JUCE's decoders.

## Server rules

1. Validate **every** message against `02-protocol.md` before touching state: type whitelist, strict field sets (unknown field ⇒ reject), type checks, finite-number checks, range checks. Reject whole messages; never partially apply.
2. Enforce roles server-side. Client-side disabling of controls is UX, not security.
3. Bind `127.0.0.1` unless `HOST` is explicitly set. Startup log must state the bind address.
4. Room code: `crypto.randomUUID()` default; compare with constant-time comparison is unnecessary (not a password against offline attack), but never log it after startup.
5. All per-message work in try/catch; a throwing handler closes that connection (1011) and logs; the process never exits on client input.
6. No `eval`, no `Function(string)`, no dynamic `require`/`import` from input, no shelling out.
7. Production posture (document in server README at M6, do not build now): TLS terminates at a reverse proxy (`wss://` → nginx/caddy → localhost server); set `HOST=127.0.0.1` behind the proxy; optionally check `Origin` when browsers ever become clients.

## Client rules

1. Treat every byte from the socket as hostile even though we wrote the server: strict-parse (reject unknown fields/wrong types/non-finite), then clamp ranges, then apply. A malformed server message is logged and dropped — never crashes the app, never trips an assertion in release builds.
2. **Network data never becomes a path.** `trackId` is only ever a lookup key into the local repository's validated manifest. The repository rejects manifest entries whose `file` contains path separators or `..`, and verifies resolution stays inside the assets root.
3. Frame-size discipline: serialize, check ≤ 4096 bytes, else drop + log (should be impossible with our messages; the check catches bugs).
4. Room code and server URL are user-entered at runtime; never persisted to the repo. If you later add a saved-settings file, keep it out of git and say so in its doc.
5. Decoding audio uses JUCE's format readers only; a failed/corrupt decode surfaces as "track unavailable", never as a crash.

## Repository / process rules

1. Dependency changes: pin exact versions; run `npm audit` (server) and skim new C++ deps' release notes; record additions in `docs/plan/DEVIATIONS.md` if not already in this plan.
2. CI has no secrets. If a future workflow needs one, use GitHub Actions secrets, least scope, and note it in `docs/decisions.md`'s follow-up section.
3. License hygiene is a security-adjacent requirement here: new dependencies must be permissive or LGPL (IXWebSocket BSD-3, Catch2 BSL-1.0, ws MIT all fine); anything GPL/AGPL beyond JUCE needs an explicit decision recorded before merging.
4. Error messages sent to peers (`error.message`) contain no stack traces, paths, or versions.
