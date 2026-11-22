# Session notes — 2026-11-21

Working tree was clean on `main` at end of session. This file captures state that
matters for picking the work back up.

## Where things stand

- **v0.2.6 released** (gitflow release branch `release/v0.2.6`, merged to `main`
  and `develop`, tagged): volume-control fix for issue #3 — AirPlay slider now maps
  through a shairport-sync `vol2attn()` audio-taper onto a capped [−6, −54] dBFS
  range (one transfer function, one Q16 gain per volume change, one PCM scaling
  stage, dead i2s volume stage deleted). Builds clean esp32+esp32s3 on IDF v5.5.1,
  host unit test `make -C test`, flashed & booted on ESP-68F8. Five agentic review
  rounds on PR #4 ending `approve` (see PR comments); NaN/malformed `SET_PARAMETER`
  fails safe to the floor.
- **PR #4 merged** (`224ecce`) from `openhands/issue-3`.
- **Gitflow initialised**: `main` = production, `develop` = integration
  (created from `main` at v0.2.6). Release flow from now on:
  `git flow release start vX.Y.Z` → bump `PROJECT_VER` → `git flow release finish`.
- **v0.2.5 released**: https://github.com/jptrsn/esp-airsync/releases/tag/v0.2.5
  - Tag `v0.2.5` → commit `269b335` ("version bump"), on `main`.
  - Assets: `esp-airsync-esp32.bin`, `esp-airsync-esp32s3.bin`,
    `libesp-raop-receiver-esp32.a`, `libesp-raop-receiver-esp32s3.a`.
- **PR #1 merged** (`33e84d7`): mDNS starvation fix + RTP/decode pipeline rework +
  teardown hardening (the AirPlay drop-out-at-~33s regression).
- **PR #2 merged** (`33e84d7`): `audio_buffer_stop()` join handshake replacing
  delay-and-delete; leak-don't-delete on wedged task; atomic `running`; restart
  guard. Reviewed by independent agent — approve, "cannot free mutex/frames while
  the task is alive: No."

## Deferred items — investigated & intentionally NOT changed

These were reviewed against docs-mcp + upstream sources (squeezelite-esp32,
shairport-sync, fetched live). Summary:
1. `audio_buffer_stop` delay-and-delete — **fixed** (PR #2).
2. `stalled` counter resets on any packet (`rtp.c:961/981`) — byte-identical to
   squeezelite-esp32 `rtp.c:647/667`. Kept to stay faithful to reference.
3. `audio_buffer_write` uses `portMAX_DELAY` on `ab_mutex` (`audio_buffer.c:350`)
   — mutex is `xSemaphoreCreateMutex()` (priority-inheriting, `semphr.h:643`),
   hold time is a memcpy. Not a hazard. No change.

## Hardware flashed & validated (both at v0.2.5)

### ESP32 WROVER-B (the "soundbar" unit)
- mDNS name `ESP-68F8.local`, IP `192.168.1.62`.
- owntone output id `189206612437240`.
- Serial: `/dev/cu.usbserial-0001` (CP2102 UART bridge) — flashes reliably.
- I2S pins BCK 26 / WS 25 / DOUT 22. 4MB PSRAM mapped.
- Streaming validated clean (I2S ~176.4kB/s, zero resends/overruns).

### ESP32-S3 (second test unit)
- mDNS name `ESP-CAAC64.local`, IP `192.168.1.233`, MAC `fc:01:2c:ca:ac:64`.
- owntone output id `277081976646756`.
- I2S default pins BCK 11 / WS 9 / DOUT 10. 8MB PSRAM (fully mapped).
- **FLASHING GOTCHA**: the devkit has two USB ports.
  - `/dev/cu.usbmodem5A450578761` (CH343 UART bridge, VID 0x1a86) — flash FAILS
    with "Checksum error" / "invalid message format" at every baud, with and
    without stub. Do not use for flashing.
  - `/dev/cu.usbmodem1101` (native USB-Serial/JTAG, VID 0x303a) — flashes cleanly.
    **Use this one.**
- Streaming validated clean past the old ~33s point; 2 underruns both at
  startup/track-transition (normal priming), zero mid-stream.

## Environment facts (reusable)

- IDF: `~/esp/v5.5.1/esp-idf`. Build: `source ~/esp/v5.5.1/esp-idf/export.sh && idf.py build`.
- owntone 29.3 at `192.168.1.13:3689`.
  - `PUT /api/outputs/{id}` JSON `{"selected":true|false}` → 204 on success.
  - `PUT /api/player/play` / `pause` → 204.
  - `GET /api/outputs`, `GET /api/player`.
- Serial capture needs a PTY: run `esp_idf_monitor` under `script -q <logfile> ...`
  (plain `idf.py monitor` / bare `-m esp_idf_monitor` fail with "requires stdin to
  be a TTY" in non-interactive shells).
- Release flow: bump `PROJECT_VER` in `CMakeLists.txt`, commit "version bump",
  push `main`, then `gh release create <tag> <assets> --target main`. (`release.sh`
  also works but needs `GITHUB_TOKEN` in `.env`; the `gh` keyring token is cleaner.)
  Do NOT create new GitHub repos.

## Release assets still needed if rebuilding
Build per target (`idf.py set-target esp32` / `esp32s3`), then copy:
- `build/esp-airsync.bin` → `esp-airsync-<target>.bin`
- `build/esp-idf/esp-raop-receiver/libesp-raop-receiver.a` → `libesp-raop-receiver-<target>.a`

## Current live state (was true at session end)
- owntone: ESP-68F8 (esp32 soundbar) selected + playing; Volumio Kitchen also selected.
- ESP32-S3 (`ESP-CAAC64`) flashed with v0.2.5, running, but NOT selected in owntone.
- ESP-68F8 (esp32) flashed with the v0.2.6 volume-control build, boots clean
  (WiFi + mDNS + RAOP up); re-test iOS slider end-to-end on the release bin.