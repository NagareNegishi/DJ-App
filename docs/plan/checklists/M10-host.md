# M10 host checklist - beat alignment (time-stretch, beat detection, sync)

Run on the Windows host, from the **x64 Native Tools Command Prompt** for VS Build Tools
(see `M3-host.md` if you need the reminder). M10 adds a real time-stretcher (rate now
changes speed without changing pitch), a pitch slider, real beat detection per track, and a
per-deck sync button that tempo-matches and phase-aligns one deck to the other. This is the
first checklist where the audio content itself matters - you need two tracks with a real,
detectable beat at two different, known tempos, not the plain sine tone earlier checklists
used.

This repo is checked out at `C:\Users\nagi\Desktop\DJ-App` on this host.

## Prerequisites

Same toolchain as `M3-host.md`. **New for M10: two click-track `.wav` files at different,
known BPMs**, generated the same way earlier checklists generated their sine tone (a
PowerShell one-liner producing a real decodable file), but this time with actual periodic
clicks a beat detector can lock onto - a steady tone has no rhythmic content at all and
would make every sync-related step below untestable.

1. In a PowerShell window, define and run this once (writes two 15-second mono click
   tracks, 120 BPM and 90 BPM - the same two BPMs and the same click-track shape the
   automated `BeatDetectorTest.cpp` suite already uses, so the values below are
   cross-checked against what qm-dsp is already known to recover correctly in the
   container):
   ```powershell
   function New-ClickTrackWav {
       param([string]$Path, [double]$Bpm, [double]$DurationSeconds, [int]$SampleRate = 44100)
       $numSamples = [int]($SampleRate * $DurationSeconds)
       $samples = New-Object int16[] $numSamples
       $beatInterval = 60.0 / $Bpm
       $clickSamples = [int]($SampleRate * 0.03)
       $beatTime = 0.2
       while ($beatTime -lt $DurationSeconds) {
           $startSample = [int]($beatTime * $SampleRate)
           for ($j = 0; $j -lt $clickSamples -and ($startSample + $j) -lt $numSamples; $j++) {
               $env = 1.0 - ($j / $clickSamples)
               $samples[$startSample + $j] = [int16]([math]::Sin(2 * [math]::PI * 1000 * $j / $SampleRate) * $env * 30000)
           }
           $beatTime += $beatInterval
       }
       $bytes = New-Object byte[] ($numSamples * 2)
       [System.Buffer]::BlockCopy($samples, 0, $bytes, 0, $bytes.Length)
       $stream = [System.IO.File]::Create($Path)
       $writer = New-Object System.IO.BinaryWriter($stream)
       $writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
       $writer.Write([int32](36 + $bytes.Length))
       $writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
       $writer.Write([int32]16)
       $writer.Write([int16]1)
       $writer.Write([int16]1)
       $writer.Write([int32]$SampleRate)
       $writer.Write([int32]($SampleRate * 2))
       $writer.Write([int16]2)
       $writer.Write([int16]16)
       $writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
       $writer.Write([int32]$bytes.Length)
       $writer.Write($bytes)
       $writer.Close()
       $stream.Close()
   }
   New-ClickTrackWav -Path "$env:USERPROFILE\Desktop\demo-beat120.wav" -Bpm 120 -DurationSeconds 15
   New-ClickTrackWav -Path "$env:USERPROFILE\Desktop\demo-beat90.wav" -Bpm 90 -DurationSeconds 15
   ```
   - Expected: two files appear on the Desktop; each plays as a steady, clearly audible
     metronome-like click when opened directly (sanity-check at least one by ear before
     continuing).
2. **Also generate one longer track**, reusing the same function, for Step 14's
   message-thread-stall check below: `EngineAdapter::handleDelta` runs `BeatDetector::analyze()`
   synchronously on the message thread (`docs/plan/DEVIATIONS.md`, deferred question - not yet
   checked against real evidence), and analysis time scales with track length, so the 15s
   tracks above analyze too fast for a stall to be visible even if one exists. 3 minutes is
   long enough to make a real stall noticeable without the file being unwieldy to generate/copy:
   ```powershell
   New-ClickTrackWav -Path "$env:USERPROFILE\Desktop\demo-beat-long.wav" -Bpm 128 -DurationSeconds 180
   ```
   - Expected: one ~3-minute file appears, same click sound as the two above, just longer
     (this one doesn't need a by-ear sanity check - Step 14 only cares about load/analysis
     time, not correct BPM detection).
