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
broader Xbox/game coverage remain unverified. The user subsequently connected
the same Pico and keyboard to an Xbox and confirmed keyboard input works there;
the exact Xbox screen/title was not recorded.

## Logitech M196 mouse (M-R0114) — partial pass

The passive-scan firmware did not enroll the mouse. Active scanning then
reached a later stage, and the diagnostic image repeatedly reported security
failure code 2. The original host policy required 16-byte LE Secure Connections
for every role, which was narrower than HOGP's mandatory unauthenticated Level
2 host capability. The compatibility image accepts encrypted bonded mouse
pairing with a 7- to 16-byte key while retaining the strict keyboard policy.

With `pico_w_dual_ble_hid_bridge_M196_COMPATIBILITY_TEST.uf2` (SHA-256
`7FA5350C1624AB35790336BA7B0E118A837E3831F66C1D5CA23C2DB2306C3A78`), the
M196 paired and produced mouse input on Windows. The LED showed two pulses
while only the mouse was ready, then solid after the saved MX Mechanical
reconnected; the user confirmed both devices worked. Button/wheel coverage,
15-minute simultaneous stress and power-cycle reconnect remain pending. The
same M196 then produced mouse input through the Pico in The Sims 4 on Xbox
Series X. The Xbox dashboard not reacting to the mouse is expected platform
behavior, not a failed bridge test.

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

Xbox acceptance is recorded separately from PC behavior and must name the title
tested. The Sims 4 passed on Xbox Series X; the console software version was not
captured. One game's keyboard/mouse support does not imply support in every
other game.
