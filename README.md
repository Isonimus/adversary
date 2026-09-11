# The Adversary

<p align="center">
  <strong>Advanced WiFi Security Testing Tool</strong>
</p>

<p align="center">
  <a href="https://github.com/Isonimus/adversary/actions/workflows/ci.yml"><img src="https://github.com/Isonimus/adversary/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/Isonimus/adversary/actions/workflows/codeql.yml"><img src="https://github.com/Isonimus/adversary/actions/workflows/codeql.yml/badge.svg" alt="CodeQL"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/status-alpha-orange" alt="Status: alpha">
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#hardware">Hardware</a> •
  <a href="#installation">Installation</a> •
  <a href="#usage">Usage</a> •
  <a href="#faq">FAQ</a> •
  <a href="#development">Development</a> •
  <a href="DISCLAIMER.md">Legal</a>
</p>

> [!WARNING]
> **Adversary is currently in ALPHA.** Expect frequent crashes, incomplete features, and breaking changes. Use at your own risk.

---

## ⚖️ Legal Disclaimer

Adversary is for **educational and research purposes only.** Unauthorized access to networks is illegal. By using this tool, you agree to the terms in the [DISCLAIMER](DISCLAIMER.md).

---

## 🔍 Features

The main menu is a carousel: **Wireless**, **BLE**, **Infrared**, **RFID**, **HID**,
and **Radio** (Sub-GHz / 2.4 GHz, shown when the multi-radio cap is attached), plus
**Modules** (live peripheral inventory + hot-swap re-scan), **Server** (dashboard),
**Settings**, and **About**.

### 📡 Wireless (WiFi)
- **Scanner** - Discover networks with security type, signal strength, and channel info; context action menu per network.
- **Packet Sniffer** - Raw 802.11 frame capture with channel hopping and filtering.
- **Handshake Capture** - WPA/WPA2/WPA3 handshake acquisition (4-way, PMKID, partial EAPOL), with Auto Hunt (autonomous multi-target hunting + whitelist) and manual modes.
- **Deauthentication** - Targeted or mass 802.11 deauth frames.
- **Evil Twin** - Rogue AP with captive-portal credential harvesting.
- **Karma AP** - Auto-responds to any client probe request.
- **Beacon Spam / Probe Flood** - Fake-network flooding and probe-request saturation.
- **Wardriving** - GPS-integrated network mapping with CSV/KML export and **WiGLE** upload.
- **Traffic Proxy** - Real-time DNS/HTTP/HTTPS metadata capture from Evil Twin victims.
- **Captures Browser** - Manage Handshakes/Packets/Credentials, with cloud-cracking status and a bulk **"Sync all new"** action across every configured cracking service.

### 🔵 BLE (Bluetooth Low Energy)
- **BLE Scanner** - Real-time discovery and RSSI tracking of BLE peripherals.
- **Apple Attack** - Proximity-pairing popup spam for Apple devices.
- **BadBLE** - BLE HID keyboard emulation, including a **DuckyScript interpreter** for scripted keystroke injection over BLE.
- **Identity Spoof** - BLE identity/address spoofing.
- **BLE Spam** - Proximity pairing notification flooding (Apple, Android, Windows).

### 🔌 HID (USB keystroke/mouse injection)
- **BadUSB** - Native USB-HID DuckyScript keyboard injection on the Cardputer (ESP32-S3 USB-OTG); built-in scripts plus SD-loaded custom scripts, with US/ES keyboard layout support.
- **Mouse Jiggler** - Periodic small mouse movements to defeat idle-lock/screensaver timers.
- Not available on M5StickC Plus2 (no native USB-OTG).

