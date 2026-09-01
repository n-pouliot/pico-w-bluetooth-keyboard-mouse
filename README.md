# xbox-pico

`xbox-pico` is experimental firmware for the original Raspberry Pi Pico W. It
connects to one supported BLE keyboard and one supported BLE mouse at the same
time, then exposes a fixed, ordinary USB keyboard and USB mouse through the
Pico W's Micro-USB port.

> Status: **READY FOR HARDWARE TEST** under the deliberately conservative
> **PRE-HARDWARE TEST** label. The source builds, host-side tests pass, and two
> clean same-day release builds produced identical UF2 files.
> It has not yet run on a physical Pico W, enumerated on a PC, connected to real
> peripherals, or been tested on an Xbox Series X. Do not interpret this status
> as a compatibility claim.

This project does not emulate an Xbox controller, use Xbox authentication,
send proprietary commands, mount storage during normal operation, or modify
the console. An Xbox game still decides whether it accepts keyboard and mouse
input.

## Hardware

Required:

- one original Raspberry Pi Pico W (`pico_w`, RP2040);
- one USB data cable with Micro-USB for the Pico W and the appropriate plug for
  the PC or console;
- one BLE HID keyboard; and
- one BLE HID mouse.

No headers, soldering, breadboard, GPIO wiring, external power supply, voltage
change, or overclock is used. Power the board only through its normal Micro-USB
connection for this project. Do not attach a second 5 V source.

Pico 2 W is not the release target. Do not flash this Pico W build to a
different board.

## What the host sees

USB descriptors are static from power-on:

| USB function | Interface | IN endpoint | Packet | Polling |
|---|---:|---:|---:|---:|
| Boot-capable keyboard | 0 | `0x81` | 8 bytes | 1 ms |
| Boot-capable mouse | 1 | `0x82` | up to 5 bytes | 1 ms |

There are no USB Report IDs because each function has its own interface and
endpoint. BLE devices never change the USB descriptors and reconnecting does
not intentionally re-enumerate USB.

Core 0 services TinyUSB. Core 1 owns BTstack and the CYW43439 radio. The two
roles have separate handles, HIDS CIDs, parsed report plans, retry state,
generations, and cross-core mailboxes. Connection creation and Report Map
discovery are serialized, but both accepted BLE links remain active together.

See [the architecture decision record](docs/architecture.md) for the detailed
design and alternatives considered.

## Supported input subset

Keyboard support:

- keyboard/keypad HID usage page, modifiers, ordinary arrays, function,
  navigation, and keypad keys;
- multiple BLE Report IDs;
- compatible one-bit NKRO maps, translated to canonical USB 6KRO;
- HID ErrorRollOver when more than six non-modifier keys are down.

Mouse support:

- five buttons;
- signed relative 8-bit or 16-bit X/Y;
- vertical wheel and horizontal pan;
- large deltas split across bounded `[-127, 127]` USB reports; accumulator
  overflow is treated as a fault rather than silently wrapping.

Deliberate limitations:

- exactly one HIDS service per peripheral;
- no absolute mouse/digitizer input;
- no consumer/media, system-control, macro, or vendor-key forwarding;
- no keyboard lock-LED output forwarding;
- keyboards that use the common host-displays/six-digit-code workflow enter the
  fixed code `739241` and then press Enter;
- mice and other no-input/no-output peripherals use Secure Connections Just
  Works, which is encrypted and bonded but is not MITM-authenticated;
- no Numeric Comparison, Pico-entered passkey, legacy-pairing, or OOB workflow;
- a malformed, oversized, mixed keyboard/mouse, or unsupported Report Map is
  rejected rather than guessed.

## First-time enrollment

The normal firmware does not read BOOTSEL at runtime. When either role has no
saved record, a 180-second enrollment window opens after USB has configured and
Bluetooth starts.

1. Test on a PC first, not on the Xbox.
2. Keep other nearby unpaired HID devices out of pairing mode.
3. Enroll the keyboard first. For an MX Mechanical using free Easy-Switch slot
   3, hold key `3` for about three seconds until its LED blinks rapidly. Wait
   until the Pico gives three short flashes followed by a pause, then type
   `739241` on that keyboard and press Enter. Do not add it in Windows Bluetooth
   settings: the keyboard is pairing directly with the Pico.
