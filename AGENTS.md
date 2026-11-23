# AGENTS.md — esp-airsync

ESP32 AirPlay 1 (RAOP) audio receiver in C on ESP-IDF, for ESP32-WROVER-B and
ESP32-S3 N16R8 with a PCM5102A-class I²S DAC. ALAC decode, PSRAM jitter/ring
buffering, NTP multi-room sync, optional WLED audio-reactive sync, web config +
captive portal + OTA. GPL-3.0. Version is `PROJECT_VER` in `CMakeLists.txt`
(currently `v0.2.6`).

## Layout

- `main/` — `main.c`, `wifi_manager.c` (captive portal `ESP-AIRSYNC-XXXXXX` is
  currently **open** — `WIFI_AUTH_OPEN`, no password; the WPA2 `authmode` and the
  `AP_PASSWORD` copy are both commented out at `wifi_manager.c:117-133`, leaving
  `AP_PASSWORD` (`espairsync`) as dead code — revisit it if the AP is meant to be
  secured; config page at `192.168.4.1`), `web_server.c`
  (esp_http_server, config + OTA pages), `config_manager.c` (NVS namespaces
  `wifi_config`, `device_config`, `i2s_config`, `wled_config`).
- `components/esp-raop-receiver/` — the RAOP library (RTSP `raop.c`, RTP +
  ALAC `rtp.c`, ring buffer/output `audio_buffer.c`, session `raop_core.c`,
  volume transfer function `raop_volume.c`). Fork of
  `sle118/squeezelite-esp32@master-v4.3` `components/raop`; protocol reference
  is shairport-sync.
- `components/i2s_output/`, `components/wled_sync/` — I²S DAC output, FFT→WLED
  UDP sync (needs `espressif__esp-dsp`).
- `test/` — host unit tests, **not** part of the `idf.py` build.
- `sdkconfig.defaults[.esp32|.esp32s3]`, `partitions.csv` (two ~1.75MB OTA app
  slots), `release.sh`.
- `.agent/memory/` — durable topic notes. See Workflows below.

## Commands

Host tests (the only automated check available; verified working with plain
gcc 14 / no extra deps):

```bash
make -C test && ./test/test_raop_volume   # expect: "all raop_volume checks passed"
make -C test clean
```

Firmware build / flash — requires ESP-IDF **v5.5.1** (README pins it;
`main/idf_component.yml` only asserts `idf >=5.0.0`, plus
`espressif/mdns >=1.4.0`, `espressif/esp-dsp >=1.0.0`):

```bash
idf.py set-target esp32 && idf.py build      # also: esp32s3
idf.py -p PORT flash monitor
```

`set-target` matters: PSRAM/flash/mDNS settings live in the per-target
defaults, and `build/` is reused between targets (`release.sh` runs `rm -rf
build` between them).

**There is no ESP-IDF in the OpenHands container and it must not be installed**
(~2–3 GB toolchain, needs sudo writes, ephemeral overlay filesystem, nested
Docker blocked — see
`.agent/memory/build-environment-no-esp-idf.md`). `idf.py build` for
`esp32`/`esp32s3` is a **human verification step**: say so plainly rather than
implying a firmware build happened.

**There is no CI and no lint/format config** (no `.github/workflows`,
no `.clang-format`, no `.pre-commit-config.yaml`, no `Makefile` at the root).
Do not invent a lint or CI command; match the surrounding style instead.

## Code style

- 4-space indent, no tabs; `snake_case`, `static` file-scope state.
- Logging: `static const char *TAG = "<module>";` + `ESP_LOG*` in `main/` and
  the non-RAOP components; inside `esp-raop-receiver` use the `LOG_ERROR` …
  `LOG_SDEBUG` wrappers from `src/log_util.h` (they hardcode the `"raop"` tag).
- New pure-logic code should stay free of ESP-IDF headers so it can be
  host-tested the way `raop_volume.c` is (see `test/Makefile`).

## Repository workflows

