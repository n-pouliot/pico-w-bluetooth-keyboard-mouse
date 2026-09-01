# Fixed-passkey keyboard pairing addendum

Date: 2026-09-01

## User-device evidence

Logitech's official Bluetooth keyboard setup procedure says to select the
desired Easy-Switch channel, hold it for three seconds until its LED blinks
rapidly, select the keyboard on the host, type the code shown by the host on the
keyboard, and press Enter:

- <https://www.logitech.com/en-sg/setup/mxsetup/keyboard-setup/bluetooth.html>

The intended keyboard is a Logitech MX Mechanical with Easy-Switch channel 3
available. It has not yet been physically paired with this firmware, so this is
documented compatibility evidence rather than a test result.

## BTstack evidence and selected model

The pinned BTstack 1.6.2 Security Manager used by Pico SDK 2.2.0 provides
`sm_use_fixed_passkey_in_display_role(uint32_t)`. Its API documentation states
that the fixed display-role value is used for both Legacy and Secure
Connections pairing:

- <https://bluekitchen-gmbh.com/btstack/develop/appendix/apis/>
- pinned declarations and implementation:
  `lib/btstack/src/ble/sm.h` and `lib/btstack/src/ble/sm.c` in Pico SDK 2.2.0

Inspection of the pinned association matrix shows that a central/initiator with
`DisplayOnly` and a responder with `KeyboardOnly` selects `PK_RESP_INPUT`: the
central displays the passkey and the keyboard inputs it. A responder with
`NoInputNoOutput`, such as a typical BLE mouse, selects Just Works. Therefore
the bridge can retain one static local I/O capability and support both target
workflows without changing the USB topology.

## Implementation and security boundary

The bridge uses fixed six-digit value `739241`, `DisplayOnly`, bonding,
16-byte encryption keys, and Secure Connections. It does not globally require
MITM authentication because doing so would reject a no-input/no-output mouse.
The post-encryption authorization gate requires a Secure Connections bond and
a 16-byte key before any role can be saved. Because BTstack considers I/O
capabilities only when either peer requests MITM protection, a keyboard could
otherwise fall back to Just Works. The bridge therefore also requires both the
fixed display event and BTstack's persisted authenticated flag before committing
a candidate classified as a keyboard. A mouse does not require authentication.

A passkey-display event is accepted only when all of these are true:

- its connection maps to a device context currently in the securing state;
- that context is the current bounded enrollment attempt;
- BTstack marks the event as Secure Connections; and
- the displayed number is exactly `739241`.

Unexpected or malformed display events are declined and disconnected. Numeric
Comparison and the opposite workflow, where a number shown by the peripheral
must be entered into the Pico, remain declined.

A candidate later classified as a keyboard is rejected if it reached discovery
through Just Works or if its stored bond is not authenticated. Its exact
uncommitted bond is removed. This preserves mouse compatibility without allowing
a keyboard to silently bypass `739241`.

The fixed value is public and is not treated as a password. The practical
first-run control is the 180-second enrollment window plus the user's act of
putting only the intended keyboard or mouse into pairing mode. Physical pairing
with the MX Mechanical remains a hardware gate.
