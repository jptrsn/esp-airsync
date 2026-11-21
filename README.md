# ESP Air Sync
## An ESP32 Remote Audio Output Protocol (AirPlay1) Receiver

A high-quality RAOP audio receiver implementation for ESP32 boards with external PSRAM and a PCM5102a DAC, featuring multi-room synchronization and robust audio buffering.

## Features

- **AirPlay Audio Streaming**: Full AirPlay 1 protocol support with authentication
- **Multi-room Sync**: Synchronized playback across multiple AirPlay devices
- **High-Quality Audio**: PCM5102A DAC support via I²S (44.1kHz, 16-bit stereo)
- **Software Volume Control**: AirPlay slider mapped through an audio-taper
  curve onto a capped digital range (−6 dBFS ceiling for headroom, −54 dBFS
  floor for a truly silent minimum); AirPlay mute (−144 dB) is digital silence.
  Before the remote sends its first volume the receiver sits at the top of the
  slider, i.e. the −6 dBFS ceiling. Out-of-range values (anything outside
  0…−30 dB that is not the mute sentinel) fail safe to the floor
- **ALAC Decoding**: Apple Lossless audio codec support
- **Robust Buffering**: ~23 seconds of audio buffering with automatic drift correction
- **WLED Audio-Reactive Sync**: Real-time FFT analysis streamed to WLED devices for audio-reactive lighting effects (optional)
- **Web Configuration**: Simple web interface for WiFi setup and I²S pin configuration
- **WiFi Provisioning**: Easy setup via captive portal

## Hardware Requirements

- **ESP32-WROVER-B** (4MB PSRAM required)
- **PCM5102A DAC** or compatible I²S DAC
- Power supply (5V recommended)

### Default I²S Pin Configuration

- BCK (Bit Clock): GPIO 26
- WS (Word Select/LRCLK): GPIO 25
- DOUT (Data Out): GPIO 22

Pins can be reconfigured via the web interface.

## Setup

### Prerequisites

- [ESP-IDF v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/get-started/index.html)
- Python 3.8+

### Building and Flashing

1. Clone the repository:
```bash
git clone https://github.com/jptrsn/esp-airsync.git
cd esp-airsync
```

2. Set up ESP-IDF environment:
```bash
. ~/esp/esp-idf/export.sh
```

3. Configure (optional):
```bash
idf.py menuconfig
```

4. Build:
```bash
idf.py build
```

5. Flash to device:
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Initial Configuration

1. On first boot, the device creates a WiFi access point named `ESP-AIRSYNC-XXXXXX`
2. Connect to this network (password: `espairsync`)
3. Navigate to `192.168.4.1` in your browser
4. Enter your WiFi credentials and configure I²S pins if needed
5. Configure device name, if desired
5. Reboot device to save settings and connect to configured wifi

### Firmware Updates (OTA)

Once the device is running and connected to WiFi, firmware can be updated without a USB connection via the built-in OTA update page in the web interface:

