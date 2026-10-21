# Flipper Zero — Official-Source Technical Research Dossier

**Compiled:** October 20, 2026
**Scope rule:** Every fact below comes from official Flipper Devices sources only — docs.flipper.net, developer.flipper.net (part of the flipper.net family; it hosts the official Doxygen developer documentation that docs.flipper.net and the firmware repo link to), flipper.net, and github.com/flipperdevices. No forums, Reddit, YouTube, unofficial wikis, or custom-firmware sites were used. Anything needed but not found in official sources is listed at the end under "Gaps not covered by official sources" rather than sourced elsewhere. Each claim is followed by its source URL.

---

## Executive summary

Flipper Zero is an unusually open platform by official design: the firmware is GPL-3.0 open source, the hardware schematics and blueprints are published in the official docs, and the device is built around an STM32WB55RG (ARM Cortex-M4) running a FreeRTOS-based firmware with Flipper's own Furi abstraction layer. Source: https://github.com/flipperdevices/flipperzero-firmware ; https://docs.flipper.net/zero/development/hardware

External control is a first-class, officially documented feature, and it is the strongest part of the platform for a "drivable device" project. The device exposes a protobuf-based RPC system (definitions published in the official flipperzero-protobuf repo) over its USB serial interface — the text CLI and the RPC session share the port, and the CLI command `start_rpc_session` switches the port into protobuf mode. RPC covers full storage access (read/write/list/delete/backup/tar), application launching and data exchange with running apps, GUI screen-frame streaming plus input-event injection, GPIO pin control, and system operations (device info, reboot into OS/DFU/update modes, firmware update, factory reset). The official mobile apps additionally control and update the device over Bluetooth LE. Source: https://docs.flipper.net/zero/development/cli ; https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/flipper.proto ; https://flipper.net/products/flipper-zero

Third-party apps are FAPs (Flipper Application Packages): relocatable ELF-based executables with bundled metadata, loaded from the microSD card into RAM by the firmware's App Loader, and bound to the firmware only through a semantically versioned API (an exported symbol table), so a FAP is not tied to one exact firmware build — only to a matching major API version. The official uFBT tool reduces app building to a pip install plus one command, downloading its SDK from the official firmware update channels. Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html ; https://github.com/flipperdevices/flipperzero-ufbt

BadUSB is scriptable in an extended DuckyScript dialect (plain .txt files, no compilation) with custom USB VID/PID spoofing, Bluetooth HID mode with custom MAC/name, mouse control, media keys, and macros. The ESP32-S2 Wi-Fi developer board is officially a Black Magic Probe / CMSIS-DAP debug probe (firmware source published by Flipper Devices) that plugs into the GPIO header and enables flashing and full GDB-style debugging of the Flipper over USB or Wi-Fi. Sub-GHz transmission is region-gated by design: transmit stays disabled until the first firmware update (via the mobile app or qFlipper) provisions the device's region, after which only locally permitted civilian bands are unlocked. Source: https://developer.flipper.net/flipperzero/doxygen/badusb_file_format.html ; https://developer.flipper.net/flipperzero/doxygen/dev_board.html ; https://docs.flipper.net/zero/sub-ghz/frequencies

---

## 1. Hardware

### Main processor and system

- **MCU:** STM32WB55RG, ARM Cortex-M4. Source: https://flipper.net/products/flipper-zero
- The developer docs describe the STM32WB55 as a microcontroller with **two cores**, RAM, flash, and several dozen peripheral blocks (timers, RTC, BLE, GPIO, etc.). The firmware repo's `lib/stm32wb_copro` holds the "WPAN and radio co-processor firmware," i.e. the BLE radio stack runs on the second core. Source: https://developer.flipper.net/flipperzero/doxygen/app_dev_furi_api.html ; https://github.com/flipperdevices/flipperzero-firmware/tree/dev/lib
- **Display:** monochrome LCD, 128×64 px. Source: https://flipper.net/products/flipper-zero
- **Battery:** 2100 mAh, up to 28 days of operation. Source: https://flipper.net/products/flipper-zero
- **Storage:** microSD up to 256 GB, connected in SPI mode. A microSD card is required to use the device and is not included. Source: https://flipper.net/products/flipper-zero
- **Dimensions / weight:** 100 × 40 × 25 mm, 102 g. Source: https://flipper.net/products/flipper-zero
- Flipper Devices describes the device as "fully open-source and customizable." Source: https://flipper.net/products/flipper-zero

### Radios

- **Bluetooth:** Bluetooth Low Energy 5.4 (radio integrated in the STM32WB55). Full BLE support lets the Flipper act as a peripheral for smartphones and third-party devices. Source: https://flipper.net/products/flipper-zero
- **Sub-GHz:** built-in sub-1 GHz module based on the **TI CC1101** transceiver with an integrated antenna. The CC1101 chip and antenna are designed to operate in the **300–348 MHz, 387–464 MHz, and 779–928 MHz** bands; the docs state a maximum range of 50 meters. The product page summarizes supported frequencies as 315–900 MHz. The Sub-GHz app can also drive external CC1101 modules (internal/external antenna switchable in Radio Settings). Source: https://docs.flipper.net/zero/sub-ghz ; https://flipper.net/products/flipper-zero
- **NFC (13.56 MHz):** read NFC cards, save to SD card, write (rewritable cards only), and emulate: NXP MIFARE Classic, Ultralight, DESFire (etc.), FeliCa, HID iClass (PicoPass), and NFC Forum protocols. Source: https://flipper.net/products/flipper-zero
- **RFID (125 kHz):** modulation AM/OOK, coding ASK/PSK. Supported card families: EM400x / EM410x / EM420x, HID, Indala, FDX (A & B), Pyramid, AWID, Viking, Jablotron, Paradox, PAC Stanley, Keri, Gallagher, NexWatch. Source: https://flipper.net/products/flipper-zero
- **Infrared:** IR receiver and transmitter with a built-in signal library and "guess" mode for TVs and ACs. Supported protocols: NEC family, Kaseikyo, RCA, RC5, RC6, Samsung, SIRC. Source: https://flipper.net/products/flipper-zero
- **iButton / 1-Wire:** read, save, write (rewritable keys only), and emulate 1-Wire keys. Supported: Dallas DS199x, DS1971, CYFRAL, Metakom, TM2004, RW1990. Source: https://flipper.net/products/flipper-zero

