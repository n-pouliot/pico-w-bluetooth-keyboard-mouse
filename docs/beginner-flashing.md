# Beginner flashing guide

Start with a Windows PC. Do not make your first test on the Xbox.

## What you need

- an original Raspberry Pi Pico W;
- a USB **data** cable, not a charge-only cable;
- the cable's small Micro-USB plug for the Pico W;
- `pico_w_dual_ble_hid_bridge_PRE_HARDWARE_TEST.uf2` from the repository's
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
   `pico_w_dual_ble_hid_bridge_PRE_HARDWARE_TEST.uf2` onto `RPI-RP2`.
8. The drive should disappear automatically. The Pico reboots into the copied
   firmware; do not unplug it while the copy is in progress.
9. The onboard LED should begin a pattern. Since this has not yet been checked
   on physical hardware, stop and report the result if the drive does not
   disappear or the board behaves unexpectedly.

Copying the normal UF2 does not erase an existing role record. Use a maintenance
image only when intentionally changing pairing state.

## Enroll the peripherals on the PC

The first empty-role enrollment window lasts 120 seconds after the PC configures
USB.

1. Keep unrelated nearby keyboards and mice out of pairing mode.
2. Put only the intended BLE keyboard and BLE mouse into their documented
   pairing modes.
3. Wait for the Pico to classify and accept them. It processes candidates one
   at a time.
4. A solid LED means both are ready. One short pulse every two seconds means
   only the keyboard is ready; two pulses means only the mouse is ready.
5. If the 120-second window closes before an empty role is enrolled, unplug and
   reconnect the Pico, then put only the missing device into pairing mode.

Secure Connections Just Works devices can enroll. A device that requires a
six-digit passkey, Numeric Comparison, legacy pairing, or a display interaction
is intentionally unsupported and should be rejected.

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
   reports success; a 150 ms rapid blink reports failure. Both patterns remain
   physically unverified until the first board test.
   If power is interrupted or the rapid failure blink appears, run the
   **clear-all** maintenance UF2 next; do not simply rerun the same role image.
4. Disconnect the Pico.
5. Re-enter `RPI-RP2` with BOOTSEL.
6. Copy the normal
   `pico_w_dual_ble_hid_bridge_PRE_HARDWARE_TEST.uf2` again.
7. Put only the intended replacement device into pairing mode during the new
   120-second empty-role window.

Never leave a maintenance image installed for normal use.
