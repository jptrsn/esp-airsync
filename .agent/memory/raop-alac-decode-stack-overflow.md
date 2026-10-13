---
topic: raop-audio-pipeline
created: 2026-10-12
status: resolved
tags: [bugfix, memory-corruption, alac, raop, i2s, debugging]
---

# Mid-track audio glitch — ALAC decode out-of-bounds stack write

## Symptom
Intermittent mid-track audio corruption on the ESP32 AirPlay1 receiver: the
original audio with persistent static/noise mixed in. It persists for the rest
of the track and clears when skipping to the next track (or sometimes when the
next track begins). No clear time pattern. Reproduced on every device tested.
A Pi Zero W running shairport-sync, playing the same source (owntone MP3) over
the same network, was clean.

## Root cause
`alac_decode()` in `components/esp-raop-receiver/src/rtp.c` performed a **32-bit
store into a 16-bit stack variable on every decoded RTP packet**.

- `alac_to_pcm()` (Apple `ALACDecoder::Decode`) always writes a 32-bit
  `*out_frames`.
- A local fork revision changed the out-parameter to `u16_t *outsize` and cast
  it to `unsigned int*`, so that 32-bit store overflowed **2 bytes of the
  caller's (`buffer_put_packet()`) stack**.
- Strict-aliasing + out-of-bounds write = undefined behavior. The victim is
  stack-layout dependent, which is exactly why the glitch was intermittent and
  patternless.

This was a **regression vs upstream** `sle118/squeezelite-esp32@master-v4.3`
(`components/raop`), which uses a 32-bit `unsigned int frames` local and returns
`frames * 4`. Introduced in commit `d336531` (library extract — originally wrote
`&abuf->len` into the packed `abuf_t`, clobbering `ready`/`allocated`), retargeted
to a local in `1e9a0fa`.

## Evidence
- `diff` of local `rtp.c` vs upstream v4.3: `alac_decode` is the **only**
  functional delta (the rest is whitespace / log levels / `RAOP_*`→`RAOP_INT_*`
  renames / `heap_caps` / task-core pinning).
- `alac_wrapper.h` + upstream `alac_wrapper.cpp` confirm `unsigned *out_frames`
  (32-bit out).
- `git log -S 'u16_t *outsize'` pins the regression to `d336531` / `1e9a0fa`.
- Pi Zero W + shairport-sync clean with the same source/network isolates the bug
  to the esp-airsync implementation (rules out network, encoder, protocol).
- AirPlay 1 uses a **fixed per-session AES IV** (no CBC chaining) → crypto cannot
  desync mid-track; ruled out as the corruption source.

## Fix
`components/esp-raop-receiver/src/rtp.c`:
- Reverted `alac_decode()` to the upstream 32-bit pattern:
  `unsigned int frames = 0; ... return (u16_t)(frames * 4);`
- Caller: `abuf->len = alac_decode(ctx, abuf->data, data, len);` (no 16-bit local).
- Added defensive `frames = 0` init + a `WARN` on ALAC decode failure.

### Secondary fixes (confirmed real, applied)
- `audio_buffer.c`: `audio_buffer_flush()` now resets `timing_correction`
  (skip/pause) — a leftover correction no longer bleeds into the next track.
- `audio_buffer.c`: the three 2048-byte hot-path arrays in `audio_output_task`
  moved off the 4096-byte task stack to file-scope (de-risks stack overflow).
- `i2s_output/CMakeLists.txt`: added `esp_timer` requirement (for diagnostics).

## Diagnostics added (keep them; they make the next audio bug cheap to find)
- `rtp.c`: 5s `rtp health` log — resend/missed/silent/discarded counters + RTP
  task stack high-water mark.
- `audio_buffer.c`: 5s `health` log — ring fill, played/written/underrun/skip/
  pause counters + task stack high-water; `UNDERRUN` warn when the ring drains.
- `raop_core.c`: `WARN` on every drift SKIP/PAUSE with error-ms + timestamp.
- `i2s_output.c`: 5s actual B/s vs expected 176400 B/s (44100 × 2 ch × 2 B).

## Verification
- `idf.py set-target esp32 && idf.py build` — passes, no warnings in modified
  files (ESP-IDF v5.5.1).
- On hardware: `idf.py -p PORT flash monitor`, stream long MP3 tracks from
  owntone, watch the serial log.
  - Glitch gone → confirmed.
  - If a glitch recurs, correlate the onset with the logs:
    - `drift: SKIP/PAUSE` bursts → timing correction (`RAOP_INT_TIMING`)
    - `UNDERRUN` / low `fill` / `i2s < 176400 B/s` → producer starvation
    - falling `stack_hwm` → task stack pressure
    - `ALAC decode failed` → codec issue
  - Toggle WLED on/off to rule the FFT tap in/out (was untested at fix time).

## Key learnings (reusable)
- **Fork vs upstream is the top lever.** `rtp.c`/`raop.c` are a fork of
  `sle118/squeezelite-esp32@master-v4.3` (`components/raop`). For any
  audio/protocol bug, `diff` the local files against that tag first — local
  changes are the prime suspect.
- **A clean reference device isolates the layer.** Same source + network +
  tracks with shairport-sync clean ⇒ the bug is in the local implementation,
  not network/encoder/protocol.
- **Keep ALAC out-frames 32-bit.** `alac_to_pcm()`/`ALACDecoder::Decode` always
  store 32 bits into the out-frames pointer. Never pass a 16-bit target.
- **Fixed-IV AirPlay 1 can't desync mid-track** — don't chase AES as a
  mid-track corruption source.
- **Persistent-until-skip = stateful corruption.** Points to decoder state or a
  leaked static (e.g. `timing_correction`), not a transient underrun.

## Preventive measures
- Add a static-analysis / ASan pass over the RAOP component in CI if possible.
- Keep the 5s health/drift/underrun/i2s logs in the tree.