### 📻 Radio — Sub-GHz & 2.4 GHz (multi-radio cap)
Requires the CC1101/NRF24 expansion cap; the **Radio** entry is greyed out when no cap is detected.
- **Sub-GHz Band Sweep** - CC1101 RSSI read across the 315 / 433.92 / 868.35 / 915 MHz presets while you hold a remote's button, with a peak-held bar per band and the strongest highlighted — tells you *which band* an unknown remote transmits on before you try to capture it. Reads energy, not modulation (independent of OOK vs FSK).
- **Sub-GHz OOK Capture & Replay** - CC1101-based raw OOK capture and replay across 315 / 433.92 / 868.35 / 915 MHz (e.g. simple remotes and sensors), with a pulse-train preview of the captured signal.
- **Sub-GHz FSK Capture & Replay** - CC1101 FSK demodulated capture (2-FSK / GFSK / MSK) for constant-amplitude remotes the OOK envelope-slicer can't see, chosen from a table of common modem presets (deviation / data rate / RX bandwidth), stored so a captured signal replays under its own configuration. Constant-amplitude modulation carries no envelope, so unknown deviation/data-rate is a preset search, not a read — and rolling-code targets won't actuate on replay (this is not an SDR).
- **Sub-GHz Jamming** - CC1101 carrier-wave or modulated-noise transmission on the selected band preset to hold a channel busy against a fixed-code receiver. Hold-to-jam (emits only while the key is held) behind an interference-warning gate — authorized testing only.
- **2.4 GHz Spectrum Analyzer** - NRF24-based channel sweep (received-power detection) across the 2.4 GHz band for activity/interference mapping.

### 📟 Infrared
- **IR TV-B-Gone** - High-power universal IR blast to turn off TVs (NA/EU code databases, arrow-key region toggle).
- **IR Record & Replay** - Learn a remote's IR frame (NEC/RC5/raw — decoded or raw timing train) via the cap's IR receiver, persist it to SD, and replay it, with a pulse preview of the capture.

### 🔖 RFID
- **RFID Audit** - MFRC522-based (13.56 MHz, I2C) NFC/RFID tag reading and dumping.

### 🧩 Modules
- **Module inventory & hot-swap re-scan** - A live view of which peripherals are attached right now (Sub-GHz/2.4 GHz cap, RFID reader, GPS) with a **Re-scan** action, so a module seated after boot is detected without a reboot — its greyed-out menu tile un-greys as soon as the re-scan finds it. Also surfaces the cap-detection override. Seat the module while the radio bus is idle, then Re-scan.

### ☁️ Cloud Cracking & Sync
- **WPA-SEC** - Upload `.pcap` handshakes for cloud cracking; status tracking (uploaded/cracked) per capture.
- **pwncrack.org** - Second cracking service, consumes the `.hc22000` (`.22000`) files handshake capture already writes.
- **WiGLE** - Upload wardriving CSVs for community wireless mapping.
- All uploads go over a certificate-validated TLS connection (Mozilla root-CA bundle, no `setInsecure()`), heap-paced to avoid OOM on large uploads.

### 🌐 Server / Dashboard Mode
- Serves a web dashboard from the SD card for browser-based review of captures and status, with path-traversal protection and auth enabled by default.

### 🎨 User Interface
- **Theme System** - Customizable color schemes.
- **Toast Notifications** - Real-time operation feedback.
- **Status Bar** - Battery, SD card, WiFi status, and connected module display.
- **Footer Hints** - Context-sensitive key mapping guides.

---

## Hardware

### Supported Devices

| Device | Status | Notes |
|--------|--------|-------|
| M5Stack Cardputer (ESP32-S3) | ✅ Primary | Full keyboard, SD card slot, 240x135 display, native USB-OTG (BadUSB/Mouse Jiggler) |
| M5StickC Plus2 (ESP32-PICO-D4) | ⚠️ Secondary | Builds and is exercised in CI; active development and hardware testing target the Cardputer. No USB-OTG, so BadUSB/Mouse Jiggler are unavailable there. |

> [!NOTE]
> The architecture is designed to support both boards via the HAL, but active development and hardware testing target the Cardputer.

### Requirements

- **SD Card**: 8GB+ Class 10 (FAT32 formatted)
- **Battery**: Fully charged for portable operation
- **USB-C Cable**: For flashing and serial monitoring

### Optional external modules

Most features run on a bare Cardputer. These need extra hardware:

