# Verified devices

No physical keyboard, mouse, Raspberry Pi Pico W, PC USB host, or Xbox has been
verified with the `xbox-pico` dual-device revision yet.

The starting upstream project listed several devices tested with its own
single-device/dynamic-descriptor firmware. Those results do **not** verify this
substantially different canonical dual-interface implementation and are not
carried forward as compatibility claims.

Planned first keyboard: Logitech MX Mechanical, using Easy-Switch slot 3 and
fixed passkey `739241`. It is a target for the first test, **not a verified
device**. The Logitech G502 LIGHTSPEED is not a BLE candidate for this bridge;
the planned mouse is a separate BLE mouse whose exact model is not yet known.

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
