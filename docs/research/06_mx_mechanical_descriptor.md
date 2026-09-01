# MX Mechanical descriptor compatibility evidence

Date: 2026-09-01

Logitech documents that MX Mechanical supports direct Bluetooth Low Energy,
three Easy-Switch channels, and the standard host-shows-code keyboard pairing
flow:

- <https://support.logi.com/hc/en-sg/articles/5216357107991-Getting-Started-MX-Mechanical>
- <https://www.logitech.com/en-sg/setup/mxsetup/keyboard-setup/bluetooth.html>

A public hardware report includes the MX Mechanical keyboard-interface HID
descriptor captured through its Logi Bolt receiver:

- <https://github.com/hrvach/deskhop/issues/47>

That descriptor uses a 16-byte, one-key-per-bit keyboard input report rather
than the usual six-key boot array. The exact bytes are now a host-test fixture.
The bounded compiler accepts all 128 key/modifier bits, normalization produces
the canonical USB boot-keyboard report, and release is tested.

This evidence removes a known parser-format risk but does not prove the direct
BLE Report Map is byte-identical to the Bolt USB interface or that the device
exposes exactly one HIDS service. Those remain first-hardware-test gates.
