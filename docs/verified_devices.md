# Verified devices

Physical validation started on 2026-09-02 with an original Raspberry Pi Pico W
and a Windows PC.

## Logitech MX Mechanical keyboard — partial pass

Using Easy-Switch slot 3 and firmware SHA-256
`FF0EE7D18E4A4F0E420EE7AE4BC2990A069714BBE272E3A277270BA28ED0690F`, the
keyboard completed direct BLE pairing after the user entered fixed passkey
`739241`. Keyboard input reached Windows through the Pico after Windows
Bluetooth was disconnected, verifying the real BLE-to-Pico-to-USB keyboard
path. Full key coverage, held-key release, simultaneous mouse operation, and
Xbox behavior remain unverified.

## Logitech M196 mouse (M-R0114) — enrollment failure under investigation

The mouse entered pairing mode and Windows independently detected it, but the
passive-scan firmware above did not complete mouse enrollment or produce cursor
input. Mouse and Pico LED behavior suggested repeated discovery/connection
activity, but no UART trace or Report Map was captured, so the exact rejected
stage is unknown. Active-scan candidate
`pico_w_dual_ble_hid_bridge_M196_ACTIVE_SCAN_TEST.uf2` (SHA-256
`E40CFF334A855924A02FBE1B62E1FE35740F93F7E81F8BBF742448766082383D`) is built
and awaits physical retest.

The starting upstream project listed several devices tested with its own
single-device/dynamic-descriptor firmware. Those results do **not** verify this
substantially different canonical dual-interface implementation and are not
carried forward as compatibility claims.

The Logitech G502 LIGHTSPEED is not a BLE candidate for this bridge.

Add a device here only after recording:

- exact manufacturer and model;
- whether it is keyboard or mouse and that it uses BLE HID;
- Report Map acceptance result;
- pairing method;
- simultaneous operation with the other role;
- disconnect/reconnect and power-cycle behavior;
- PC operating system and observed USB topology; and
- firmware commit and UF2 SHA-256.

Xbox acceptance, if tested later, must be recorded separately from PC behavior
and must name the console software version and the title tested. A game's
keyboard/mouse support does not imply support in every other game.
