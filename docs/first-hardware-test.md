# Conservative first hardware test plan

The release is software-verified only. Stop at the first unexpected result and
record exactly what happened; do not improvise with random firmware or wiring.

## Stage 1 — ROM recovery and flash

On a PC, enter BOOTSEL mode and confirm `RPI-RP2` appears. Copy the normal
PRE_HARDWARE_TEST UF2. Expected: the drive disappears and the Pico reboots.

Stop if `RPI-RP2` never appears, the copy fails, or the board becomes unusually
hot.

## Stage 2 — PC USB enumeration

Leave both BLE peripherals off. Expected: the PC recognizes a USB keyboard
interface and mouse interface immediately; no storage, serial, gamepad, or
unknown interface appears in normal runtime. The LED should show neither ready.

Capture the full USB descriptor tree with a trusted USB inspection tool. Stop
if interfaces/endpoints differ from `0/0x81` keyboard and `1/0x82` mouse.

## Stage 3 — Keyboard only

Put only the intended BLE keyboard into pairing mode. For the MX Mechanical,
hold Easy-Switch `3` for about three seconds until it blinks rapidly, type
`739241` on that keyboard, and press Enter. Do not pair it in Windows settings.
Expected: the Pico accepts the code and the keyboard-only LED pattern appears.

Test normal letters, modifiers, arrows, function keys, held keys, six
simultaneous keys, and release. Power the keyboard off while a harmless key is
held. Expected: the PC receives all-keys-up and the mouse USB interface remains
present.

Stop if the fixed passkey does not complete pairing, a different association
method appears, a key remains stuck, or unrelated input is generated.

## Stage 4 — Mouse only

If necessary, power-cycle the Pico to reopen the still-empty role window. Put
only the intended BLE mouse into pairing mode. Test movement, left/right/middle,
back/forward, vertical wheel, and horizontal pan if present. Power the mouse off
while a button is held. Expected: all buttons release and keyboard remains
usable.

Stop if motion direction is wrong, movement is lost, buttons remain held, or
the keyboard disconnects.

## Stage 5 — Both simultaneously on PC

Type while moving and clicking. Exercise both for at least 15 minutes, including
high-rate mouse movement and repeated key changes. Expected: neither input
starves or resets the other and USB never re-enumerates.

## Stage 6 — Power-cycle reconnect

Disconnect the Pico for ten seconds, reconnect it, and wake both peripherals.
Expected: no new pairing prompt, both saved identities reconnect independently,
and unknown nearby HID devices are ignored.

## Stage 7 — Failure and recovery

Repeat separate keyboard and mouse sleep/disconnect/reconnect tests. Use each
single-role maintenance UF2 once, verify only that role is cleared, then reflash
normal firmware and re-enroll it. Confirm BOOTSEL recovery still works.
Also interrupt a role-clear only if a sacrificial test setup and recovery time
are available; the documented response is to run clear-all before normal
firmware, never to guess at an orphan bond.

## Stage 8 — Xbox Series X

Only after Stages 1–7 pass, power down the test setup and connect the Pico W to a
normal Xbox USB port with the same data cable. Do not attach external power.
Expected: at most ordinary keyboard/mouse behavior in Xbox UI or games that
support it. No controller, storage, or vendor device should appear.

Test one Xbox-supported title conservatively. Game-level keyboard/mouse support
is title-specific. If the console does not accept the device, disconnect it and
report the console version, game, LED state, and PC descriptor results; do not
try proprietary commands or controller spoofing.