### GPIO header and expansion port

- **18 pins** on the top side: power-supply pins plus I/O pins. I/O pins connect to the STM32WB55 through 51 Ω resistors, are ESD-protected, and operate at **3.3 V** logic levels. Source: https://docs.flipper.net/zero/gpio-and-modules
- **Pin 9 (+3.3 V out):** enabled by default, maximum load 1.2 A. The microSD card is powered from the same rail, so pin 9 power is temporarily disabled during firmware updates and SD mounting. Source: https://docs.flipper.net/zero/gpio-and-modules
- **Pin 1 (+5 V out):** can be powered from the battery or USB; on battery it is off by default and is enabled via GPIO → "5V on GPIO"; maximum load 1.2 A. Source: https://docs.flipper.net/zero/gpio-and-modules
- **I/O limits:** each I/O pin can source up to 20 mA; total power consumption from I/O pins must not exceed 5 W or the battery may enter protection mode and shut the device down. Source: https://docs.flipper.net/zero/gpio-and-modules
- **User-controllable GPIO pins** (GPIO app "Manual Control", also the CLI/RPC-controllable set): PA7 (pin 2), PA6 (pin 3), PA4 (pin 4), PB3 (pin 5), PB2 (pin 6), PC3 (pin 7), PC1 (pin 15), PC0 (pin 16). Source: https://docs.flipper.net/zero/gpio-and-modules
- **Buses on the header:** the GPIO app provides a **USB-UART Bridge** mode (Flipper acts as a USB-to-serial-UART converter), and the device can be used as a USB-to-UART/SPI/I2C converter. The official Expansion Module Protocol assigns USART to pins 13 (TX) / 14 (RX) and LPUART to pins 15 (TX) / 16 (RX). SWD debugging uses pin 10 (SWCLK) and pin 12 (SWDIO), with ground on pins 8/11/18. Source: https://docs.flipper.net/zero/gpio-and-modules ; https://developer.flipper.net/flipperzero/doxygen/expansion_protocol.html ; https://developer.flipper.net/flipperzero/doxygen/app_dev_debugging_faps.html

### USB interface

- USB-C connection (a USB-A to USB-C cable is included). USB carries: the CLI/RPC serial interface at **230400 baud**, firmware update and file transfer via qFlipper, the DFU recovery bootloader, USB HID (BadUSB), and the USB-UART bridge mode. Source: https://docs.flipper.net/zero/development/cli ; https://flipper.net/products/flipper-zero ; https://docs.flipper.net/zero/basics/firmware-update/firmware-recovery

### Schematics availability

- The official documentation's Hardware section publishes, as downloads: **tech specs of Flipper Zero, schematics of Flipper Zero ("Downloadable schematics and PCB drawings"), blueprints of Flipper Zero (3D models and drawings), external-module blueprints, Wi-Fi Developer Board schematics, and ST-Link V3 Developer Board schematics/specs.** Source: https://docs.flipper.net/zero/development/hardware
- The Wi-Fi module schematic PDF is also linked directly from the official blackmagic-esp32-s2 repo README (hosted on cdn.flipperzero.one). Source: https://github.com/flipperdevices/blackmagic-esp32-s2

---

## 2. Firmware architecture

### RTOS base and software layers

- The firmware is built on **FreeRTOS**: the official firmware repo vendors the kernel source in `lib/FreeRTOS-Kernel` plus a `lib/FreeRTOS-glue` layer "to hold together FreeRTOS kernel and flipper firmware." Source: https://github.com/flipperdevices/flipperzero-firmware/tree/dev/lib
- Above the kernel sits **Furi** (**F**lipper **U**niversal **R**egistry **I**mplementation) — Flipper's own abstraction layer between bare-metal hardware and application code. Furi provides the API for hardware interaction plus thread management, dynamic memory allocation, and resource access control. It is split into **Furi Core** (OS-level primitives, in the repo's `furi/` directory) and **Furi HAL** (hardware abstraction, in `targets/`). Developers can also bypass Furi and write their own low-level drivers, since the firmware is open source. Source: https://developer.flipper.net/flipperzero/doxygen/app_dev_furi_api.html
- Notable bundled libraries in the firmware repo's `lib/`: `nanopb` (protobuf implementation for MCU — the RPC transport encoding), `mbedtls` (cryptography), `fatfs` (SD-card filesystem driver), `mjs` (JavaScript engine — backs the CLI `js` command), `nfc`, `subghz`, `lfrfid`, `ibutton`, `one_wire`, `infrared`, `u8g2` (GUI graphics), `update_util` (updater). Source: https://github.com/flipperdevices/flipperzero-firmware/tree/dev/lib

### Source, license, repo structure

- **Repository:** github.com/flipperdevices/flipperzero-firmware (default branch `dev`), licensed **GPL-3.0**, written primarily in C with some C++ and ARM assembly. Source: https://github.com/flipperdevices/flipperzero-firmware
- **Top-level structure:** `applications` (built-in apps/services), `applications_user` (user/third-party apps), `assets`, `documentation`, `furi` (Furi Core), `lib`, `site_scons`, `scripts`, `targets`. Source: https://github.com/flipperdevices/flipperzero-firmware
- **Build system:** the **Flipper Build Tool** (`./fbt`), an SCons-based build (SConstruct / firmware.scons / site_scons). `./fbt flash` flashes via an in-circuit debugger; `./fbt flash_usb` flashes over USB. Supported development hosts: Windows 10+, macOS 12+, Ubuntu 20.04+. Source: https://github.com/flipperdevices/flipperzero-firmware

