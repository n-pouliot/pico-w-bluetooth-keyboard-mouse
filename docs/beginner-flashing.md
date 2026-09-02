# Beginner flashing guide

Start with a Windows PC. Do not make your first test on the Xbox.

## What you need

- an original Raspberry Pi Pico W;
- a USB **data** cable, not a charge-only cable;
- the cable's small Micro-USB plug for the Pico W;
- `xbox-pico-v0.1.0-beta.1-pico-w.uf2` from the repository's
  `release` folder.

The Micro-USB socket is the only USB socket on the Pico W. BOOTSEL is the small
white pushbutton on the top of the board, close to that socket.

## Install the normal firmware

1. Leave the keyboard and mouse off for now.
2. Disconnect the Pico W from USB.
3. Press and keep holding the white BOOTSEL button.
4. While holding BOOTSEL, plug the Micro-USB end into the Pico W and the other
   end into the PC.
5. Release BOOTSEL.
6. Windows should show a removable drive named `RPI-RP2`. This drive is the
   RP2040 ROM bootloader, not the normal bridge firmware.
7. Copy exactly
   `xbox-pico-v0.1.0-beta.1-pico-w.uf2` onto `RPI-RP2`.
8. The drive should disappear automatically. The Pico reboots into the copied
   firmware; do not unplug it while the copy is in progress.
9. The onboard LED should begin a pattern. This behavior passed on the tested
   Pico W; stop and report the result if the drive does not disappear or the
   board behaves unexpectedly.

Copying the normal UF2 does not erase an existing role record. Use a maintenance
image only when intentionally changing pairing state.

## Enroll the peripherals on the PC

The first empty-role enrollment window lasts 180 seconds after the PC configures
USB. Read the keyboard and mouse steps before copying the UF2 so both devices
are charged and within reach.

1. Keep unrelated nearby keyboards and mice out of pairing mode.
2. Do **not** open Windows Bluetooth settings or pair either device to Windows.
   They must pair directly with the Pico W.
3. On the MX Mechanical, select the unused Easy-Switch slot `3`, then press and
   hold key `3` for about three seconds. Release it only when the slot-3 light
   is blinking rapidly. A short press merely switches slots and is not enough.
4. Watch the **Pico's onboard LED**. It first changes to a faster connection
   blink. When it gives a repeating pattern of **three short flashes followed
   by a pause**, type `739241` on the MX Mechanical and press Enter. Do not type
   the code before that three-flash prompt. The digits are consumed by Bluetooth
   pairing and should not appear in Notepad or another PC application.
5. Wait for the keyboard to finish pairing. One short LED pulse on the Pico
   every two seconds means the keyboard is ready and the mouse is still absent.
6. Put only the separate BLE mouse into its documented pairing mode. Do not use
   the G502 LIGHTSPEED: its LIGHTSPEED receiver is proprietary 2.4 GHz rather
   than BLE HID and this bridge cannot connect to it.
7. Wait for the Pico to classify and accept the mouse. A solid Pico LED means
   both devices are ready; two pulses means only the mouse is ready.
8. If the 180-second window closes before an empty role is enrolled, unplug and
   reconnect the Pico normally (do not hold BOOTSEL), then repeat only the
   missing device's pairing steps.

The firmware supports the common keyboard workflow in which the host displays
a six-digit passkey and the user types it on the keyboard. Because the Pico has
no screen, this build uses the documented fixed code `739241`. No second
keyboard, UART adapter, soldering, or display is needed. Encrypted bonded Just
Works remains available for mice, including legacy fallback for compatibility.
Numeric Comparison, a passkey displayed by the peripheral for entry into the
Pico, and OOB are rejected. Keyboards still require authenticated Secure
Connections.

The fixed code is public and should not be treated as a secret. Keep unrelated
devices out of pairing mode and use only the bounded enrollment window.

## If `RPI-RP2` does not appear

1. Disconnect the cable.
2. Confirm it is a data cable by trying another known data cable and PC port.
3. Hold BOOTSEL before reconnecting, and keep it held until the cable is fully
   inserted.
4. Try another direct PC USB port, without a hub.
5. Confirm the board really is a Pico W, not a Pico 2 W or another Pico model.

Do not erase the whole flash, short pins, apply external 5 V, or solder anything
as a troubleshooting step. Continue with the [recovery guide](recovery.md).

## Re-pair one role

This is deliberately a two-flash process so normal firmware never manipulates
the flash-connected BOOTSEL signal while Bluetooth runs on the other core.

1. Enter `RPI-RP2` mode using the BOOTSEL steps above.
2. Copy exactly one maintenance file:
   - keyboard: `pico_w_clear_keyboard_pairing_MAINTENANCE.uf2`;
   - mouse: `pico_w_clear_mouse_pairing_MAINTENANCE.uf2`;
   - both: `pico_w_clear_all_pairings_MAINTENANCE.uf2`.
3. Wait for the maintenance program to boot. Solid LED means the clear operation
   reports success; a 150 ms rapid blink reports failure. The maintenance
   indicators have not yet been physically tested.
   If power is interrupted or the rapid failure blink appears, run the
   **clear-all** maintenance UF2 next; do not simply rerun the same role image.
4. Disconnect the Pico.
5. Re-enter `RPI-RP2` with BOOTSEL.
6. Copy the normal
   `xbox-pico-v0.1.0-beta.1-pico-w.uf2` again.
7. Put only the intended replacement device into pairing mode during the new
   180-second empty-role window.

If USB detection is unclear, double-click `RUN-WINDOWS-DIAGNOSTIC.cmd` in the
`release` folder. It reads only the relevant Windows USB/volume state and saves
a small report to the Desktop; it does not change drivers or device settings.

Never leave a maintenance image installed for normal use.