1. Download the appropriate binary for your board from the [Releases page](https://github.com/jptrsn/esp-airsync/releases):
   - `esp-airsync-esp32.bin` for ESP32-WROVER-B
   - `esp-airsync-esp32s3.bin` for ESP32-S3 N16R8
2. Navigate to the device's web interface and go to **Firmware Update**
3. Select the `.bin` file and click upload — the device will flash and reboot automatically

The current firmware version is displayed on the update page.

## WLED Audio-Reactive Sync

esp-airsync can stream real-time FFT audio analysis to a WLED device, enabling audio-reactive lighting effects synchronized to your music.

### Requirements

- A WLED device running on an ESP32 (ESP8266 is not supported for audio sync)
- WLED firmware with the AudioReactive usermod enabled

### WLED Device Setup

1. Open the WLED web interface on your WLED device
2. Navigate to **Config → Usermods**
3. Scroll to the **AudioReactive** section at the bottom
4. Set **Sync mode** from `Off` to `Receive`
5. Save and reboot the WLED device

### esp-airsync Configuration

In the esp-airsync web interface, configure the following settings and reboot to apply:

| Setting | Description |
|---|---|
| **Enabled** | Enable or disable WLED sync |
| **Host** | IP address or multicast group of your WLED device (default: `239.0.0.1` for multicast) |
| **Port** | UDP port for audio sync packets (default: `11988`) |
| **Channel** | Audio channel to analyse: `mono` (L+R mixdown), `left`, or `right` |

> **Note**: All configuration changes require a reboot to take effect.

### How It Works

Audio frames are captured at the point of I²S output — when audio is actually playing — and passed through a 1024-point FFT. The resulting 16-band frequency spectrum is packed into a WLED V2 UDP audio sync packet and sent to the configured destination. This produces lighting effects that are accurately synchronized to the audio you hear.

### Usage

Once configured:
1. Device appears as the configured device name in your AirPlay device list
2. Select it from iTunes, OwnTone, or any AirPlay-compatible application
3. Audio streams with automatic synchronization for multi-room playback

## Architecture

- **RTSP Server**: Handles AirPlay control protocol
- **RTP Receiver**: Manages audio packet reception, ALAC decoding, and jitter buffering
- **Audio Buffer**: Large PSRAM-based ring buffer with timing-aware playback (Core 1)
- **I²S Output**: Hardware audio interface (Core 1); PCM is already volume-scaled
  by the audio buffer — the one and only gain stage in the output path
- **WLED Sync**: FFT computation (Core 0) and UDP sender (Core 1) for audio-reactive lighting
- **Timing Sync**: NTP-based synchronization for multi-room playback

### Audio pipeline and mDNS tuning

The RTP receive thread never decodes: it copies raw packets into a fixed
256-slot (~352KB PSRAM) single-producer/single-consumer queue and a separate
decode task does ALAC work, so socket draining is never blocked by decoding.
Priorities are set so RTP RX (10) outranks decode (4); decode is pinned to
core 0 so it cannot preempt the I2S/output consumer on core 1. See
`components/esp-raop-receiver/src/rtp.c`.

mDNS is the other half of staying connected: if the responder is starved it
cannot answer the controllers' unicast SRV/A resolves and the device is dropped
mid-playback. `sdkconfig.defaults.<target>` therefore raises
`CONFIG_MDNS_TASK_PRIORITY` to 5 (above decode's 4, below RTP RX's 10) and pins
the responder to core 1. This is defensive: the decode task now blocks on a
notification instead of busy-polling, which removes the original starvation
cause, but keeping mDNS above decode guards against future priority changes. If
you retune task priorities or affinity, keep mDNS schedulable or the "device
disappears" symptom returns.

Note: task stacks for the RTSP (24KB), decode (8KB), RTP RX (4KB) and search
(3KB) threads live in internal RAM (~39 KB of `.bss`), not PSRAM, because PSRAM
stacks are unsafe here. This is a fixed cost, not per-session heap.

## Credits and Acknowledgments

This project builds upon and incorporates code from:

### Squeezelite-ESP32
- **Project**: [https://github.com/sle118/squeezelite-esp32](https://github.com/sle118/squeezelite-esp32)
- **Contributors**: Philippe G., Sebastien, and contributors
- **Used for**: RTP packet handling, jitter buffer implementation, audio buffering architecture, and timing synchronization logic
- **License**: GPL v3 / MIT (components vary)

### Shairport-sync
- **Project**: [https://github.com/mikebrady/shairport-sync](https://github.com/mikebrady/shairport-sync)
- **Author**: Mike Brady and contributors
- **Used for**: RTSP protocol handling, RSA authentication with mbedtls v3, and AirPlay protocol reference
- **License**: GPL v2+

### ALAC Decoder
- Apple Lossless Audio Codec decoder
- **License**: Apache 2.0

Special thanks to the developers of these projects for their excellent work in reverse-engineering and implementing the AirPlay protocol.

## License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

This license was chosen to maintain compatibility with the GPL-licensed components from squeezelite-esp32 and shairport-sync.

## Technical Details

### Memory Usage
- **PSRAM**: ~2.7MB for audio buffers (RTP jitter buffer + ring buffer)
- **DRAM**: ~85KB static (`.data` + `.bss`); ~39KB of that is permanent task
  stacks. The rest of the internal-RAM budget is WiFi/lwIP runtime.
- **Flash**: ~900KB application binary

### Network Requirements
- 2.4GHz WiFi (ESP32 limitation)
- Multicast DNS (mDNS) support on your network
- UDP ports for RTP, control, and timing packets (dynamically allocated)

## Troubleshooting

### Device not appearing in AirPlay list
- Ensure device and client are on the same network
- Check that mDNS/Bonjour is not blocked by firewall
- Verify device has successfully connected to WiFi (check serial monitor)

### Audio dropouts or stuttering
- Check WiFi signal strength
- Check for interference from other 2.4GHz sources (microwaves, other wifi routers)
- Monitor for "Buffer full" or "Skipping late frame" warnings in logs

### Authentication failures
- RSA authentication errors indicate mbedtls issues
- Ensure ESP-IDF v5.5.1 is being used

## Development

### Project Structure
```
esp-airsync/
├── main/                   # Main application code
├── components/
│   ├── esp-raop-receiver/ # Full RAOP library
│   ├── i2s_output/        # I²S DAC interface
├── test/                   # Host-buildable unit tests (not part of idf.py build)
└── partitions.csv         # Flash partition table
```

### Running the Host Unit Tests
```bash
make -C test && ./test/test_raop_volume
```

`test/test_raop_volume.c` verifies the AirPlay→dBFS volume transfer function
(`components/esp-raop-receiver/src/raop_volume.c`): endpoints, monotonicity,
continuity, the −144 dB mute sentinel, headroom below full scale, and the
non-linear taper. It builds with a plain host compiler.

### Building with Debug Symbols
```bash
idf.py menuconfig
# Compiler options → Optimization Level → Debug (-Og)
idf.py build
```

## Contributing

Contributions are welcome! Please:
1. Fork the repository on GitHub
2. Create a feature branch
3. Make your changes with clear commit messages
4. Submit a pull request

## Known Limitations

- AirPlay 1 only (AirPlay 2 not supported)
- No video support
- 2.4GHz WiFi only (ESP32 hardware limitation)

## Future Enhancements

- [ ] Additional DAC support (I²S variants)
- [ ] Additional board variant builds
- [ ] Additional WLED sync customization (band scaling, sensitivity)

## Support

For issues, questions, or contributions, please use the GitHub issue tracker.

---

**Note**: AirPlay is a trademark of Apple Inc. This project is an independent implementation and is not affiliated with or endorsed by Apple Inc.