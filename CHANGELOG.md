# Changelog

## Unreleased — dual BLE HID bridge

- Target the original Raspberry Pi Pico W and expose a static, boot-capable USB
  keyboard plus a static, boot-capable USB mouse on separate interfaces.
- Replace dynamic BLE Report Map passthrough with bounded descriptor parsing and
  canonical keyboard/mouse report translation.
- Support one BLE HID keyboard and one BLE HID mouse concurrently with explicit
  per-device state and independent reconnect behavior.
- Add role-specific persistent authorization records with CRC32 validation and
  separate keyboard, mouse, and all-role maintenance-clear images.
- Add cross-core release barriers and TinyUSB completion tracking to prevent
  stale or stuck input after disconnects and failed transfers.
- Restrict first-run enrollment to a 180-second window and Secure Connections
  with 16-byte encryption keys; support fixed displayed passkey `739241` for
  keyboard responder-input pairing and Just Works for no-input/no-output mice.
  Keyboard authorization requires the passkey event and an authenticated bond,
  preventing a silent Just Works downgrade. Unknown devices are ignored outside
  an enrollment window.
- Add host tests for HID parsing/normalization, persistence, cross-core
  mailboxes, malformed input, exhaustive mutation of the canonical boot
  keyboard descriptor, an empirical MX Mechanical NKRO descriptor, and
  randomized descriptor mutation smoke tests.
- Add an exact three-pulse LED prompt for keyboard passkey entry and a
  double-click Windows USB diagnostic collector.
- Harden BTstack callback routing, HIDS service preflight, connection-event and
  timeout ownership, exact rejected-bond cleanup, radio restart retries, USB
  suspend/resume, boot-protocol mouse masking, and overflow fail-safe behavior.
- Add four reproducible, SHA-256-recorded PRE_HARDWARE_TEST/maintenance UF2
  artifacts and remove stale single-device binaries.
- Add architecture research, safety/recovery guidance, reproducible build
  instructions, beginner flashing instructions, and a staged hardware plan.

This revision remains **PRE-HARDWARE TEST**. Physical Pico W, BLE peripheral,
PC USB, power/current, latency, and Xbox behavior are not yet verified.

## 20260830

- Support for Devices with Privacy Addresses (RPA)
  - Smoothly and automatically reconnects to paired Bluetooth devices (such as modern keyboards and mice) that periodically change their address for privacy.
- Simplified Pairing
  - Eliminated manual passkey/PIN entry, allowing Bluetooth devices to pair seamlessly and automatically ("Just Works" pairing).
- Device Name Update
  - Changed the default Bluetooth display name from `"HOG Host"` to `"BLE-USB HID Bridge"`.
- Other Improvements
  - Various minor bug fixes and stability enhancements.

## 20260813

- Changes in `hog_host_demo.c`
  - Fixed an issue where the BLE connection state and LED indication would mismatch when the BLE device was rapidly power-cycled.
  - Reordered function definitions to improve source code readability.

- Global Changes (All source files)
  - Code cleanup: Removed modification history comments such as `@add`, `@chg`, and `@del`.

## 20260810

### Added

- Pico 2 W (RP2350) support alongside the Pico W. `PICO_BOARD` picks the target
  and defaults to `pico2_w`.
- `ENABLE_USB_LOGGING`, `ENABLE_HEARTBEAT_LOGS` and `UART_BAUD_RATE` build
  options.

### Fixed

- Keyboards that place a descriptor after the CCC descriptor no longer stall in
  GATT discovery, via BTstack's `ENABLE_GATT_LEGACY_CCC_DISCOVERY`.
- The HID report descriptor cache grew from 500 bytes to 2 KB, so descriptors
  from keyboards with media keys or multi-device switching are not truncated.
- Core 0 no longer touches the CYW43-attached status LED before Core 1 has
  finished `cyw43_arch_init()`, which could hang Core 0 during boot.
- A 12.5-15 ms connection interval is requested once the link is encrypted,
  instead of accepting the device's power-saving default.
