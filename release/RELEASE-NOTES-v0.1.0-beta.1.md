# Pico W Bluetooth keyboard and mouse bridge v0.1.0-beta.1

First public beta of the dual BLE HID bridge for the **original Raspberry Pi
Pico W (RP2040)**.

## Tested result

- Logitech MX Mechanical keyboard and Logitech M196 mouse simultaneously
- Windows 11 keyboard and mouse input
- Xbox Series X keyboard navigation
- The Sims 4 on Xbox Series X keyboard and mouse input

Xbox mouse support is game-specific. The mouse is not expected to navigate the
Xbox Home dashboard.

## First installation

1. Download `pico-w-bluetooth-keyboard-mouse-v0.1.0-beta.1.uf2`.
2. Unplug the Pico W.
3. Hold BOOTSEL while connecting it to a Windows PC.
4. Release BOOTSEL when the `RPI-RP2` drive appears.
5. Copy the normal UF2 to `RPI-RP2`; it disappears and reboots automatically.
6. Do not add the peripherals in Windows Bluetooth settings.
7. Put the keyboard into pairing mode. When the Pico repeats three short
   flashes and a pause, type `739241` on that keyboard and press Enter.
8. Put the BLE mouse into its documented pairing mode.
9. Solid green on the Pico means both roles are connected. Test on the PC
   before moving the Pico to another USB host.

The enrollment window lasts three minutes. A normal power cycle reopens it for
an empty role and does not erase an existing pairing.

## Replacing a paired device

Flash the matching `MAINTENANCE` UF2, wait for its solid success LED, then flash
the normal `pico-w-bluetooth-keyboard-mouse-v0.1.0-beta.1.uf2` again and pair the replacement.

- `pico_w_clear_keyboard_pairing_MAINTENANCE.uf2`: keyboard only
- `pico_w_clear_mouse_pairing_MAINTENANCE.uf2`: mouse only
- `pico_w_clear_all_pairings_MAINTENANCE.uf2`: both roles, or recovery after an
  interrupted clear

Maintenance images erase state; they do not run the bridge. Never leave one
installed for normal use.

## Verify and read first

Compare downloads with `SHA256SUMS.txt`. Read `START-HERE.txt` for the compact
beginner guide and `LICENSE.TXT` before redistribution or modification.

This non-commercial derivative is built from Shiomachi Software's
[`picow_ble_usb_hid_bridge`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge)
at tag `20260830_7` / commit `2c6a303`, with upstream history and notices
preserved. See the repository's `NOTICE.md` for the detailed lineage.
