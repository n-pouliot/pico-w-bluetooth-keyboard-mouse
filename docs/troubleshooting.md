# Troubleshooting

This firmware is a **public beta**. Start every investigation on a normal PC.
Do not use the Xbox as the first diagnostic environment.

## Nothing happens after copying the UF2

The `RPI-RP2` drive should disappear automatically after a valid UF2 is copied.
Wait ten seconds, then reconnect the Pico W normally without holding BOOTSEL.

If `RPI-RP2` stays mounted, eject it, disconnect the cable, and repeat the
procedure in [the flashing guide](beginner-flashing.md). Confirm that the file
name is exactly:

```text
pico-w-bluetooth-keyboard-mouse-v0.1.0-beta.1.uf2
```

Do not use one of the `MAINTENANCE` images as normal firmware.

## The PC does not detect a keyboard and mouse

Try the same known-good USB **data** cable and PC port used to copy the UF2.
Charge-only cables can power the board without carrying USB data. Open Windows
Device Manager and look for one composite USB device with a keyboard interface
and a mouse interface. Normal firmware must not mount `RPI-RP2`; that drive is
expected only while BOOTSEL ROM mode is active.

If the LED never changes after 15 seconds, stop and use
[the recovery guide](recovery.md). Do not connect a second power supply.

## Understanding the LED

The core ready-state patterns have been physically observed:

| Pattern | Meaning |
|---|---|
| Solid | Keyboard and mouse are both ready |
| Three short flashes, then a pause | Passkey prompt: type `739241`, then Enter |
| One 100 ms pulse every 2 seconds | Keyboard ready; mouse absent |
| Two 100 ms pulses every 2 seconds | Mouse ready; keyboard absent |
| 500 ms on / 500 ms off | Neither role is ready |
| 200 ms on / 200 ms off | Connection, security, or discovery is in progress |

After an enrollment rejection, a long pulse followed by short pulses identifies
the failed stage for 20 seconds: 1 connection, 2 security, 3 HIDS service,
4 Report Map, 5 runtime report, or 6 internal. Do not confuse the long-plus-two
diagnostic with the ordinary two-short-pulse “mouse only” state.

For a maintenance image, solid indicates that its clear operation completed.
A 150 ms rapid blink indicates failure. Reflash the normal image after a
maintenance image completes.

## A device is not discovered

- Confirm that it is a **BLE HID** device, not a proprietary 2.4 GHz-only
  receiver or Bluetooth Classic-only peripheral.
- Disconnect it from other hosts and put it into its documented BLE pairing
  mode.
- During enrollment, keep unrelated nearby HID devices out of pairing mode.
- The enrollment window lasts 180 seconds after USB configures. Power-cycle the
  Pico W to reopen it for any role that still has no saved authorization.
- For an MX Mechanical, hold the chosen Easy-Switch key for about three seconds
  until it blinks rapidly. Wait for the Pico's repeating three-short-flash
  prompt, then type `739241` on that keyboard and press Enter. Do not add it
  through Windows Bluetooth settings.
- A short Easy-Switch press only selects a slot; it does not enter pairing mode.
- If the keyboard code is not accepted, power-cycle the Pico normally to reopen
  enrollment, hold the Easy-Switch key until rapid blinking, and retry promptly.
- Numeric Comparison, a passkey that must be entered into the Pico, and OOB-only
  devices are intentionally unsupported. Legacy fallback is accepted only for
  the unauthenticated mouse role; keyboards require Secure Connections.

The firmware rejects unsupported, mixed keyboard/mouse, malformed, and
oversized HID Report Maps. Rejection is safer than forwarding unknown bytes.

## One device works and the other does not

The two roles are independent. Wake the absent peripheral, then wait several
seconds for its advertisements and the one-second reconnect backoff. Do not
clear the working role just to troubleshoot the other.

If the missing role has incorrect or stale authorization, flash only its
maintenance image, wait for the maintenance success indication, then reflash
the normal firmware:

- `pico_w_clear_keyboard_pairing_MAINTENANCE.uf2`, or
- `pico_w_clear_mouse_pairing_MAINTENANCE.uf2`.

Use `pico_w_clear_all_pairings_MAINTENANCE.uf2` only when deliberately starting
over with both devices, or whenever a role-clear operation was interrupted or
reported failure. In that partial-clear case, clear-all is required because the
role tag may be gone while its BLE database entry remains.

## Mouse works on PC but not the Xbox dashboard

This is expected. Xbox documents keyboard navigation for the console itself,
but limits mouse navigation to select games and apps:

- <https://www.xbox.com/en-US/community/for-everyone/accessibility>

Keep the Pico connected and confirm its LED is solid, then launch a game whose
Xbox Store capabilities explicitly include `Console Keyboard & Mouse`. A free,
first-party test option is Halo Infinite multiplayer:

- <https://www.xbox.com/en-US/games/store/x/9pp5g1f0c2b6>

Test mouse-look, left/right click, and the wheel inside Academy/Training Mode or
a match. Lack of a pointer on Xbox Home is not a bridge failure.

## Input appears stuck

Disconnect the Pico W from the host. A lost release report is a serious defect,
not a condition to work around. Record which peripheral, keys/buttons, and LED
pattern were present, then report the result before continuing. Do not proceed
to Xbox testing.

## UART logs

Logs are optional diagnostics, not required for a beginner's normal setup.
They use UART0 TX on GPIO 0 at 115200 baud by default and require extra hardware;
do not connect GPIO to an Xbox. If you already understand 3.3 V UART wiring,
`UART_BAUD_RATE` can be changed at build time as described in [build.md](build.md).
Never connect a 5 V UART signal to a Pico W GPIO.

## Recovery

Application mistakes do not normally overwrite the RP2040 ROM bootloader.
Use the PC-based BOOTSEL process in [recovery.md](recovery.md). Avoid flash-nuke
unless ordinary BOOTSEL reflashing works but a documented persistent-state
problem remains and the recovery guide specifically calls for it.
