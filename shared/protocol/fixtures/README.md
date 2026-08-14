# Protocol fixtures

This directory is the shared wire-contract corpus: JSON files describing messages that
should be accepted or refused on each side of the socket. Both the Node server suite and
the C++ client suite run against it, so a drift between the two implementations shows up
as a failing fixture instead of as a bug found later with two clients and an audio device.

Every fixture here is derived from `docs/plan/02-protocol.md` alone, not from either
implementation. That is the point: the corpus can disagree with the code, and when it does
the code is what gets looked at first.

## Who reads what

| directory | server suite (`node:test`) | client suite (Catch2/ctest) |
|---|---|---|
| `client-to-server/valid` | `validate.js` **accepts** | not run — the client never parses its own outgoing messages at M6 |
| `client-to-server/invalid` | `validate.js` **rejects**, with `closeCode` asserted where present | not run |
| `server-to-client/valid` | not run — the server never validates its own output | `model/Serialization` **parses** every payload |
| `server-to-client/invalid` | not run | per the fixture's `expect.client` |

`client-to-server/` is therefore a server-only corpus. Those fixtures carry no
`expect.client` and no `payloads`: the client has no `hello` parser and no cross-field
rules, so asking it to reject a bad hello would mean nothing.

## File shape

One file per case, named `<message-type>-<what-it-exercises>.json` in kebab-case. Valid
JSON, 2-space indentation, trailing newline, no comments. The explanation goes in
`description`.

- `description` — one line saying what the case exercises. Required everywhere.
- `message` — the literal JSON frame body. Required everywhere. It is not always an
  object: a few fixtures deliberately carry `null`, a number, a string, `true`, or an
  array, because those are legal frames a hostile client can send. Test the key's presence,
  not its truthiness.
- `reason` — short phrase naming the violation. Required in both `invalid/` directories.
- `expect` — required on every `invalid/` fixture, with no default. It names the outcome
  for each consumer that reads that directory: `{"server": "reject"}` under
  `client-to-server/invalid/`, `{"client": "reject"}` or `{"client": "accept"}` under
  `server-to-client/invalid/`. There is no default on purpose, so that a forgotten field
  fails loudly rather than silently asserting the wrong rule.
- `closeCode` — `client-to-server/invalid/` hello fixtures only: the WebSocket close code
  the server must respond with (`4000` bad handshake, `4001` version mismatch, `4004` wrong
  room). Other invalid messages draw an `error{bad-message}` rather than a close, so they
  omit it.
- `payloads` — required on every `server-to-client/` fixture, in both `valid/` and
  `invalid/`. See below.

Each fixture exercises exactly one violation. A file that breaks three rules at once tells
you nothing when it fails.

## Which room code the server runner supplies

`validateHello` takes the expected room code as an argument, because whether a submitted
code is correct is process configuration and not message shape. The hello fixtures do not
all share one code (`hello-room-max-length.json` needs a 64-character one), so a runner
cannot use a single fixed value for the whole directory. The rule is mechanical and
derivable from each file:

- A fixture declaring `closeCode: 4004` is the room-mismatch case. Supply a code that
  differs from its `message.room`.
- Every other hello fixture is testing shape, not room matching. Supply its own
  `message.room` as the expected code, so the only thing under test is what the fixture
  says it is testing.

## `reject` versus `accept`

The two sides do not agree on what to do with an out-of-range value, and that is
deliberate. `02-protocol.md` has the server reject out-of-spec values (the delta is dropped
whole, no partial application) while the client clamps incoming values into range as
defense-in-depth. So `{"gain": 5.0}` is invalid, the server must refuse it, and the client
parses it happily and clamps to 2.0 afterwards.

- `"reject"` — the violation is structural: an unknown field, a wrong JSON type, a
  non-finite number, a `trackId` that breaks its pattern or could be read as a path
  segment, a `loop` whose `inSeconds` is not strictly less than `outSeconds`, a missing
  required field. Both sides refuse it.
- `"accept"` — the violation is a number outside its range and nothing else. The server
  refuses it, the client parses it and relies on clamping, which is tested elsewhere.

A fixture that is both out of range and structurally broken would be `"reject"`, but no
fixture here mixes the two.

## `payloads`

At M6 the client parses exactly two things, a bare `PlaybackState` and a bare `StateDelta`.
It has no envelope parser; that arrives at M7 with the transport. So the fixture declares
where its payloads are and the C++ suite walks the list mechanically, with no switch on
message type.

Each entry has two keys:

- `pointer` — an RFC 6901 JSON Pointer into `message`, for example `/snapshot/decks/A` or
  `/decks/B`. `""` means the whole message. Only plain forms appear here; no `~0`/`~1`
  escapes are needed in any key this corpus uses, and none should be introduced.
- `as` — `"PlaybackState"` or `"StateDelta"`.
  - `"PlaybackState"`: the object at `pointer` goes straight into `fromVar<PlaybackState>`.
  - `"StateDelta"`: the object at `pointer` is a wire delta carrying `deck` and `changes`,
    and the test flattens it to `{ "deck": <deck>, ...<changes> }` before calling
    `fromVar<StateDelta>`, because the wire form nests changed fields under `changes` while
    the client's `StateDelta` JSON form is flat. If the value at `pointer` has no `deck`,
    or its `changes` is not an object, the test records a parse failure; it never spreads a
    non-object.

A message with nothing the M6 client can parse carries `"payloads": []`. That is the
correct value for `roleChanged`, `peerJoined`, `peerLeft`, and `error`; those fixtures are
checked for well-formed JSON and nothing more.

For an `invalid/` fixture, `expect.client` applies to every entry in `payloads`: with
`"reject"` each declared payload must fail to parse, with `"accept"` each must succeed.
Such fixtures carry a single payload so the assertion has one subject.

This mechanism exists only because the client has no envelope parser before M7. When M7's
transport lands, route the fixture suite through the real parser and let `payloads` shrink
to whatever the parser still cannot reach.

## Changing a fixture

A fixture change means the wire contract moved. Change `docs/plan/02-protocol.md` first,
bump the protocol version if the change is not backward compatible, then update the server,
the client, and this corpus together in the same milestone. A fixture run that fails on one
side is a drift report: fix the code, or move the contract deliberately and take all three
with it.
