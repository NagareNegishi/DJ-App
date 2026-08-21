# 05 — Testing Strategy

Tests are part of each milestone's definition of done, not a separate phase. Everything below runs in the dev container except the manual audio checklists.

## Layers of the strategy

| Layer | Tool | What it covers |
|---|---|---|
| Client unit tests | Catch2 v3 (FetchContent, pinned), run via `ctest` | model serialization/clamping, StateManager, `BufferPlaybackSource` offline rendering, repository manifest validation |
| Server unit tests | `node:test` | `validate.js`, `room.js` via fake clients (no sockets) |
| Server integration tests | `node:test` + real `ws` clients | full protocol flows over real sockets on an ephemeral port |
| Contract fixtures | shared JSON in `shared/protocol/fixtures/` | both sides agree on the wire format |
| Manual host checklists | human on Windows host | real audio output, GUI behavior, multi-machine sync |
| CI | GitHub Actions | all of the above except manual |

## Client unit tests (Catch2)

Target `dj-app-tests` (see `04-client.md` CMake notes). Mirror `src/` structure in `tests/`. Required suites as their code lands:

- **Serialization:** round-trip `PlaybackState`/`StateDelta` ↔ JSON; strict-parse rejection of unknown fields, wrong types, `NaN`/`Infinity`, out-of-range values; `loop:null` vs absent-loop distinction.
- **Ranges:** clamp behavior at and beyond every boundary in the `02-protocol.md` Field reference.
- **StateManager:** merge semantics (only present fields change), listener notification order and payload, source tagging (local vs remote), empty-delta drop, `playing:true` position injection.
- **BufferPlaybackSource (offline render):** rate 1.0 reproduces source samples; rate 2.0 consumes the buffer in half the blocks; gain scales output; seek positions the head; loop wraps at `outSeconds`; paused renders silence; **repeat on (M9):** end-of-track wraps the head to 0 and stays playing, output past the wrap resumes from the track's start sample; **repeat off (M9):** end-of-track stops and clears, matching the pre-M9 default. Use small synthetic buffers (e.g., ramp signals) so assertions are exact or within an interpolation epsilon.
- **Repository:** manifest parsing, invalid-entry skipping, path-traversal rejection (`../`, `/abs`, `a\b`), missing-file handling, cache identity (same `shared_ptr` on repeat load).

Run: `ctest --test-dir client/build/linux --output-on-failure`.

## Server tests (`node:test`)

- **Unit — `validate.js`:** table-driven over the Field reference: every field, valid/invalid/boundary, unknown-field rejection, non-object `changes`, empty `changes`.
- **Unit — `room.js` with fake clients:** hello→welcome snapshot contents, claim/release/steal-attempt, delta apply + `serverSeq` increment, non-controller delta ⇒ error+snapshot to sender only, controller disconnect clears control, deck B lazy creation.
- **Integration — real sockets** (start server on port 0): full handshake; wrong room/version close codes; broadcast fan-out reaches everyone except source; oversize frame closes 1009; binary frame closes 1008; malformed JSON ⇒ `bad-message`; three invalid ⇒ close 1008; rate limiter triggers `rate-limited` (temporarily lower the bucket via config injection to keep the test fast); heartbeat termination is exempt from testing (timer-based, cover by code review).

Run: `npm test` in `server/`.

## Contract fixtures (`shared/protocol/fixtures/`)

JSON files, one directory per direction, each split into `valid/` and `invalid/`. The file shape is specified and kept current in `shared/protocol/fixtures/README.md` — that is the authoritative description, because it has to stay in step with the corpus as it grows. Beyond `description`/`message`/`reason` it covers the fields a fixture needs in practice: `expect`, which names the outcome per consumer because the two sides legitimately disagree (the server rejects an out-of-range value, the client clamps it); `payloads`, the RFC 6901 pointers that let the client suite find nested payloads without knowing about envelopes; and `closeCode` on handshake fixtures. Populate at M6 with at least: hello (good, bad version, long name), every server→client type, deltas exercising each field's boundaries, `loop` null/value/invalid.
- Server tests feed every `valid/` fixture through `validate.js` expecting acceptance, every `invalid/` expecting rejection.
- Client tests parse every `valid/` server-message fixture through `model/Serialization` expecting success, and every `invalid/` expecting failure.
A change that breaks one side's fixture run means the wire contract drifted — fix the code, or update `02-protocol.md` + fixtures + both sides together.

## Manual host checklists

Kept as `docs/plan/checklists/M<k>-host.md`, created by the milestone that needs them (M1, M3, M5, M7, M8, M9 per `07-milestones.md`). Each is a numbered list of user actions with expected observations ("click play → audio starts within ~50 ms, playhead moves"). The agent prepares builds + instructions; the user executes on Windows and reports. A milestone requiring a checklist is not done until the user confirms it passed.

## CI (`.github/workflows/ci.yml`, created at M0)

Triggers: push and PR to `main`. Jobs:

1. **server** — `ubuntu-24.04`, `actions/setup-node` (22), `npm ci`, `npm test` in `server/`. (Skip gracefully until `server/package.json` exists: guard with a paths filter or a file-existence check.)
2. **client** — `ubuntu-24.04`: install the JUCE Linux apt dependencies (list in `docs/setup.md`); restore cache keyed `juce-8.0.12-ubuntu24`; on miss, clone JUCE at tag 8.0.12, build and install to `$HOME/juce-install`, save cache; configure with `-DCMAKE_PREFIX_PATH=$HOME/juce-install`, build, run `ctest --output-on-failure`.
3. **format** — run `clang-format --dry-run --Werror` over `client/src client/tests`. Make this job non-blocking (`continue-on-error: true`) until M5, then flip it required.

No coverage percentage gate. Policy instead: every bug found after M1 gets a regression test in the same commit as its fix; every protocol handler and every public model/state function has at least one test.
