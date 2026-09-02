# Architecture decision record

Date: 2026-09-01  
Target: original Raspberry Pi Pico W (`pico_w`, RP2040)  
Starting project: `https://github.com/shiomachisoft/picow_ble_usb_hid_bridge`  
Starting revision: `2c6a303d1f172e56b271283af978efdcc483a389`

## Gate 1 decision

**APPROVED FOR IMPLEMENTATION, NOT APPROVED FOR HARDWARE RELEASE.**

The research workstreams found no architectural reason that two low-bandwidth BLE HOGP links cannot be represented by the Pico SDK 2.2.0 BTstack port. BTstack has per-HCI-handle GATT and HIDS client contexts, and the CYW43439 controller firmware can represent more than two LE links. The claim still needs physical Pico W validation.

The starting application cannot be extended safely by changing connection-count constants. Its connection state, HIDS CID, descriptor view, persistence selector, timer, report queue, reconnect path, and USB shape are singletons. Its dynamic descriptor passthrough also lets an untrusted BLE peripheral control the USB HID grammar and cannot compose two unrelated Report-ID namespaces.

Implementation may begin only under the bounded architecture below.

## USB architecture

The Pico W will enumerate once as a static USB composite device with exactly two HID interfaces:

| Function | HID instance | Interface | Endpoint | Report protocol | Boot protocol |
|---|---:|---:|---:|---|---|
| Keyboard | 0 | 0 | `0x81` IN | 8-byte 6KRO state | same 8-byte state |
| Mouse | 1 | 1 | `0x82` IN | buttons, relative X/Y, wheel, pan | buttons and relative X/Y only |

Both interfaces are present before Bluetooth starts. BLE connection, sleep, failure, replacement, and reconnect never alter USB descriptors and never trigger USB re-enumeration. USB Report IDs are not used because the functions have separate interfaces and endpoints.

This is preferred over one interface with keyboard/mouse Report IDs because each function can declare the correct boot protocol, traffic and backpressure remain isolated, and unrelated BLE Report IDs cannot collide. RP2040 endpoint and USB DPRAM limits provide ample headroom for two 8-byte interrupt-IN endpoints.

The normal firmware exposes no mass-storage, CDC, vendor, gamepad, Xbox-controller, or USB-host function. BOOTSEL ROM mode remains a separate recovery mode supplied by RP2040.

## Bluetooth architecture

The application owns two explicit role contexts: keyboard and mouse. Each context owns its persisted identity, current address/RPA, HCI handle, HIDS CID, security state, connection generation, parsed Report Map plan, canonical input state, retry/backoff, and failure counters.

One global radio orchestrator owns scanning and the single outgoing LE create procedure. Connection attempts and complete HIDS discovery are serialized. Once discovery is complete, both links remain active simultaneously and reports are routed by HIDS CID, service index, handle, and generation.

Release 1 accepts exactly one HID Service instance per peripheral. This is intentional: the pinned BTstack HIDS client uses a shared Report Map byte store whose reservations can overlap when discoveries or service instances interleave. The application will:

1. permit at most one in-progress HIDS discovery;
2. allocate 4096 bytes of BTstack staging storage;
3. reject a completed Report Map over 2048 bytes;
4. compile/copy the accepted map into application-owned bounded metadata; and
5. start discovery for the other link only after the first is accepted or torn down.

Pool limits become two HCI connections, two GATT clients, and two HIDS clients. The final map file must quantify the RAM cost.

## HID classification and translation

Bluetooth names never determine a role. GAP Appearance and HID service advertising are discovery hints only. Final classification requires a structurally valid Report Map with exactly one supported role:

- keyboard: Generic Desktop Keyboard/Keypad Application Collection and supported Keyboard/Keypad inputs;
- mouse: Generic Desktop Mouse Application Collection with signed relative X and Y inputs.

A map that is malformed, oversized, absolute-pointer-only, roleless, or contains both accepted keyboard and mouse roles is rejected. Appearance is never trusted as final classification; the compiled Report Map role wins.

