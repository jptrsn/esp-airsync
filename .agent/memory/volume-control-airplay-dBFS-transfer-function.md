---
topic: volume-control
created: 2026-11-20
status: resolved
tags: [audio, volume, raop, airplay, pcm5102a, bugfix]
---

# AirPlay volume used raw as dBFS gain — min audible, max clipped, linear scale

## Symptom
- Slider at minimum (AirPlay `-30.0`) was still clearly audible: the code applied a
  gain of `0.0316` (−30 dBFS) into the PCM5102A's fixed 2.1 Vrms line output.
- Slider at maximum (`0.0`) applied exactly `1.0` (0 dBFS): zero headroom, loud
  material clipped at the DAC/analog stage.
- Slider movement was linear-in-dB everywhere: no fine control near the top, no
  fast roll-off at the bottom.
- `i2s_output: Volume set to 1.00 (100%)` logged on every session regardless of the
  slider — a second volume stage that looked live but was dead code.

## Root cause
The AirPlay RAOP `SET_PARAMETER volume` value (contract: `0.0` loudest … `-30.0`
quietest, `-144.0` mute sentinel) was forwarded **raw** and reinterpreted as a dBFS
figure in `audio_buffer.c` (one software-libm `powf(10, vol_db/20)` per ~8 ms
frame, then a per-sample float multiply — the classic ESP32's LX6 has a
single-precision FPU but no hardware doubles, and `powf()` is a software
routine regardless). The RAOP slider is a
*position*, not a gain. Upstream squeezelite-esp32 normalises on ingest
(`volume = (volume == -144.0) ? 0 : (1 + volume / 30);`); this fork omitted that,
and shairport-sync solves the same problem with a purpose-built transfer function
(`vol2attn()`).

## Fix
Single source of truth for the volume value, one attenuation stage, documented units.

- **New module** `components/esp-raop-receiver/src/raop_volume.{h,c}`: a port of
  shairport-sync's three-segment `vol2attn()` (standard profile) mapping the AirPlay
  slider onto named constants `RAOP_VOLUME_MAX_DBFS = -6.0` (headroom) and
  `RAOP_VOLUME_MIN_DBFS = -54.0` (silent floor, 48 dB range), plus
  `raop_volume_to_gain_q16()` (shairport's `fix_volume` pattern:
  `65536 * 10^(dB/20)`, once per volume change) and `raop_volume_scale_s16()`
  (integer multiply-shift-saturate, memset fast path for mute, no-op for unity).
  `-144.0` (and below) → gain 0 → exact digital silence. Pure arithmetic, no IDF
  headers → host-testable.
- **One PCM scaling stage**: `audio_buffer.c` applies the cached Q16 gain in the
  output task; the dead `i2s_output_set_volume()` and its float scaling loop are
  deleted (acceptance: exactly one code path scales PCM).
- **Units documented** in the headers: `raop_volume_cb_t` and `raop_get_volume()`
  now state they carry raw AirPlay dB, not 0.0–1.0 and not dBFS; `i2s_output_write()`
  states data arrives pre-scaled.
- **Host unit test** `test/test_raop_volume.c` (plain `make -C test`): endpoints,
  ≥45 dB span, monotonic + continuous sweep, mute → all-zero samples, ≥3 dB
  headroom (full-scale sine and rail-to-rail never reach the rails), the
  first-unit-vs-last-unit dB step ≥ 2× non-linearity check, and the issue's
  11-point transfer table as a regression pin.
- **Out-of-range fail-safe**: any value outside [0, −30] that is not the mute
  sentinel maps to the *floor* (upstream `vol2attn()` behaviour — a malformed
  high value from a corrupt `SET_PARAMETER` must never map to full volume).
- **HARDWARE mode contract**: the library never scales PCM in HARDWARE mode
  (buffer pinned at unity); documented on `RAOP_VOLUME_HARDWARE` and
  `volume_cb` in `esp_raop_receiver.h`. `raop_cmd()`'s `dmcp.device-volume`
  reporting passes raw dB through instead of misreading it as normalised 0–1.

## Verification
- `make -C test && ./test/test_raop_volume` → all checks pass.
- `gcc -fsyntax-only` against IDF stub headers for `audio_buffer.c`, `raop_core.c`,
  `i2s_output.c` — a syntax gate only, **not** a firmware build.
- **Target builds: DONE.** `idf.py build` for **esp32** and **esp32s3** on IDF v5.5.1
  both pass clean (no warnings in the changed files), and the esp32 image was
  flashed to a physical ESP32 (MAC ac:15:18:ec:68:f8): boots, joins WiFi, RAOP
  receiver advertises, no crashes. (The OpenHands dev container that produced the
  branch has no ESP-IDF: ephemeral overlay filesystem, no CAP_SYS_ADMIN for nested
  Docker — builds belong on the host IDF v5.5.1 install at `~/esp/v5.5.1`.)
- **Agentic review (run 1)** on PR #4: request-changes → fixed — PR-body
  overclaims (per-sample `powf`, `(Volume-1)*30` "consistency" on dead code),
  HARDWARE-mode contract documentation, out-of-range fail-safe, mute log line,
  super-unity test comment.
- **Agentic review (run 2)**: the run-1 fail-safe leaked NaN to full volume
  (two-sided comparison is always false for NaN). Fix: upstream's *positive*
  in-range test `!(x <= MAX && x >= MIN) → floor`, plus NaN pins in the host
  test, `sscanf` return checked in `raop.c`, initial-gain-by-mode extracted to
  a pure host-tested helper. **Lesson: range-validation guards must be shaped
  to catch NaN — test the predicate's negation, not two-sided clamping.**
- Hardware: drag the iOS slider to both ends; expect silence at the bottom and no
  clipping at the top; `raop_core` now logs `Volume: AirPlay %.2f dB -> %.2f dBFS
  (Q16 gain N)` (1/s throttle) and the misleading `i2s_output: Volume set to...`
  line is gone. Note max output is ~half the previous voltage (the −6 dBFS ceiling is
  deliberate headroom); `RAOP_VOLUME_MAX_DBFS` is the single knob if it's too low for a
  given amplifier.

## Key learnings (reusable)
- **Protocol units are contracts.** A dB figure in a protocol is not automatically a
  dBFS gain. When a control value is forwarded "raw", find the spec's semantic range
  first (here: AirPlay's [0, −30] slider + −144 mute sentinel) and convert at the
  boundary.
- **The reverse mapping is a tell.** `raop.c` already reconstructed AirPlay dB from a
  normalised 0–1 value when reporting `dmcp.device-volume` — proof the input side
  should have been normalised too.
- **Dead volume stages lie.** A second scaling stage that is never called but still
  logs its state sends debuggers down the wrong path. One value, one place that
  scales PCM, units in every header.
- **Keep the hot loop integer.** Transfer curve + `powf` once per volume change into a
  cached Q16 gain; per-sample work stays multiply-shift + saturate (`powf` is
  software libm even on the LX6's single-precision FPU). The PCM tap deliberately
  stays pre-attenuation (issue #3 says
  leaving visualisation amplitude tracking the source, not the slider, is acceptable).
- **Widen the range, don't just shift it.** −6/−54 dBFS gives 6 dB headroom *and*
  48 dB of range (≥7 bits of resolution at the bottom) from one pair of constants.