### Release / update channels

- Three official update channels: **Development** (built with every commit, multiple times a day, may be unstable), **Release-Candidate** (QA-validated; a passing RC becomes the Release), and **Release** (stable, recommended). Source: https://docs.flipper.net/zero/basics/firmware-update
- Updates are delivered via the **Flipper Mobile App over Bluetooth** (about 10 minutes) or **qFlipper over USB**; per-version changelogs are published on the firmware repo's GitHub Releases page. Source: https://docs.flipper.net/zero/basics/firmware-update
- Firmware updates can also be triggered programmatically: the RPC `UpdateRequest` takes an update-manifest path and returns a detailed result code, and the CLI offers `update install <.fuf>` / `update backup` / `update restore`. Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/system.proto ; https://docs.flipper.net/zero/development/cli

### Recovery / DFU mechanism

- The device has a **built-in USB DFU bootloader that bypasses the device OS**, so a broken firmware install does not brick it. Recovery mode: Settings → Power → Reboot → Firmware Upgrade (press RIGHT), or hold LEFT+BACK for 5 s, release BACK, keep holding LEFT until the blue LED lights. On a bricked/unresponsive device: unplug USB, hold OK+BACK for 30 s (hard reboot; a blank screen is normal), after which the PC detects the device as "DFU in FS Mode." Source: https://docs.flipper.net/zero/basics/firmware-update/firmware-recovery
- **qFlipper** auto-detects a device in DFU mode and offers **Repair**, which performs a complete firmware installation; alternatively "Install from file" accepts a full image `flipper-z-f7-full-[firmware_version].dfu` from the official update server (update.flipperzero.one). Source: https://docs.flipper.net/zero/basics/firmware-update/firmware-recovery
- RPC can reboot the device directly into DFU or update mode: `RebootRequest` modes are OS (0), DFU (1), UPDATE (2). Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/system.proto

---

## 3. External control — the RPC system, CLI, and host tools

This is the core of any "drive the Flipper from a computer" project, and it is fully official and documented.

### 3.1 Transports

- **USB serial (primary):** the Flipper presents a serial interface at **230400 baud**. It can be reached three documented ways: **Flipper Lab** (lab.flipper.net, in-browser via Web Serial), a generic **web serial terminal**, or a desktop **serial terminal / PuTTY** — all at 230400 baud. The same port carries both the text CLI and RPC (see below). Source: https://docs.flipper.net/zero/development/cli
- **Bluetooth LE:** the official iOS/Android **Flipper Mobile App** connects over BLE; Flipper Devices states the mobile apps "let you update your Flipper Zero via BLE, remotely control the device, share keys and manage data." The Android app (official repo, MIT license) is architected around a `components/bridge` module — "Communication between android and Flipper." Source: https://flipper.net/products/flipper-zero ; https://github.com/flipperdevices/Flipper-Android-App
- **Session switch:** on the serial port, the CLI command **`start_rpc_session`** — documented as "Activates the RPC session. Switches the CLI into protobuf mode" — converts the interactive CLI connection into a binary RPC session. Source: https://docs.flipper.net/zero/development/cli
- **On-device encoding:** the firmware implements protobuf with **nanopb** ("protobuf implementation for MCU," bundled in `lib/`). Source: https://github.com/flipperdevices/flipperzero-firmware/tree/dev/lib

### 3.2 The protobuf definitions

- **Repository:** github.com/flipperdevices/flipperzero-protobuf — "Flipper Zero RPC protocol protobuf description" (proto3). Files: `flipper.proto` (envelope), `system.proto`, `storage.proto`, `application.proto`, `gui.proto`, `gpio.proto`, `property.proto`, `desktop.proto`. Source: https://github.com/flipperdevices/flipperzero-protobuf
- **Envelope:** every message is a `PB.Main` with `command_id`, a `command_status` (a rich error enum: OK, ERROR, ERROR_BUSY, ERROR_INVALID_PARAMETERS, storage errors, app errors such as ERROR_APP_SYSTEM_LOCKED / ERROR_APP_NOT_RUNNING, virtual-display errors, GPIO errors, …), a `has_next` flag for multi-part responses, and a `oneof content` selecting the operation. Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/flipper.proto
- The developer docs define the term plainly: "RPC: Remote Procedure Call, a protobuf-based communication protocol widely used by Flipper Zero companion applications." Source: https://developer.flipper.net/flipperzero/doxygen/expansion_protocol.html
- **Protocol versioning:** RPC includes `ProtobufVersionRequest/Response` (major/minor) so a host can negotiate compatibility. Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/system.proto

### 3.3 What RPC exposes — operation inventory (from the .proto files)

**System (PB_System)** — `system.proto`:
Ping (echo bytes), Reboot (modes OS / DFU / UPDATE), DeviceInfo (key/value), FactoryReset, GetDateTime / SetDateTime, PlayAudiovisualAlert (beep + blink), ProtobufVersion, Update (by update-manifest path, with 11 distinct result codes covering manifest/stage/integrity/target errors), PowerInfo (key/value). Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/system.proto

**Storage (PB_Storage)** — `storage.proto`:
Info (total/free space for a path), Timestamp, Stat, List (with optional MD5 per file and a max-size filter), Read, Write, Delete (recursive flag), Mkdir, Md5sum, Rename, BackupCreate, BackupRestore, TarExtract. The `File` message carries type (FILE/DIR), name, size, data bytes, and md5sum — i.e. complete file-manager access to the device's internal storage and SD card. Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/storage.proto