- **`.agent/memory/` is the memory convention**: read the relevant topic file
  before starting related work, append distilled learnings after, and **commit
  them** — uncommitted memory is lost. Organize by topic, one file per topic
  (see `.agent/memory/README.md` for the format).
- **Gitflow**: `main` = production, `develop` = integration. Release flow:
  `git flow release start vX.Y.Z` → bump `PROJECT_VER` → `git flow release
  finish`. Feature branches open from `develop`; merge to `develop`, then to
  `main` with a tag.
- **Releases**: `./release.sh` builds both targets, tags `PROJECT_VER`, and
  uploads `esp-airsync-<target>.bin` + `libesp-raop-receiver-<target>.a` to the
  GitHub release (needs `GITHUB_TOKEN`, optionally in an untracked `.env`).
  `gh release create <tag> <assets> --target main` is the equivalent by hand.
  The static library is shipped as a release asset too, so the RAOP component's
  public headers are a published surface, not just internals.
- `components/esp-raop-receiver/lib/libalac.a` is **prebuilt and committed**;
  the ALAC C++ sources are not in this tree. Link failures there mean the
  prebuilt archive mismatched the toolchain, not that a source file is missing.
- Runtime config from the web UI is written to NVS but **only takes effect
  after a reboot** — say so when testing config changes.

## Gotchas

- **Task priorities/affinity are load-bearing.** RTP RX (`CONFIG_PTHREAD_TASK_PRIO_DEFAULT + 5`,
  affinity per the IDF pthread default) > mDNS responder (`CONFIG_MDNS_TASK_PRIORITY=5`,
  i.e. *level with* the pthread default — it is unset in every `sdkconfig.defaults*`, so
  the IDF default is 5; pinned core 1) > decode (priority `default − 1`, pinned core 0)
  > audio output (priority 3, core 1). If you retune these, keep mDNS schedulable or the
  "device disappears mid-playback" symptom returns, but do **not** raise it above the
  audio path either — that only trades one starvation for another
  (`sdkconfig.defaults.esp32:20-22`). The `CONFIG_MDNS_TASK_AFFINITY_CPU1`
  block is duplicated in both `sdkconfig.defaults.<target>` files because it
  depends on `!FREERTOS_UNICORE` and must not sit in the shared defaults.
- **`xTaskCreateStatic*` takes a count of `StackType_t` elements, not bytes.**
  Always pass the `*_STACK_WORDS` macros (with their `_Static_assert` guards),
  never the `*_STACK_SIZE` byte macros.
- **Task stacks must live in internal RAM**, as static arrays — never PSRAM
  (PSRAM is unreachable while the flash cache is disabled during flash writes /
  OTA / NVS commits).
- **Keep ALAC out-frames 32-bit.** `alac_to_pcm()` always stores 32 bits into
  the out-frames pointer; passing a 16-bit target corrupts the caller's stack
  (`.agent/memory/raop-alac-decode-stack-overflow.md`).
- **Volume has one path.** AirPlay dB → `raop_volume_to_dbfs()` → cached Q16
  gain → a single PCM scaling stage in `audio_buffer.c`. Never add a second
  gain stage, and keep out-of-range/NaN input failing safe to the floor
  (`.agent/memory/volume-control-airplay-dBFS-transfer-function.md`).
- The health/drift/throughput logs are deliberate diagnostics — keep them; they
  are how the next audio bug gets diagnosed. Their cadences differ, so don't use
  one line's timing to judge another: `rtp health` (`rtp.c:934`) and the i2s
  throughput log (`i2s_output.c:101`) are every 5 s, `health` in `audio_buffer.c`
  is every **30 s** (deliberately rare to avoid flooding UART), and the
  `drift: SKIP` / `drift: PAUSE` warnings (`raop_core.c`) are event-driven, not
  periodic — absence just means sync stayed inside ±50 ms.
- `SESSION_NOTES.md` records hardware state and validation results (serial
  ports, owntone test endpoints, per-board flashing quirks) as of the last
  session; treat it as dated context, not current truth.
