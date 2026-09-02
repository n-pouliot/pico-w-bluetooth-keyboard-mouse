# Contributing

Thanks for helping improve `pico-w-bluetooth-keyboard-mouse`. Device compatibility reports are
particularly useful because BLE HID Report Maps and pairing policies vary by
manufacturer.

## Before contributing

Read [LICENSE.TXT](LICENSE.TXT). The inherited BlueKitchen demo terms restrict
this project to personal, non-commercial use. Contributions are accepted under
the same applicable terms; do not submit code you cannot legally contribute.

This project will not add Xbox controller emulation, proprietary
licensed-accessory authentication, console modification, or security-bypass
behavior.

## Report a device

Use the device compatibility issue form and include:

- exact manufacturer and model;
- keyboard or mouse role and advertised connection type;
- original Pico W board revision;
- firmware filename and SHA-256;
- PC or console host and, for Xbox, the exact game;
- pairing steps and LED pattern;
- movement/key/button/wheel results; and
- reconnect behavior after a ten-second Pico power cycle.

Do not include Bluetooth keys, private addresses, account details, or diagnostic
files containing information you have not reviewed.

## Code changes

1. Create a focused branch.
2. Follow [docs/build.md](docs/build.md).
3. Add host tests for pure parser, policy, persistence, or mailbox logic.
4. Run `ctest --test-dir build-host-tests --output-on-failure`.
5. Build all Pico W firmware targets with project warnings treated as errors.
6. Update the evidence boundary and compatibility documentation honestly.

Never silently broaden accepted HID semantics or pairing security. Document the
reason, test boundaries, and security/interoperability tradeoff.