**Applications (PB_App)** — `application.proto`:
StartRequest (app **name + args string**), LockStatus, AppExit, AppLoadFile (load a data file into the running app by path), AppButtonPress / AppButtonRelease / AppButtonPressRelease (args + index — virtual button actions inside the running app), GetError (numeric code + text), **DataExchange (arbitrary bytes delivered to the running app)**, and AppState notifications (APP_CLOSED / APP_STARTED). This is the mechanism by which a host program can launch a FAP and talk to it bidirectionally. Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/application.proto

**GUI (PB_Gui)** — `gui.proto`:
StartScreenStream / StopScreenStream, with `ScreenFrame` messages (raw frame bytes + orientation: horizontal, horizontal-flip, vertical, vertical-flip); **SendInputEvent** — keys UP / DOWN / RIGHT / LEFT / OK / BACK with types PRESS / RELEASE / SHORT / LONG / REPEAT; and a **Virtual Display**: StartVirtualDisplay (optionally with a first frame to show, and a flag to forward the Flipper's own input events to the host) / StopVirtualDisplay. In other words: full remote screen + full remote keypad + the ability to push your own UI onto the Flipper's display. Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/gui.proto

**GPIO (PB_Gpio)** — `gpio.proto`:
Pins PC0, PC1, PC3, PB2, PB3, PA4, PA6, PA7 (the same 8 user GPIOs as the CLI/GPIO app). Operations: SetPinMode (OUTPUT/INPUT), SetInputPull (NO/UP/DOWN), GetPinMode, ReadPin, WritePin, and GetOtgMode / SetOtgMode (the 5 V pin-1 OTG power, OFF/ON). Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/gpio.proto

**Property & Desktop** — also wired into the envelope: Property Get (key/value system properties) and Desktop IsLocked / Unlock / Status. Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/flipper.proto

**Region data model:** `flipper.proto` also defines a `Region` message — a country code plus a list of bands, each with start/end frequency, **power_limit**, and **duty_cycle** — the data structure behind Sub-GHz region provisioning (see §7). Source: https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/flipper.proto

### 3.4 The CLI over USB serial

The CLI is the human-facing twin of RPC on the same port: an interactive shell (`>` prompt) with the following documented command set (grouped; full reference at the source). Source for this whole subsection: https://docs.flipper.net/zero/development/cli

- **System/device:** `info device` / `info power` / `info power_debug`, `date`, `uptime`, `free` / `free_blocks` (heap), `factory_reset` (internal storage; SD data is preserved), `power off` / `power reboot` / `power reboot2dfu` / `power 5v` / `power 3v3`, `sysctl debug`, `sysctl heap_track`, `update install <.fuf>` / `update backup <.tar>` / `update restore`, `start_rpc_session`, `exit`.
- **App control:** `loader list` / `loader open` / `loader info` / `loader close` / `loader signal` — enumerates and launches both internal apps and external FAPs by name, so any app on the device is script-launchable from the CLI as well as via RPC.
- **Input injection:** `input send <key> <type>` with keys up/down/left/right/back/ok and types press/release/short/long; `input dump` streams input events — the CLI equivalent of RPC's SendInputEvent.
- **Storage:** a full filesystem suite mirroring RPC storage — `storage info/format/list/tree/remove/read/read_chunks` (≤512-byte chunks) `/write/write_chunk/copy/rename/mkdir/md5/stat/timestamp/extract.tar`, with paths rooted at `/int` (internal) and `/ext` (SD card).
- **Radios:** `subghz chat` (two Flippers can chat over Sub-GHz from the CLI), `subghz tx/rx/rx_raw/decode_raw/tx_from_file` (valid TX/RX frequency ranges: 299999755–348000000, 386999938–464000000, 778999847–928000000 Hz; device 0 = internal CC1101 antenna, 1 = external CC1101 module; debug-mode-only `tx_carrier`/`rx_carrier`/`encrypt_keeloq`/`encrypt_raw`), `rfid read/write/emulate/raw_read/raw_emulate/raw_analyze` with named key types and data lengths (e.g. EM4100 5 bytes, H10301 3, HIDProx 6, HIDExt 12, FDX-A 5, FDX-B 11, AWID 9, Gallagher 8, Nexwatch 8, Indala26 4, Jablotron 5, Paradox 6, PAC/Stanley 4, Keri 4, Pyramid 4, Viking 4, GProxII 12, …), `ir rx / rx raw / tx / tx raw` (raw TX up to 512 samples; raw carrier frequency 10000–56000 Hz) / `ir decode` / `ir universal`, and an `nfc` sub-shell with dump/save for named protocols (14_3a, 14_3b, 14_4a, 14_4b, 15, felica, mfu, mfc, mfp, des, slix, st25), raw APDU-level send, `mfu info/rdbl/wrbl`, `nfc field`, `nfc emulate`, `nfc scanner`.
- **1-Wire / iButton:** `ikey read/emulate/write` (Dallas 8-byte, Cyfral 2-byte, Metakom 4-byte keys), `onewire search`.
- **GPIO/buses:** `gpio mode/set/read` (pins PA7, PA6, PA4, PB3, PB2, PC3, PC1, PC0), `i2c` bus scan.
- **Crypto:** `crypto` — secure-enclave AES-256-CBC encrypt/decrypt, `has_key`, `store_key` (non-reversible); key slots 1–100, slots 1–11 preloaded.
- **Peripherals/misc:** `led r/g/b/bl` (0–255), `vibro`, `buzzer freq/note`, `bt` (BLE HCI factory test), `log` (streams device logs; levels error/warn/info (default)/debug/trace), `js <path>` (runs a JavaScript file), `echo`, `neofetch`, `reload_ext_cmds`.
- The docs note that **some CLI commands are only available with Debug mode on** (Settings → System), e.g. the Sub-GHz carrier/encryption test commands and `power 3v3` on pin 9. Source: https://docs.flipper.net/zero/development/cli

### 3.5 qFlipper and Flipper Lab

- **qFlipper** is the official desktop companion (Qt, GPL-3.0; Windows/macOS/Linux): one-button firmware + supplemental-data updates, **repair of broken firmware**, **streaming the Flipper's display and controlling it remotely** (i.e. an RPC GUI/input client), installing firmware from `.dfu`, backup/restore of settings, progress and pairing data, automatic self-update, and a **command-line interface that exposes nearly all of the main application's functionality**. Its backend includes a protobuf-based communication-protocol plugin for talking to the device. Official builds are served from update.flipperzero.one. Source: https://github.com/flipperdevices/qFlipper
- **Flipper Lab** (lab.flipper.net) is the official web interface: it provides the in-browser CLI terminal over Web Serial and hosts the **Apps Catalog** from which FAPs are browsed/installed. Source: https://docs.flipper.net/zero/development/cli ; https://developer.flipper.net/flipperzero/doxygen/app_dev_furi_api.html

### 3.6 Expansion Module Protocol (GPIO-header modules)

- Flipper publishes a formal **Expansion Module Protocol** for third-party hardware on the GPIO header: a serial-based, byte-oriented, synchronous protocol with automatic module detection, baud-rate negotiation, frames of header (1 byte) + payload + XOR checksum, a 250 ms inactivity timeout, and **integration with the Flipper RPC protocol** — i.e. a GPIO module can become an RPC endpoint. UART choices: USART on pins 13/14 or LPUART on pins 15/16. Source: https://developer.flipper.net/flipperzero/doxygen/expansion_protocol.html

---

## 4. App development — FAP, API/SDK, uFBT

### 4.1 The FAP format

- A **FAP (Flipper Application Package)** is "essentially an `.elf` executable with extra metadata and resources bundled in." FAPs are built with fbt's `faps` target (deployable copies via `fap_dist`), and can bundle static and animated images as private assets (declared via the manifest's `fap_icon_assets` field). Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html
- An app becomes a FAP by living in `applications_user/`, having an **`application.fam`** manifest, and setting `apptype` to `FlipperAppType.EXTERNAL`. Build one app with `./fbt fap_{APPID}`; build-and-run over USB with `./fbt launch APPSRC=applications_user/path/to/app`; build only with `./fbt build APPSRC=...`. Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html

