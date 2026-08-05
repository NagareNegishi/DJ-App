# M3 host checklist — single-deck audio engine

Run on the Windows host, from a Developer Command Prompt (Visual Studio Build
Tools 2022). Confirms real playback through `JuceAudioEngine`/`AudioDeviceHub`
on the platform that actually has a sound device — the container only proves
the DSP core is correct offline (`ctest`'s `BufferPlaybackSource` suite).

## Prerequisites

Same toolchain as `M1-host.md` (VS Build Tools 2022, JUCE 8.0.12 installed).
Needs at least one manifest entry pointing at a real, playable audio file
(`docs/setup.md` / the repository's manifest format) — the container's test
fixtures are synthetic and too short to judge audibly.

## Steps

1. From a Developer Command Prompt at the repo root: `git checkout claude`
   then `git pull`.
2. Configure and build: `cmake -S client -B client/build/windows -G Ninja`
   then `cmake --build client/build/windows`.
   - Expected: both `dj-app-client` and `dj-app-tests` build clean.
3. Run the test suite: `ctest --test-dir client/build/windows --output-on-failure`
   - Expected: all tests pass, same count as the container (86 as of this
     unit).
4. Run the client artefact: `client\build\windows\dj-app-client_artefacts\DJ App.exe`
   - Expected: the window opens with the M2 track list on one side and the
     M3 dev-UI controls (Load Selected, Play/Pause, Seek, Gain, Rate) on the
     other — no crash on startup.
5. Select a track in the list, click **Load Selected**.
   - Expected: no crash; the seek slider's range updates to the track's
     duration.
6. Click **Play/Pause**.
   - Expected: audio plays audibly through the default output device; the
     button's label reflects the playing state. Click again: audio pauses
     (silence, not stopped-and-reset).
7. While playing, drag the **Seek** slider to a new position and release.
   - Expected: playback audibly jumps to the new position on release (not
     continuously while dragging).
8. While playing, move the **Gain** slider away from its default (1.0).
   - Expected: volume audibly scales up/down in real time as the slider
     moves.
9. While playing, set **Rate** to 0.5, then to 2.0.
   - Expected: playback is audibly slower/faster at each setting, with a
     pitch shift (expected — `BufferPlaybackSource` resamples, it does not
     do pitch-preserving time-stretch; a pitch shift here is correct
     behavior, not a bug).
10. Let a short track play to its end without touching any control.
    - Expected: playback stops on its own at end-of-track (self-stop, no
      crash, no looping unless a loop was set), and the Play/Pause button's
      next click starts over correctly rather than getting stuck.
11. Close the window via its close button (X).
    - Expected: clean exit, no hang, no leftover process in Task Manager.

## Container cross-check (already covered, listed for completeness)

`docs/plan/07-milestones.md`'s M3 acceptance also requires "no crash on
device absence, test in container run too" — already verified during this
session's build-orchestration work via a headless container run with no
audio device present (`AudioDeviceHub` logs a "no audio device" note and the
app stays up). No host action needed for this item; listed here so the
checklist is a complete record of M3's acceptance criteria.

Report back: pass/fail per step, and note anything audibly wrong (crackling,
wrong pitch direction, seek landing at the wrong position) even if it isn't
a crash.