3. Copy all three into the track folder and register them in `manifest.json` alongside the
   existing entries:
   ```
   copy "%USERPROFILE%\Desktop\demo-beat120.wav" "C:\Users\nagi\Desktop\DJ-App\client\assets\tracks\demo-beat120.wav"
   copy "%USERPROFILE%\Desktop\demo-beat90.wav" "C:\Users\nagi\Desktop\DJ-App\client\assets\tracks\demo-beat90.wav"
   copy "%USERPROFILE%\Desktop\demo-beat-long.wav" "C:\Users\nagi\Desktop\DJ-App\client\assets\tracks\demo-beat-long.wav"
   ```
   ```json
   { "id": "demo-beat120", "title": "Demo Beat 120", "file": "demo-beat120.wav", "bpm": 120 },
   { "id": "demo-beat90", "title": "Demo Beat 90", "file": "demo-beat90.wav", "bpm": 90 },
   { "id": "demo-beat-long", "title": "Demo Beat Long (3 min)", "file": "demo-beat-long.wav", "bpm": 128 }
   ```

## Steps

### Solo (one window, not connected) - time-stretch, pitch slider, beat detection, sync

1. Update and build:
   ```
   cd C:\Users\nagi\Desktop\DJ-App
   git checkout claude
   git pull
   cmake -S client -B client/build/windows -G Ninja
   cmake --build client/build/windows
   ```
   - Expected: `dj-app-client` and `dj-app-tests` both build clean (this milestone vendors
     two new dependencies via `FetchContent` - `signalsmith-stretch` and `qm-dsp` - so the
     first configure will take noticeably longer while they download).
2. Run the test suite:
   ```
   ctest --test-dir client/build/windows --output-on-failure
   ```
   - Expected: **0 failures** (re-verify the current count in the container, don't trust a
     stale number from a prior milestone's checklist).
3. Launch the client, load "Demo Beat 120" onto deck A.
   - Expected: same as every prior milestone - title and controls enable. Note two new
     widgets next to the rate slider: a **pitch slider** (labelled range roughly -12..12)
     and a **Sync button**.
4. **Rate now changes speed only, not pitch - the headline behavior this milestone adds.**
   Press Play, let the click track establish its steady rhythm, then drag the rate slider
   to roughly 1.5x.
   - Expected: the clicks noticeably speed up (tighter spacing) but their pitch/timbre
     stays the same - compare against the pre-M10 behavior described in
     `02-protocol.md`'s superseded note ("rate affects pitch (no timestretch)"), which no
     longer applies. Double-click the rate slider to snap back to 1.0x and confirm the
     tempo returns to normal.
5. **Pitch slider changes pitch only, independent of tempo.** With rate back at 1.0x and
   still playing, drag the pitch slider up several semitones.
   - Expected: the click's tone rises in pitch while the beat spacing (tempo) stays
     exactly the same. Double-click the pitch slider to snap back to 0 and confirm the
     pitch returns to normal. Try rate and pitch offset together (e.g. rate 1.3x, pitch +5)
     - both effects should be audible simultaneously and independently.
6. **Startup-latency gap.** Stop playback, seek back to 0, then press Play again and listen
   closely to the very first fraction of a second.
   - Expected: per `docs/plan/10-beatsync-design.md`'s accepted fallback (no pre-roll
     implemented), a brief near-silent gap before the first click is normal and acceptable
     - flag this step as a **fail** only if the gap is long enough to be disruptive (more
     than a fraction of a second) or if anything glitches/clicks audibly at the seam rather
     than just staying quiet.
7. Load "Demo Beat 90" onto deck B and press Play on both decks together (both at rate
   1.0x, pitch 0).
   - Expected: you can clearly hear two different tempos running independently - deck A's
     clicks noticeably faster than deck B's (120 vs 90 BPM).
8. **Sync deck A to deck B.** With both decks playing, click deck A's **Sync** button.
   - Expected: a brief moment after the click, deck A's rate slider jumps to reflect the
     tempo ratio (90/120 = **0.75**, i.e. roughly 3/4 along the slider's 0.5-2.0 range) and
     deck A's clicks visibly/audibly slow to match deck B's tempo. Listen for a beat or two
     after the jump: the two decks' clicks should land close together in time (phase-aligned
     - the "nudge to nearest beat" half of what Sync does), not just matched in tempo while
     staggered.
9. **Sync the other direction.** Stop both decks, reload fresh copies of both tracks (or
   just reset rate/position back to defaults - reload is simplest), press Play on both, and
   this time click deck B's Sync button.
   - Expected: deck B's rate jumps toward 120/90 ≈ **1.333** and deck B speeds up to match
     deck A, with the same phase alignment behavior as Step 8. Confirms Sync always corrects
     the deck you clicked, leaving the other deck untouched (deck A's rate/tempo should not
     have changed at all during this step).
10. **Sync is one-shot, not continuous.** Right after either sync above, manually drag the
    now-synced deck's rate slider away from the matched value.
    - Expected: it stays wherever you dragged it - Sync does not re-engage or fight your
      manual adjustment. This confirms Sync computes and applies a correction once, per the
      design doc's explicit "no ongoing beat-lock loop" decision.
