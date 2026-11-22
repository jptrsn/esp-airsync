---
topic: build-environment
created: 2026-11-20
status: active
tags: [build, esp-idf, ci, environment]
---

# No ESP-IDF in the OpenHands container — but there IS one on the host

> **Update 2026-11-21:** this entry describes the OpenHands automation container only.
> The macOS dev host has a working **ESP-IDF v5.5.1** at `~/esp/v5.5.1/esp-idf`
> (`source ~/esp/v5.5.1/esp-idf/export.sh`), a physical ESP32 on `/dev/cu.usbserial-0001`,
> and PR #4's esp32 + esp32s3 builds both pass there clean. Run target builds on the
> host; nothing below applies to it.

## Evidence (measured 2026-11-20 in the OpenHands automation container)

- `idf.py` / `IDF_PATH` absent; `cmake`, `ninja`, `ccache`, `pkg-config` also absent.
  `gcc` 14 and Python 3.13 are present, which is enough for host unit tests.
- Container, not host: `/.dockerenv` exists, PID 1 is `tini`, `/` is an overlay whose
  upperdir lives on the host's `/dev/nvme0n1p2`. **Only `~/.openhands` is a named
  volume** (`openhands-stack_openhands-data`); `~`, `/tmp`, `/usr/local`, dpkg state are
  all the ephemeral overlay → anything installed evaporates on container rebuild.
- **Nested Docker is a dead end.** `dockerd` starts with
  `--iptables=false --ip6tables=false --bridge=none`, but `docker run` dies at
  `mount … operation not permitted`: no `CAP_SYS_ADMIN`. Don't retry it.
- No CI: the repo has no `.github/workflows`, and `gh pr checks` reports
  *"no checks reported"* — nothing builds this automatically.

## Policy

Do not install ESP-IDF (~2-3 GB toolchain + IDF checkout) into this container. It is
wasted on an ephemeral filesystem and needs `sudo apt-get` writes; the user has
explicitly ruled it out. **`idf.py build` for `esp32` and `esp32s3` on IDF v5.5.1 is a
human verification step.** Say so plainly in PRs rather than implying a build happened.

## What to run instead, and what it does and doesn't prove

| Check | Command | Proves |
|---|---|---|
| Pure-logic units | `make -C test && ./test/test_raop_volume` | host-compilable + logic correct |
| Syntax/types of IDF-bound files | `gcc -fsyntax-only -std=gnu11 -Wall -Wextra -I <stub-idf-headers> -I . -I ../include -I ./private_include <file>.c` | parses + typechecks. **Not** a firmware build |

For the stub-header trick, minimal `freertos/{FreeRTOS,task,semphr,timers}.h`,
`esp_{log,err,timer,mac,netif,heap_caps}.h`, `driver/{i2s_std,gpio}.h`, `mdns.h`,
`lwip/inet.h` are enough to syntax-check `audio_buffer.c`, `raop_core.c`,
`i2s_output.c`. `esp_log.h` must pull in `<stdio.h>` or `snprintf` looks undeclared.
Regenerate them as scratch under `/tmp` and delete afterwards — they are not committed.

## Also true about this container

`uname -m` is **aarch64** (Nvidia Spark), 20 cores, ~245 GB free on the shared NVMe.
Espressif *does* publish `aarch64-linux-gnu` Xtensa toolchains, so a native build is
theoretically possible — it just isn't worth it here given the ephemeral filesystem.