### 4.2 How apps are loaded and run

- The MCU **cannot execute code directly from external storage**: the firmware's **App Loader** loads the FAP from the SD card, **verifies its integrity and compatibility**, copies it to RAM, **processes relocations**, and resolves the app's imported symbols against the firmware's **symbol table** before starting it. Only code+data sections occupy RAM (the FAP file also carries load-time-only metadata). Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html
- Once on the device (copied to the SD card, installed from the Apps Catalog in Flipper Lab / the mobile app, or uploaded by the build tools), an app appears in the on-device Applications menu and can equally be launched from the CLI (`loader open`) or via RPC `App StartRequest`. Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html ; https://docs.flipper.net/zero/development/cli ; https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/application.proto

### 4.3 API surface and versioning

- External apps may only use a **defined subset of the firmware API**: the exported functions/variables are listed in **`api_symbols.csv`**, part of the firmware target definition in `targets/`. fbt generates the symbol table from it (built into the `fap_loader` app); at build time fbt also checks that all of a FAP's imported symbols exist — an app with missing symbols "won't be able to run on the device." Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html
- **Versioning is semantic and bound to the FAP's metadata, not to a firmware build:** "FAPs do not depend on being run on a specific firmware version. Compatibility is determined by the FAP's metadata, which includes the required API version." The App Loader checks that the app's **major** API version matches the firmware's. Major increments = breaking changes (removing a function/variable, changing a signature); minor increments = added features. Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html
- fbt **automates** API versioning: when firmware headers gathered from `SDK_HEADERS` change, the build stops, adjusts the API version, and asks the developer to mark each new `.csv` entry `+` (expose to FAPs) or `−` (withhold); the build refuses to proceed until all `?` entries are resolved. Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html

### 4.4 uFBT — the micro build tool

- **uFBT** (github.com/flipperdevices/flipperzero-ufbt, GPL-3.0) is Flipper's simplified build tool for app developers who don't want the full firmware tree: `python3 -m pip install --upgrade ufbt` (host Python ≥ 3.8 for bootstrap; it downloads its own Python and platform toolchain). Source: https://github.com/flipperdevices/flipperzero-ufbt
- **On first run it downloads the SDK components from the `release` update channel of the official firmware.** SDK management: `ufbt update [--channel=dev|rc|release] [--branch=...] [--index-url] [--url] [--local=<path>]` — so apps can be built against dev, release-candidate, or release APIs. State lives in `~/.ufbt` (override with `UFBT_HOME`); per-project `.env` files via `ufbt dotenv_create`. Source: https://github.com/flipperdevices/flipperzero-ufbt
- Workflow: run `ufbt` in the app directory (the one containing `application.fam`) → the `.fap` lands in `dist/`; **`ufbt launch`** uploads and starts the app on a USB-attached Flipper; `ufbt create APPID=<id>` scaffolds a new app (manifest + main source); `ufbt cli` opens the device CLI; `ufbt lint` / `ufbt format` run clang-format checks; `ufbt flash` (via SWD probe) / `ufbt flash_usb` flash the SDK-matched firmware — debugging requires running the firmware distributed with the current SDK. An official GitHub Action for uFBT builds also exists. Source: https://github.com/flipperdevices/flipperzero-ufbt

---

## 5. BadUSB