The descriptor compiler is allocation-free and has explicit limits:

- one HIDS service per peripheral;
- Report Map at most 2048 bytes per device;
- input payload at most 64 bytes after removing BTstack framing;
- at most 8 input Report IDs, 8 Application Collections, collection depth 8, global stack depth 4, and 256 compact extraction fields;
- extracted field width 1 through 16 bits;
- no HID long items;
- checked arithmetic for every size, count, and bit offset.

Supported keyboard input includes modifiers, ordinary array reports, one-bit NKRO fields, function/navigation/keypad usages, and multiple Report IDs. USB output is intentionally 6KRO; more than six non-modifier keys produces the HID ErrorRollOver state. Consumer/media, system-control, macro, and vendor collections are ignored after structural validation.

Supported mouse input includes buttons 1-5, signed relative 8/16-bit X and Y, vertical wheel, and horizontal pan. Large deltas are accumulated and emitted in multiple signed 8-bit USB reports rather than clamped away. Absolute pointing devices and unsupported semantics fail inertly.

BTstack inserts the Report ID as byte zero of every HIDS report event, including implicit ID zero. The callback boundary verifies this byte against event metadata, removes it, requires the exact descriptor-computed payload length, and then applies the precompiled extraction plan. Raw BLE bytes and descriptor pointers never cross to the USB core.

## Cross-core handoff and stuck-input prevention

Keyboard and mouse have independent bounded mailboxes protected by a Pico critical section. Keyboard messages are complete state snapshots. Mouse state retains current buttons and checked wide accumulators for relative movement and wheels.

Each role uses a monotonically increasing RAM generation. Stale callbacks and queued data from older handles/generations are ignored.

Every terminal path calls one idempotent role-release operation: HCI disconnect, HIDS disconnect, security failure, descriptor rejection, malformed-report quarantine, timeout, local replacement, maintenance clear, and internal abort. Release increments the generation, discards pending input for that role, and installs a high-priority all-zero report. A release cannot sit behind a full queue or be overwritten by a fast reconnect; the zero report is a barrier that USB must submit before newer state. The other role is untouched.

## Pairing and security

Release 1 uses BLE-only HOGP with `DisplayOnly` and bonding during an explicitly armed enrollment window. A keyboard that requests the common responder-input association model receives the fixed displayed value `739241`; the user types that value on the keyboard and presses Enter. A keyboard requires authenticated LE Secure Connections with a 16-byte key. A no-input/no-output mouse uses encrypted bonded Just Works with a 7- to 16-byte key; LE Secure Connections is preferred, while legacy fallback is accepted for interoperability. Just Works is not MITM-authenticated.

The passkey is intentionally fixed because the Pico W has no display and the beginner workflow must not require UART wiring. It is public, so the bounded enrollment window and putting only the intended device into pairing mode remain important. Display events are accepted only for the current securing enrollment candidate, only for Secure Connections, and only when the stack's value exactly matches the compiled code. After Report Map classification, a keyboard is committed only if that display event occurred and BTstack stored the bond as authenticated; a keyboard that falls back to Just Works or legacy pairing is rejected and its uncommitted bond is removed. Numeric Comparison, passkey input on the Pico, and OOB are declined. Mouse legacy fallback is an explicit compatibility/security tradeoff contained to the unauthenticated mouse role.

Normal operation reconnects only identities authorized by role records and ignores unknown HID advertisers. Initial enrollment is opened by first-run state for a bounded interval. Re-pairing is performed with small, separately built maintenance UF2 images that clear keyboard, mouse, or both role records and their matching bond, then require the normal firmware to be reflashed. This is more cumbersome than a runtime button but avoids unsafe QSPI/flash sampling while the other RP2040 core executes BTstack.

The physical BOOTSEL button is not read by normal firmware. It remains dedicated to RP2040 ROM recovery and maintenance flashing.

## Persistence