4. Put the intended BLE mouse into pairing mode. The Logitech G502 LIGHTSPEED
   is not a BLE mouse and cannot fill this role; use the separate BLE mouse.
5. The Pico connects to one candidate at a time, retrieves and validates its
   HID Report Map, classifies it from standards-based usages, and stores it only
   in the matching empty role.
6. If the window expires before an empty role is filled, unplug and reconnect
   the Pico to open another bounded window for that still-empty role.

Outside an enrollment window, unknown advertisers are ignored. Saved identities
must re-encrypt with an existing 16-byte Secure Connections bond; a missing key
does not silently authorize re-pairing.

The fixed keyboard code is public, not a password. Security depends on the
bounded enrollment window and putting only the intended device into pairing
mode. Mouse Just Works pairing has no authenticated display or input channel,
so user presence cannot prove the mouse's identity.

## LED meanings

These patterns are implemented but remain physically unverified:

| Pattern | Meaning |
|---|---|
| Solid | Keyboard and mouse are both ready |
| Three short flashes, then a pause | Type `739241` on the keyboard and press Enter now |
| One 100 ms pulse every 2 seconds | Keyboard ready; mouse absent |
| Two 100 ms pulses every 2 seconds | Mouse ready; keyboard absent |
| 500 ms on / 500 ms off | Neither role is ready |
| 200 ms on / 200 ms off | A connection, security, or discovery operation is active |

For a maintenance image, solid means its clear operation completed. A rapid
150 ms blink means that operation failed. Reflash the normal firmware after a
maintenance image succeeds.

## Reconnect and failure behavior

Known devices reconnect independently with a one-second backoff. A sleeping
device may need a key press or mouse movement to advertise again. If one link
fails, the other is not intentionally disconnected.

Every terminal path invalidates that role's generation and places an
all-keys-up or all-buttons-up report ahead of newer state. USB removes a report
from the mailbox only after TinyUSB confirms transfer completion. This design
prevents stale input from an old BLE connection and prevents release reports
from being dropped behind a full queue.

## Firmware files

The release directory contains:

- `pico_w_dual_ble_hid_bridge_PRE_HARDWARE_TEST.uf2` — normal firmware;
- `pico_w_clear_keyboard_pairing_MAINTENANCE.uf2`;
- `pico_w_clear_mouse_pairing_MAINTENANCE.uf2`; and
- `pico_w_clear_all_pairings_MAINTENANCE.uf2`.

Verify downloads against `release/SHA256SUMS.txt`. The normal firmware SHA-256
is `FF0EE7D18E4A4F0E420EE7AE4BC2990A069714BBE272E3A277270BA28ED0690F`.

Use the normal image for first installation. Maintenance images are only for
deliberately clearing role state and must be followed by reflashing the normal
image.

Follow the [beginner flashing guide](docs/beginner-flashing.md) exactly.

## Build and verification

- [Reproducible build](docs/build.md)
- [Test report](docs/test-report.md)
- [Code-review report](docs/code-review.md)
- [Adversarial-review report](docs/adversarial-review.md)
- [Risks and limitations](docs/risks-and-limitations.md)
- [Assumptions and evidence](docs/assumptions.md)
- [Recovery guide](docs/recovery.md)
- [Troubleshooting](docs/troubleshooting.md)
- [First hardware test plan](docs/first-hardware-test.md)
- [Verified devices](docs/verified_devices.md) — currently none for this revision

## Xbox caveat

Microsoft documents keyboard and mouse support on Xbox, but game support is
title-specific. Whether Xbox Series X accepts this exact composite topology is
**NOT TESTED**. Complete all PC stages in the hardware plan first. During normal
operation this firmware is USB device-only and advertises only two HID
interfaces; it does not enumerate as storage, CDC, gamepad, controller, or a
vendor-specific device.

## Lineage and license

The clean baseline was Shiomachi Software's
[`picow_ble_usb_hid_bridge`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge)
at commit `2c6a303d1f172e56b271283af978efdcc483a389` (tag `20260830_7`).
The dynamic single-device forwarding architecture was replaced, while required
TinyUSB and BlueKitchen attribution was preserved. See [LICENSE.TXT](LICENSE.TXT).
The inherited BlueKitchen demo terms include a non-commercial condition; review
the full license before redistribution or commercial use.