- Official documentation: the **BadUSB file format** page in the developer docs. Scripts are plain-text `.txt` files — **no compilation** — using an **extended DuckyScript syntax that is "compatible with classic USB Rubber Ducky 1.0 scripts,"** extended with custom USB ID, ALT+Numpad input, SYSRQ, and additional functional keys. Both `\n` and `\r\n` line endings are accepted. Source: https://developer.flipper.net/flipperzero/doxygen/badusb_file_format.html
- **Core commands:** `REM` (comment); `DELAY`, `DEFAULT_DELAY` / `DEFAULTDELAY`; `STRING`, `STRINGLN`; `STRING_DELAY` / `STRINGDELAY` and `DEFAULT_STRING_DELAY` (per-character typing delays); `REPEAT` (repeat previous command). Source: https://developer.flipper.net/flipperzero/doxygen/badusb_file_format.html
- **Keys and modifiers:** full special-key set (arrows, ENTER, DELETE, BACKSPACE, END, HOME, ESCAPE/ESC, INSERT, PAGEUP, PAGEDOWN, CAPSLOCK, NUMLOCK, SCROLLLOCK, PRINTSCREEN, BREAK, PAUSE, SPACE, TAB, MENU/APP, F1–F12); modifiers CTRL/CONTROL, SHIFT, ALT, GUI/WINDOWS, chainable with hyphens or spaces (e.g. `CTRL-ALT DELETE`); `HOLD` / `RELEASE` for holding up to 5 keys simultaneously. Source: https://developer.flipper.net/flipperzero/doxygen/badusb_file_format.html
- **Alternate input methods:** `ALTCHAR`, `ALTSTRING`, `ALTCODE` (Alt+Numpad code entry), `SYSRQ` (Linux magic SysRq), `GLOBE` (Mac Fn/Globe key). Source: https://developer.flipper.net/flipperzero/doxygen/badusb_file_format.html
- **Media/consumer keys:** `MEDIA` with POWER, REBOOT, SLEEP, LOGOFF, EXIT, HOME, BACK, FORWARD, REFRESH, SNAPSHOT, PLAY, PAUSE, PLAY_PAUSE, NEXT_TRACK, PREV_TRACK, STOP, EJECT, MUTE, VOLUME_UP, VOLUME_DOWN, FN, BRIGHT_UP, BRIGHT_DOWN. Source: https://developer.flipper.net/flipperzero/doxygen/badusb_file_format.html
- **Mouse control:** `LEFTCLICK`/`LEFT_CLICK`, `RIGHTCLICK`/`RIGHT_CLICK`, `MOUSEMOVE`/`MOUSE_MOVE x y`, `MOUSESCROLL`/`MOUSE_SCROLL`. Source: https://developer.flipper.net/flipperzero/doxygen/badusb_file_format.html
- **Identity spoofing and connection mode:** `ID <VID>:<PID> <manufacturer> <product>` sets a custom USB identity (USB mode); `BLE_ID` / `BT_ID <MAC> <name>` sets a custom Bluetooth identity (Bluetooth mode). The ID command must be the script's first line and **automatically switches the connection mode** — i.e. the same BadUSB system works as a USB HID keyboard/mouse or as a **Bluetooth HID** device. `WAIT_FOR_BUTTON_PRESS` pauses a script until the user presses a button (a physical-consent gate for payloads). Source: https://developer.flipper.net/flipperzero/doxygen/badusb_file_format.html

---

## 6. Wi-Fi developer board

### Hardware and connection

- The Wi-Fi Developer Board is based on an **ESP32-S2** MCU — specifically an **ESP32-S2-WROVER** module with built-in Wi-Fi. It has a USB Type-C connector for data and power, GPIO pins, and tactile switches, and it **plugs into Flipper Zero's GPIO header**. Source: https://developer.flipper.net/flipperzero/doxygen/dev_board.html
- Official schematics for the board are published in the docs' Hardware section ("Wi-Fi Developer Board schematics — Downloadable schematics and PCB drawings"). Source: https://docs.flipper.net/zero/development/hardware

### Official firmware

- The board's **custom firmware is provided by Flipper Devices and incorporates Black Magic Debug and CMSIS-DAP**; it is built with **ESP-IDF**. Its job: "flash and debug various MCUs (including Flipper Zero) via Wi-Fi or USB." Source: https://developer.flipper.net/flipperzero/doxygen/dev_board.html
- The firmware's source is published at **github.com/flipperdevices/blackmagic-esp32-s2** (GPL-3.0): "Black Magic Probe / DapLink for ESP32-S2 — WiFi/USB capable version of the famous BlackMagicProbe (or DapLink) debugger." It builds with ESP-IDF v4.4 (`idf.py build`, `idf.py -p <port> flash`) and includes a web interface (served at blackmagic.local) used, among other things, to inspect/switch the board's debug mode. Source: https://github.com/flipperdevices/blackmagic-esp32-s2
- **Updating the board's firmware:** put the board in bootloader mode (hold BOOT, tap RESET, release BOOT), connect USB-C, and run **`python3 -m ufbt devboard_flash`** (uFBT handles flashing the Wi-Fi board as well as Flipper apps/firmware). Source: https://developer.flipper.net/flipperzero/doxygen/dev_board_fw_update.html

### What it enables

- A real debug interface to the Flipper's MCU: **halt execution, set breakpoints, inspect variables and memory, and step through code** — over Wi-Fi or USB. In VS Code (with fbt-generated configs), the two attach profiles are **"Attach FW (blackmagic)"** (Wi-Fi or USB) and **"Attach FW (DAP)"** (USB only); the active mode is chosen in the dev board's web interface ("USB mode" field). Source: https://developer.flipper.net/flipperzero/doxygen/dev_board.html ; https://developer.flipper.net/flipperzero/doxygen/dev_board_debugging_guide.html
- The official guide set around the board covers: getting started, firmware update, USB connection, **Wi-Fi connection**, debugging via the dev board, dev-board debug modes, and **reading logs via the dev board**. Source: https://developer.flipper.net/flipperzero/doxygen/dev_board.html

---

