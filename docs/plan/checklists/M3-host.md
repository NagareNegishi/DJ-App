# M3 host checklist — single-deck audio engine

Run on the Windows host, from the **x64 Native Tools Command Prompt** for VS
Build Tools (not the plain "Developer Command Prompt," which defaults to
32-bit detection — see `docs/plan/DEVIATIONS.md`'s 2026-08-05 JUCE-install-path
entry). Confirms real playback through `JuceAudioEngine`/`AudioDeviceHub` on
the platform that actually has a sound device — the container only proves the
DSP core is correct offline (`ctest`'s `BufferPlaybackSource` suite).

This repo is checked out at `C:\Users\nagi\Desktop\DJ-App` on this host.

## Prerequisites

### Toolchain (one-time, skip if already done — same as `M1-host.md`)

VS Build Tools, "Desktop development with C++" workload, JUCE 8.0.12
installed. Note: `docs/setup.md`'s text says "VS Build Tools 2022," but
`docs/plan/DEVIATIONS.md` (2026-08-05 entry) already recorded that this host
actually has **VS Build Tools 2026** (MSVC 19.51) and that's fine — proceed
with whatever this host has installed, don't try to install 2022 specifically.

**Open the correct terminal**: press the Windows key, type
`x64 Native Tools Command Prompt`, and open the entry that appears (it will
say "for VS 2026" or similar, matching whatever version Windows shows you —
do not use a plain `cmd.exe`, PowerShell window, or the generic "Developer
Command Prompt" entry, which defaults `cl.exe` to 32-bit target detection
instead of 64-bit).

### Test audio file (new for M3 — do this before Step 1)

The app's audio decoder (`juce::AudioFormatManager::registerBasicFormats()`,
built without the FLAC/OggVorbis/MP3 compile flags — checked against
`client/CMakeLists.txt`) reads **`.wav` or `.aiff` only**. Anything else
(`.mp3`, `.m4a`, `.flac`) will fail to decode.

1. Search this Windows machine for an existing `.wav` or `.aiff` file that's
   at least ~20-30 seconds long (long enough to actually hear a gain change,
   a rate change, or a seek land somewhere new — a 2-second chime won't do).
   `C:\Windows\Media\*.wav` exists but those clips are only a few seconds
   long — usable only as a last resort for the "does it play at all" checks,
   not for Steps 8-9 below.
2. If you can't find one, generate a real 30-second stereo WAV file with
   this PowerShell one-liner (pure sine tone — a real decodable audio file,
   not a JUCE test fixture, which is all that's actually required here):
   ```powershell
   Add-Type -AssemblyName System.Speech
   $s = New-Object System.Speech.Synthesis.SpeechSynthesizer
   $s.SetOutputToWaveFile("$env:USERPROFILE\Desktop\demo1.wav")
   $s.Speak("This is a test track for the D J App audio engine milestone three checklist. " * 6)
   $s.Dispose()
   ```
   This produces spoken audio (easier to judge a pitch/rate change by ear
   than a pure tone) roughly 30-40 seconds long at
   `C:\Users\nagi\Desktop\demo1.wav`.
3. Copy that file into the repo's track folder and name it exactly
   `demo1.wav`:
   ```
   copy "%USERPROFILE%\Desktop\demo1.wav" "C:\Users\nagi\Desktop\DJ-App\client\assets\tracks\demo1.wav"
   ```
   (If you found your own file instead, copy it into
   `C:\Users\nagi\Desktop\DJ-App\client\assets\tracks\` and remember its
   filename — you'll reference it in `manifest.json` below instead of
   `demo1.wav`.)
4. Create `C:\Users\nagi\Desktop\DJ-App\client\assets\tracks\manifest.json`
   (this exact filename — not `manifest.example.json`, which is a separate,
   already-committed template) with this content, in Notepad or any editor:
   ```json
   { "tracks": [ { "id": "demo1", "title": "Demo One", "file": "demo1.wav", "bpm": 120 } ] }
   ```
   Validation rules this must satisfy (`LocalFileRepository.cpp`): `id` is
   1-64 chars of letters/digits/`.`/`_`/`-` only; `title` is any string;
   `file` is a bare filename (no `\`, `/`, or `..`) that must name a real
   file sitting directly in this same `tracks/` folder. If you used your own
   filename in Step 3, put it here instead of `demo1.wav`.
5. This file is gitignored on purpose (`client/assets/tracks/*` except the
   example) — it's local-only, never committed, and won't show up in `git
   status`. That's expected, not a mistake.

## Steps

1. Open the x64 Native Tools Command Prompt as described above. Navigate to the
   repo:
   ```
   cd C:\Users\nagi\Desktop\DJ-App
   git checkout claude
   git pull
   ```
2. Configure and build:
   ```
   cmake -S client -B client/build/windows -G Ninja
   cmake --build client/build/windows
   ```
   - Expected: both `dj-app-client` and `dj-app-tests` build clean, no
     errors (warnings are worth flagging back but not blocking).
3. Run the test suite:
   ```
   ctest --test-dir client/build/windows --output-on-failure
   ```
   - Expected: **0 failures**. Don't compare against a hardcoded count from
     this doc — counts drift as tests are added. If you want a reference,
     ask me for the container's current count at the time you run this
     (87 as of 2026-08-06; re-verify, don't trust a stale number here).
4. Find and run the client artefact:
   ```
   "client\build\windows\dj-app-client_artefacts\Debug\DJ App.exe"
   ```
   (Quote the path — `cmd.exe` splits on the space in `DJ App.exe`
   otherwise.) This `Debug` subfolder is a known quirk on this host
   (`DEVIATIONS.md`, 2026-08-05): JUCE nests it even though Ninja is
   single-config, not yet root-caused, but confirmed consistent across both
   the M1 and M3 host runs. If it's ever missing, fall back to:
   ```
   "client\build\windows\dj-app-client_artefacts\DJ App.exe"
   ```
   and tell me — that would mean the quirk resolved or changed.
   - Expected: the window opens with the M2 track list on one side (showing
     "Demo One" from the manifest you just created) and the M3 dev-UI
     controls (Load Selected, Play/Pause, Seek, Gain, Rate) on the other —
     no crash on startup.
5. Select "Demo One" in the list, click **Load Selected**.
   - Expected: no crash; the seek slider's range updates to the track's
     duration (roughly 30-40s if you used the PowerShell-generated file).
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
10. Let the track play to its end without touching any control (turn Rate
    back to 1.0 first so this doesn't take forever).
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

Report back: pass/fail per step, which artefact path actually existed in
Step 4, and note anything audibly wrong (crackling, wrong pitch direction,
seek landing at the wrong position) even if it isn't a crash.