| Feature | Requires | Attaches via |
|---------|----------|--------------|
| Radio — Sub-GHz OOK capture/replay, 2.4 GHz analyzer | CC1101 + NRF24 multi-radio expansion cap | Top expansion header |
| IR Record & Replay | multi-radio cap (IR receiver) — the built-in Cardputer IR emitter is transmit-only | Top expansion header |
| Wardriving GPS coordinates | M5Stack GNSS/LoRa cap (onboard GPS), or a Grove-port GPS module (AT6668/ATGM336H-class NMEA) | Expansion header, or Grove port (UART) |
| RFID Audit | MFRC522 NFC/RFID reader (13.56 MHz, I2C) | Grove port (I2C) |
| BadUSB / Mouse Jiggler | none — native USB-OTG | Cardputer only (no external module) |

> [!IMPORTANT]
> **GPS source, and the RFID conflict.** GPS works either from the M5Stack **GNSS/LoRa cap**
> (its onboard GPS — the cap's LoRa radio itself is not supported) or from a **Grove-port GPS
> module**; the cap path takes priority when both are present. Either way, when a GPS module is
> detected the **RFID reader is disabled** for that detection pass — and a Grove-port GPS
> additionally shares the reader's Grove pins (G1/G2). Modules attached after boot are picked up
> by the **Modules → Re-scan** action (no reboot needed); seat them while the radio bus is idle.

Everything else — WiFi, BLE, IR TV-B-Gone (built-in emitter), HID, Server — needs no
external hardware.

---

## Installation

The fastest path is the **[web flasher](https://isonimus.github.io/adversary/)** — flash
from the browser, no tools. Or download a **prebuilt release** and flash it yourself.
Building from source (Option C) is only needed for development or an unreleased change.

### Option A — One-click web flasher (Cardputer)

Open the **[web flasher](https://isonimus.github.io/adversary/)** in desktop Chrome, Edge,
or Opera, connect the Cardputer over USB-C, and click **Connect & Flash**. It writes the
latest release's factory image over Web Serial — no PlatformIO, no `esptool`, no clone.

> [!NOTE]
> Web Serial is Chromium-only (Chrome/Edge/Opera on desktop). On Firefox, Safari, or
> mobile, use Option B or C.

### Option B — Flash a prebuilt release (Cardputer)

Every version tag publishes ready-to-flash binaries on the
[Releases page](https://github.com/Isonimus/adversary/releases). Download the assets for
the latest tag:

| Asset | Use it for |
|-------|-----------|
| `adversary-cardputer-<tag>-factory.bin` | A **fresh flash** of a blank or bricked device — a complete image (bootloader + partitions + app) written to offset `0x0`. Use with an ESP web flasher, **M5Burner** ("User Custom"), or `esptool`. |
| `adversary-cardputer-<tag>-app.bin` | An **over-the-air, app-only** install from **M5Launcher** (bmorcelli) via a direct URL — writes just the app to the OTA slot. |
| `SHA256SUMS.txt` | Verify the download before flashing: `sha256sum -c SHA256SUMS.txt`. |

**With `esptool`** (factory image, ESP32-S3):

```bash
esptool --chip esp32s3 write-flash 0x0 adversary-cardputer-<tag>-factory.bin
```

**With M5Burner**: add the `-factory.bin` as a *User Custom* firmware, then burn it to the
device.

**With M5Launcher**: point it at the `-app.bin` release URL to install over OTA.

> [!NOTE]
> Prebuilt binaries are published for the **Cardputer** only. For the M5StickC Plus2, build
> from source (Option C).

### Option C — Build from source

#### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-C cable
- SD Card (FAT32 formatted)

#### Build & Flash

```bash
# Clone the repository
git clone https://github.com/Isonimus/adversary.git
cd adversary

# Build for Cardputer
pio run -e cardputer

# Flash to device
pio run -e cardputer -t upload

# Monitor serial output (optional)
pio device monitor -b 115200
```

#### For M5StickC Plus2

```bash
pio run -e m5stick -t upload
```

### First Boot

1. Insert FAT32-formatted SD card
2. Power on device
3. Wait for splash screen initialization
4. Settings will be created automatically at `/adversary/config/settings.json`

---

## Usage

### Navigation

- **ESC** (`` ` `` key): Access menu / Cancel operation / Back
- **ENTER**: Select / Confirm
- **↑/↓** or **;/.**: Navigate lists
- **Number keys** (1-9): Quick menu selection
- **Fn + S**: Screenshot — saves the current screen as a BMP to `/adversary/screenshots/` (any screen)

### Menu Structure

The main screen is a carousel; press ENTER on a section to open its submenu.

```
Main Carousel
├── Wireless
│   ├── Scan Networks      - WiFi reconnaissance
│   ├── Packet Sniffer     - Raw frame capture
│   ├── Deauth Attack      - Client disconnection
│   ├── Beacon Spam        - Fake network flooding
│   ├── Probe Flood        - Probe request saturation
│   ├── Handshake Capture  - WPA handshake acquisition (Manual / Auto Hunt)
│   ├── Evil Twin          - Rogue AP + credential capture
│   ├── Karma AP           - Probe request auto-response
│   ├── Wardriving         - GPS network mapping (requires GPS module)
│   ├── Captures           - View/manage Handshakes/Packets/Credentials
│   └── Whitelist          - Auto Hunt exclusions
├── BLE
│   ├── BLE Scanner        - Peripheral discovery
│   ├── Apple Attack       - Proximity-pairing popup spam
│   ├── BadBLE (HID)       - BLE keyboard emulation + DuckyScript
│   ├── Identity Spoof     - BLE address/identity spoofing
│   └── BLE Spam           - Pairing notification flooding
├── Infrared
│   ├── TV-B-Gone          - Universal IR power-off blast
│   └── Record / Replay    - Learn & replay an IR remote (requires cap IR RX)
├── RFID
│   └── RFID Dashboard     - NFC/RFID tag reading (requires MFRC522 I2C reader)
├── HID
│   ├── BadUSB (HID)       - USB keystroke injection (Cardputer only)
│   └── Mouse Jiggler      - Anti-idle-lock mouse movement (Cardputer only)
├── Radio                  - Sub-GHz OOK capture/replay + 2.4 GHz analyzer (requires multi-radio cap)
├── Server                 - Web dashboard (served from SD card)
├── Settings               - Configuration options
└── About
```

### Screen-by-Screen Guide

#### Scanner

- **Navigate**: `;`/`.` to scroll through networks
- **Sort**: `s` key to cycle (Signal → Channel → SSID)
- **Select**: `ENTER` for action menu
- **Actions**: Deauth, Handshake, Evil Twin, Probe Flood, Info
- **Exit**: `` ` `` (ESC) to return to menu

#### Handshake Capture

**Manual Mode:**
1. Select target from scanner or configure manually
2. Set options: Auto Deauth, Deauth Count, Timeout
3. Press `ENTER` to start capture
4. Wait for SUCCESS or TIMEOUT
5. Handshake auto-saves to SD card

**Auto Hunt Mode:**
1. Toggle Auto Hunt in config screen
2. Press `ENTER` to start autonomous hunting
3. Device scans, targets, and captures automatically
4. Press `` ` `` to stop at any time
5. Press `h` to view handshake list during hunt
6. Press `w` to whitelist current target

**Handshake List (during Auto Hunt):**
- Press `h` to enter list view
- Navigate with `;`/`.` keys
- Press `d` to delete selected handshake
- Confirm with `y` or cancel with `n`
- Type badges show: `[4WAY]`, `[PMKID]`, or `[EAPOL]`
- Press `` ` `` to return to hunt

#### Captures Browser

- **Navigate categories**: Handshakes / Packets / Credentials
- **Browse files**: `;`/`.` to scroll
- **Type badges**: Visual indicators for handshake types
- **WPA-SEC status**: Colored dots (Red=Not uploaded, Orange=Uploaded, Green=Cracked)
- **View details**: `ENTER` on file
- **Delete**: `d` key (with confirmation)

#### Settings

- **WiFi Configuration**: Connect device to internet for WPA-SEC
- **WPA-SEC API Key**: Enter your API key for cloud cracking
- **Theme Selection**: Choose color scheme
- **Auto Hunt Options**: Configure autonomous capture behavior
- **Whitelist Management**: View/edit excluded networks
- **System Info**: Firmware version, storage, battery

### Cracking Service API Keys (WPA-SEC / pwncrack / WiGLE)

Each cloud service (WPA-SEC handshake cracking, pwncrack.org handshake
cracking, WiGLE wardriving upload) needs its own API key, set up either way:

1. **On-device entry** — Settings → tap the service's key field (shows
   "Not Set" until configured) → type the key on the physical keyboard.
2. **SD-card auto-import** (no typing required) — register for the service,
   then drop a plain-text file containing just the key at:
   - `/adversary/config/wpasec.txt`
   - `/adversary/config/pwncrack.txt`
   - `/adversary/config/wigle.txt`

   On the next boot the device imports the key into `settings.json` and
   **deletes the file** (so a plaintext key doesn't linger on the card).

Registration:
- WPA-SEC: [wpa-sec.stanev.org](https://wpa-sec.stanev.org)
- pwncrack: [pwncrack.org](https://pwncrack.org)
- WiGLE: [wigle.net](https://wigle.net)

Once a key is set, Captures → **"Sync all new"** uploads every not-yet-synced
handshake to every service that has a key configured, and status (per
capture, per service) shows as colored dots in the Captures screen.

### SD Card Structure

```
/adversary/
├── config/
│   ├── settings.json           - User preferences (incl. imported API keys)
│   ├── whitelist.json           - Auto Hunt exclusions
│   ├── wpasec.txt                - (optional, one-time) WPA-SEC key import
│   ├── pwncrack.txt              - (optional, one-time) pwncrack key import
│   └── wigle.txt                 - (optional, one-time) WiGLE key import
├── badusb/
│   └── *.txt                    - Custom DuckyScript files (BadUSB/BadBLE)
├── captures/
│   ├── handshakes/
│   │   └── *.pcap, *.22000       - Captured handshakes (pcap + hashcat format)
│   ├── packets/
│   │   └── *.pcap                - Raw packet captures
│   └── credentials/
│       └── *.json                - Evil Twin credentials
├── dashboard/
│   └── index.html, ...           - Web dashboard (Server mode); deploy with
│                                    scripts/deploy_dashboard.sh (see Development)
├── screenshots/
│   └── shot_NNN.bmp              - Screen captures (Fn + S)
└── logs/
    └── system.log                 - Debug/activity logs
```

---

## FAQ

**Q: What's the difference between Auto Hunt and Manual mode?**  
A: Manual mode targets a single network until success/timeout. Auto Hunt autonomously scans, selects targets, captures handshakes, and moves to the next target indefinitely.

**Q: How do I use WPA-SEC integration?**  
A: Register at wpa-sec.stanev.org, then either type your API key into Settings on-device, or drop it as a text file at `/adversary/config/wpasec.txt` on the SD card — the device imports it on next boot and deletes the file. See [Cracking Service API Keys](#cracking-service-api-keys-wpa-sec--pwncrack--wigle) above.

**Q: What do the type badges mean (4WAY/PMKID/EAPOL)?**  
A: `[4WAY]` = Complete 4-way handshake (most reliable), `[EAPOL]` = Partial handshake (2-3 messages, still crackable), `[PMKID]` = PMKID-based capture (efficient, WPA2 only).

**Q: How do I add networks to the whitelist?**  
A: During Auto Hunt, press `w` on any target to whitelist it. Or manage manually in Settings → Whitelist Management.

**Q: What's the difference between Karma AP and Evil Twin?**  
A: Karma responds to ANY probe request (useful for reconnaissance). Evil Twin impersonates a SPECIFIC network to capture credentials via captive portal.

**Q: Why isn't my handshake cracking?**  
A: Ensure it's a `[4WAY]` or `[EAPOL]` type. Check WPA-SEC upload status in Captures screen. Weak passwords may take time; dictionary quality matters.

---

## Troubleshooting

**SD Card Not Recognized**
- Ensure FAT32 format (not exFAT or NTFS)
- Try a different/smaller card (8-32GB recommended)
- Check for physical damage to card or slot

**Handshake Capture Failures**
- Verify target is WPA/WPA2 (not WPA3-only or Open)
- Enable Auto Deauth if no clients reconnecting
- Increase timeout if network has few clients
- Check signal strength (closer to AP helps)

**WPA-SEC API Issues**
- Verify API key is correct (check for typos)
- Ensure WiFi is connected for uploads
- Check internet connectivity
- Per-capture upload/crack status lives in `/adversary/captures/handshakes/manifest.bin`; if status looks wrong, Settings → Rebuild Index resyncs it against the files on the card

**Theme Not Persisting**
- SD card may be full or corrupted
- Settings file may have write errors
- Check `/adversary/config/settings.json` exists

**Serial Output for Debugging**
```bash
pio device monitor -b 115200
```
Look for error messages, crash logs, or module initialization failures.

---

## Development

### Project Structure

```
/adversary
├── src/
│   ├── config/         - Configuration headers
│   ├── core/           - State machine, events
│   ├── hal/            - Hardware abstraction
│   ├── modules/        - Feature modules
│   │   ├── attack/     - Attack implementations
│   │   ├── capture/    - Handshake capture logic
│   │   ├── network/    - WPA-SEC, WiFi connection
│   │   ├── pcap/       - PCAP file generation
│   │   ├── sniffer/    - Packet capture
│   │   └── storage/    - Settings, registry
│   ├── ui/             - User interface
│   │   ├── screens/    - Screen implementations
│   │   ├── components/ - Reusable UI components
│   │   └── theme.h     - Color schemes
│   └── utils/          - Utility functions
├── test/               - Unit tests
│   ├── common/         - Test mocks (Arduino, ESP32)
│   └── test_*/         - Individual test suites
├── adr/                - Architecture decision records
└── slices/             - Feature slice specs (decision records)
```

### Running Tests

```bash
# Run all native tests
pio test -e native

# Run specific test
pio test -e native -f test_handshake_capture

# View test coverage
pio test -e native --verbose
```

### Deploying the dashboard

The web dashboard is served directly off the SD card from `/adversary/dashboard/`.
Its source lives in `dashboard_dev/`, and nothing syncs the two automatically —
editing `dashboard_dev/` has no effect on the device until the files are copied
onto the card. Pop the SD into a card reader and run:

```bash
# <sd_mount_path> is where the card is mounted, e.g. /media/$USER/CARDPUTER
scripts/deploy_dashboard.sh <sd_mount_path>
```

The script copies every asset, verifies each file's size on the card matches the
source (a truncated or 0-byte write renders as a blank dashboard), and flushes
the card before reporting success.

### Code Standards

- **Language**: C++17
- **Framework**: Arduino (ESP32)
- **Build System**: PlatformIO
- **Testing**: Unity test framework
- **Style**: See `CLAUDE.md` for guidelines

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for setup, coding standards, and the PR
workflow, and [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for community expectations.

---

## Documentation

- [CLAUDE.md](CLAUDE.md) - Architecture and development guidelines
- [CONTRIBUTING.md](CONTRIBUTING.md) - How to build, test, and submit changes
- [SECURITY.md](SECURITY.md) - How to report a firmware vulnerability
- [CHANGELOG.md](CHANGELOG.md) - Notable shipped changes (historical)
- [adr/](adr/) &amp; [slices/](slices/) - Architecture decisions and feature slices (decision records)

---

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) for details.

---

## Acknowledgments

- Inspired by ESP32 WiFi security research community
- M5Stack for excellent hardware platforms
- WPA-SEC for handshake cracking infrastructure
- ESP32 and Arduino communities

---