## 7. Sub-GHz / RFID / NFC / iButton / IR — official capability summaries

### 7.1 Sub-GHz

- The Sub-GHz application can **read and decode** signals from known protocols (saving static-protocol remotes), **Read RAW** (record unknown protocols), save and **emulate** remotes, **Add Manually** (create a virtual remote for pairing), run a **Frequency Analyzer**, and switch between the internal antenna and an external CC1101 module. Receive coverage is the full 300–348 / 387–464 / 779–928 MHz operational range. Source: https://docs.flipper.net/zero/sub-ghz
- Use cases named by the docs: gates, barriers, radio locks, remote switches, wireless doorbells, smart lights. Source: https://docs.flipper.net/zero/sub-ghz
- From the CLI, Sub-GHz adds scriptable TX/RX, raw capture to file, file playback, raw decode, and a two-Flipper **chat** mode. Source: https://docs.flipper.net/zero/development/cli

### 7.2 Regional restrictions on Sub-GHz transmission (official statements)

- **"Flipper Zero transmits signals only at frequencies that are allowed for civilian use."** While it can *receive* across all operational bands, transmission is limited to the bands permitted in the device's provisioned region. Source: https://docs.flipper.net/zero/sub-ghz/frequencies
- **Transmit is disabled out of the box:** "Flipper Zero has the send function disabled out of the box until the device is updated either via Flipper Mobile App or qFlipper with a microSD card inserted." Attempting Sub-GHz before that update shows "Firmware update needed. Update the firmware before using this feature." During that update, "the region where the device is located is determined and frequencies allowed for transmission in that region are unlocked." The provisioned region code (ISO format) is shown on the Device Info screen (hold DOWN on the Desktop). Source: https://docs.flipper.net/zero/sub-ghz/frequencies
- Attempting to transmit a recorded signal on a prohibited frequency produces: **"Transmission is blocked. Transmission on this frequency is restricted in your region."** Source: https://docs.flipper.net/zero/sub-ghz/frequencies
- **Official regional TX band table (abridged):** EU and a long list of associated countries — 433.05–434.79 MHz and 868.15–868.55 MHz; USA, Canada, Mexico, Brazil, Australia, New Zealand and other Americas/APAC countries — 304.10–321.95 MHz, 433.05–434.79 MHz, 915.00–928.00 MHz; UAE — 420.00–440.00 MHz; Taiwan — 304.50–321.95, 433.075–434.775, 915.00–927.95 MHz; Singapore — 300.00–300.30, 312.00–316.00, 433.50–434.79, 444.40–444.80 MHz; Israel and India — 433.05–434.79 MHz only; Philippines — 430.00–440.00 MHz; China — 314.00–316.00, 430.00–432.00, 433.05–434.79 MHz; "Rest of the world" — 312.00–315.25 and 920.50–923.50 MHz. (Full country lists at the source.) Source: https://docs.flipper.net/zero/sub-ghz/frequencies
- The Sub-GHz app's **Region Information** screen shows the device's region and its allowed transmission frequencies in-device. In the RPC data model, a region is a country code plus bands carrying explicit **power_limit** and **duty_cycle** parameters — the restriction is enforced as data, not just UI. Source: https://docs.flipper.net/zero/sub-ghz ; https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/flipper.proto

### 7.3 NFC (13.56 MHz)

- Read, save to SD, write to rewritable cards, and emulate MIFARE Classic / Ultralight / DESFire, FeliCa, HID iClass (PicoPass), and NFC Forum types. The CLI `nfc` sub-shell goes further for development work: card scanning, emulation, per-protocol dump formats (ISO14443-3A/3B/4A/4B, ISO15693, FeliCa, MIFARE Ultralight/Classic/Plus, DESFire, SLIX, ST25), **raw APDU-level send**, and MIFARE Ultralight block read/write/info commands. Source: https://flipper.net/products/flipper-zero ; https://docs.flipper.net/zero/development/cli

### 7.4 RFID (125 kHz)

- Read, save, emulate, and write low-frequency cards: AM/OOK modulation, ASK/PSK coding; families EM400x/EM410x/EM420x, HID, Indala, FDX-A/B, Pyramid, AWID, Viking, Jablotron, Paradox, PAC Stanley, Keri, Gallagher, NexWatch. The CLI adds raw read, raw emulate, and raw analyze modes plus per-key-type write support with defined data lengths (see §3.4). Source: https://flipper.net/products/flipper-zero ; https://docs.flipper.net/zero/development/cli

### 7.5 iButton (1-Wire)

- Read, save, write (rewritable keys), and emulate Dallas DS199x / DS1971, CYFRAL, Metakom, TM2004, and RW1990 keys, via the device's 1-Wire contact pad; the CLI exposes `ikey read/emulate/write` and a generic `onewire search`. Source: https://flipper.net/products/flipper-zero ; https://docs.flipper.net/zero/development/cli

### 7.6 Infrared

- Learn (RX) and replay (TX) IR signals, save them to SD, use the built-in signal library and a universal-remote "guess" mode; decoded protocols: NEC family, Kaseikyo, RCA, RC5/RC6, Samsung, SIRC. The CLI supports raw capture/transmission (raw frames up to 512 samples; raw carrier 10–56 kHz) and protocol decode. Source: https://flipper.net/products/flipper-zero ; https://docs.flipper.net/zero/development/cli

---

## 8. Debugging and developer tooling (official)

### Debug probes

- Firmware flashing/debugging is supported with: the **Flipper Zero Wi-Fi Development Board**, **CMSIS-DAP** probes (the docs name the Raspberry Pi Debug Probe as an example), **ST-Link (v2, v3, v3mods)**, and **J-Link**. `./fbt flash` flashes via the connected debugger, `./fbt flash_usb` over USB. Source: https://github.com/flipperdevices/flipperzero-firmware
- SWD is also reachable directly on the GPIO header for generic probes: **SWCLK = pin 10, SWDIO = pin 12, GND = pins 8/11/18**; connecting a probe's UART to the Flipper USART (pins 13/14) is recommended because "this serial port outputs logs and information about critical program crashes when they occur." Source: https://developer.flipper.net/flipperzero/doxygen/app_dev_debugging_faps.html