11. **Sync with no detectable beat.** Load a track with no rhythmic content (e.g. reuse
    `demo1.wav`/`demo2.wav` from `M3-host.md`'s plain sine tone, if still present) onto one
    deck, a click track onto the other, and click Sync on the sine-tone deck.
    - Expected: nothing audibly changes on either deck (no rate jump, no seek) - a silent
      no-op, not a crash or an error dialog. This is the "beat detection failed on one
      side" case `model/BeatSync.h::computeBeatSync` documents as returning no correction.

### Multi-user (two windows, connected) - sync and pitch mirror like every other control

12. Start the server in the container (`cd server && npm start`), copy the room code,
    launch a second client instance, connect both windows with different display names -
    same as `M7-host.md` Steps 1-5.
13. In window A, click **Claim Control**, load "Demo Beat 120" to deck A and "Demo Beat 90"
    to deck B, press Play on both.
    - Expected: both windows show matching track titles and playback state for both decks
      (window B stays silent - only local state/UI mirrors, never audio, same as every
      earlier multi-user checklist).
14. **Message-thread stall check during beat analysis (deferred question, see
    `docs/plan/DEVIATIONS.md`).** `EngineAdapter::handleDelta` runs `BeatDetector::analyze()`
    synchronously on the message thread, and a remote delta takes the exact same code path
    as a local one - so loading a long track could, in principle, freeze either window's UI
    while analysis runs. The short 15s click tracks used in Steps 1-11 analyze fast enough
    that a stall wouldn't be noticeable even if one exists; this step uses the ~3-minute
    "Demo Beat Long" track from the Prerequisites specifically to make a real stall, if
    present, actually visible. Check **both** windows, since the concern is different for
    each:
    - In window A (**controller**), load "Demo Beat Long (3 min)" onto deck B (deck A is
      already busy with Steps 12-13's tracks). **Immediately** after clicking the track -
      within the same second, don't wait for the load to visibly finish - try to interact
      with something else in window A (drag deck A's gain slider, click deck A's Play
      button).
      - Fail criterion: if window A is unresponsive to that interaction for longer than a
        fraction of a second (the whole app feels "frozen" rather than staying interactive
        while the new track loads), that's a **fail** - the local, synchronous
        `analyze()` call is blocking window A's own message thread long enough to be
        user-visible. Note roughly how long the freeze lasts if one occurs.
    - At the same time, watch window B (**observer**) during that same load - it receives
      the track-load delta over the network and independently runs its own
      `BeatDetector::analyze()` on its own copy of the file, via the same `handleDelta` path.
      **Immediately** after window A's load starts, try to interact with window B (drag any
      slider, click any button) without waiting for anything to finish first.
      - Fail criterion: same as above, applied to window B - a stall here means the
        *observer's* re-analysis (triggered purely by the incoming remote delta, not
        anything window B's own user did) is blocking window B's message thread.
    - Either way, this is real evidence for `docs/plan/DEVIATIONS.md`'s deferred decision on
      `handleDelta`'s synchronous analysis call - **record the result there** (which
      window(s), if any, stalled, and roughly how long) rather than leaving it just in this
      checklist. A clean "no stall" result on both windows is also a valid, useful outcome
      (it means the deferred risk didn't materialize in practice at this track length) -
      log it either way, since the decision to defer explicitly promised revisiting this on
      real evidence rather than staying an open question forever.
15. In window A, drag deck A's pitch slider to a new value.
    - Expected: within ~100 ms, window B's deck A pitch slider mirrors the same value -
      same latency as every other synced control (gain, rate, loop, repeat).
16. In window A, click deck A's Sync button.
    - Expected: within ~100 ms, window B's deck A rate slider **and** position both update
      to match what happened locally in window A (Sync produces one `StateDelta` setting
      both `playbackRate` and `positionSeconds`, and both fields already sync via the
      existing protocol - no protocol version change this milestone). Window B's Sync
      button and pitch slider for deck A should be disabled/greyed throughout (it's an
      observer), same gating as every other deck control.
17. Close all windows via their close buttons (X).
    - Expected: clean exit, no hang, no leftover process in Task Manager. Stop the server
      in the container (Ctrl+C).

Report back: pass/fail per step, and specifically flag anything in Steps 4-5 (speed/pitch
independence - the actual audible change this milestone makes to existing rate behavior),
Step 6 (startup-latency gap), and Step 14 (message-thread stall) even if they technically
pass, since all three are the kind of thing that's easy to rate as "fine" on a quick look
but worth a second, careful pass - Step 14 in particular is a currently-open, deliberately
deferred design question that this checklist is the first real chance to get evidence on.