BTstack continues to own LTK/IRK/security records. The application stores two independent 24-byte, explicitly serialized role records under `XHKB` and `XHMS`. Each contains magic, schema version, embedded length, role, identity address type/address, reserved bytes, and CRC-32/ISO-HDLC. A C structure is never written directly.

Load requires exact length, valid magic/version/role/type/address/reserved fields, valid CRC, and a matching acceptable BTstack bond. Corruption or a missing key affects only that role.

Application flash writes occur only on successful enrollment, explicit clear, or a future documented schema migration. Boot, reconnect, reports, ordinary failures, LED updates, and last-seen activity cause no application writes. Release 1 replaces a role only after its maintenance image removes the old authorization and matching bond.

## Pairing/status indication

The onboard Pico W LED communicates aggregate state:

- solid: both roles connected;
- one 100 ms pulse every two seconds: keyboard connected, mouse absent;
- two 100 ms pulses every two seconds: mouse connected, keyboard absent;
- 500 ms on/off: neither connected;
- three 100 ms pulses followed by a pause: enter the fixed keyboard passkey now;
- 200 ms on/off: a connection, security, or discovery operation is active.

Maintenance firmware uses solid for reported success and a 150 ms blink for reported failure.

No LED pattern is claimed as verified until tested on a Pico W, whose LED is controlled through CYW43439.

## Recovery and power

RP2040 BOOTSEL recovery is in immutable on-chip ROM; ordinary application UF2 files cannot overwrite it. A broken application or corrupted external flash is normally recoverable by holding BOOTSEL while connecting the Pico W to a computer and copying a correct Pico W UF2 to `RPI-RP2`. This does not protect against physical board, connector, cable, power, or button damage.

The normal use case is powered only through the Pico W micro-USB connector. No GPIO power, external supply, voltage change, overclock, OTP/fuse operation, debug lock, soldering, or Xbox-proprietary protocol is used. The descriptor will declare a conservative configured current within USB 2.0's 500 mA high-power limit. Actual active, pre-configuration, and suspend current remain physical validation items and will not be invented from a descriptor value.

## Latency model

The USB interfaces request a 1 ms full-speed polling interval. Once a BLE notification reaches the Pico, bounded parsing and mailbox publication should be substantially below one USB frame. Expected bridge-added scheduling is therefore roughly 0-1 ms plus any queued delta splitting; end-to-end latency is expected to be dominated by the peripheral and negotiated BLE interval. This is an inference until instrumented and measured.

## Release-1 limitations accepted at Gate 1

- one HIDS service instance per peripheral;
- Secure-Connections displayed fixed passkey for keyboard responder-input
  pairing, plus Just Works for no-input/no-output mice;
- 6KRO USB keyboard output; consumer/media/system/vendor keys omitted;
- relative mouse only, five buttons, vertical wheel, horizontal pan;
- no BLE keyboard LED-output forwarding;
- experimental private-use VID/PID pending an authorized production identity;
- Pico W, PC input, and one Xbox game have physical evidence; exact descriptor
  capture, current, latency, extended stress, and broader devices/games remain;
- Xbox support remains title-specific even if the console accepts the USB device.

## Required release evidence

Implementation does not become a release merely because it compiles. Required evidence includes host parser/persistence/mailbox tests, malformed-input mutation tests, descriptor validation, compiler warnings, static analysis, an independent code review, an independent tester build, adversarial review with all HIGH findings resolved, a clean final Pico W build, size/map/flash-bank-overlap checks, and independently verified recovery instructions.

The resulting UF2 was initially labelled `PRE_HARDWARE_TEST`. After Pico W,
Windows, and title-specific Xbox Series X tests passed, it was promoted to the
`v0.1.0-beta.1` public beta. Untested devices and games remain unclaimed.

## Research records

- `docs/research/01_upstream_btstack.md`
- `docs/research/02_usb_architecture.md`
- `docs/research/03_safety_xbox.md`
- `docs/research/04_hid_pairing_persistence.md`