### Build/flash/debug workflow

- fbt generates ready-made VS Code configurations (`./fbt vscode_dist`); debugging the firmware is `./fbt flash` then attach (blackmagic over Wi-Fi/USB, or DAP over USB). Starting a debug session halts firmware execution until Continue is pressed. Source: https://developer.flipper.net/flipperzero/doxygen/dev_board_debugging_guide.html
- **FAP debugging:** fbt ships a gdb-py script (`debug/flipperapps.py`, loaded by the stock debug configurations) that lets you debug a FAP as if it were part of the firmware — inspect variables, set breakpoints, step. If a session is active, the firmware triggers a breakpoint after loading a FAP into memory but before running any of its code, so breakpoints can be set first; firmware and app build types (debug/release) must match. Source: https://developer.flipper.net/flipperzero/doxygen/apps_on_sd_card.html
- **Dev-board logging:** official guides cover reading Flipper logs through the dev board (in addition to the CLI `log` stream), and the dev board's two debug modes (Blackmagic vs CMSIS-DAP) are switchable from its web interface. Source: https://developer.flipper.net/flipperzero/doxygen/dev_board.html ; https://developer.flipper.net/flipperzero/doxygen/dev_board_debugging_guide.html

### Logging

- Runtime logging is a CLI stream: `log` with levels **error / warn / info (default) / debug / trace**; log verbosity is also a device setting (Settings → System → Log Level), and heap diagnostics are available via `sysctl heap_track`, `sysctl debug`, and `free` / `free_blocks`. The official bug-report procedure itself is built on this: set Log Level to Debug, connect at 230400 baud, run `info device`, then `log`. Source: https://docs.flipper.net/zero/development/cli ; https://docs.flipper.net/zero/mobile-app/bug-report

### Crash handling

- The Furi runtime provides explicit crash primitives: **`furi_check(condition[, message])`** — "Always assert condition and crash the system if CONDITION is false"; **`furi_crash([message])`** — deliberately crash the system on an abnormal condition; **`furi_halt([message])`** — halt the system (used internally to shut down when poweroff is impossible). Source: https://developer.flipper.net/flipperzero/doxygen/furi_check.html
- Crash output is emitted on the UART (pins 13/14) for capture during debugging (see above), and a device rendered unbootable by firmware problems is recovered through the DFU bootloader + qFlipper Repair path (§2), so crash-driven bricking is a recoverable state by design. Source: https://developer.flipper.net/flipperzero/doxygen/app_dev_debugging_faps.html ; https://docs.flipper.net/zero/basics/firmware-update/firmware-recovery

---

## Gaps not covered by official sources

The following were **not** found in the official sources consulted (docs.flipper.net, developer.flipper.net, flipper.net, github.com/flipperdevices). They are listed here rather than sourced from unofficial material:

1. **NFC front-end chip part number.** Community material commonly names an ST NFC reader IC, but the official prose documentation consulted does not name the NFC controller chip (the docs and product page describe capabilities and card families only). It presumably appears in the published schematics/source, which are download artifacts rather than text pages.
2. **Exact MCU memory figures and clock speed** (RAM/flash sizes for the STM32WB55RG) are not stated in Flipper's official prose specs; they exist only in STMicroelectronics' datasheet, which is outside the permitted source set.
3. **CC1101 transmit power** (dBm figure) is not stated in the official docs consulted — only a "maximum range is 50 meters" figure is given.
4. **Display physical size and controller IC** are not stated officially in prose (only "monochrome LCD, 128×64 px").
5. **RPC wire framing details** — the byte-level framing of `PB.Main` messages on the serial stream (length prefixing/delimiting scheme) and the BLE serial service UUIDs/characteristics used for RPC over Bluetooth are not described in the official prose documentation consulted; the official .proto files define the messages only. (Framing *is* officially documented for the separate GPIO Expansion Module Protocol, but that document covers module communication, not the USB/BLE RPC session framing.)
6. **A prose statement that the mobile app's BLE link carries the same protobuf RPC** — official pages confirm the mobile apps update and remotely control the device over BLE, and define RPC as the protocol "widely used by Flipper Zero companion applications," but the consulted pages do not spell out the BLE RPC service/framing explicitly.
7. **Marauder firmware for the Wi-Fi dev board** (the ESP32 Wi-Fi security-testing firmware): no official Flipper Devices publication of a Marauder port was found in the sources consulted. The only official dev-board firmware is Flipper's Black Magic Probe / CMSIS-DAP firmware (source: flipperdevices/blackmagic-esp32-s2). Marauder for this board exists in the community ecosystem, which is outside this dossier's source rules.
8. **How the Sub-GHz region is "determined" during first update** — the official Frequencies page says the region is determined during the update via the mobile app or qFlipper, but does not specify the determination method (e.g., IP geolocation, app storefront, or manual selection).
9. **Whether an already-provisioned region can be changed later**, and what happens to saved Sub-GHz files recorded in another region, are not addressed in the official pages consulted.
10. **Internal-storage filesystem** — the firmware repo visibly bundles the FatFs driver (`lib/fatfs`) for storage, but the consulted official prose does not name the internal flash filesystem implementation.
11. **JavaScript API surface** — the on-device `js` CLI command and the bundled `mjs` engine are official, but the official JS module/API documentation was not covered in this pass, so the scriptable-from-JS capability set is unenumerated here.
12. **Battery capacity in older official materials** — the current official product page states 2100 mAh; no second official figure was found in this pass to reconcile against older references.
