# Recovery guide

## Why BOOTSEL recovery remains available

The RP2040's USB mass-storage boot path is in immutable on-chip ROM. The Pico
SDK documentation and Raspberry Pi's official getting-started instructions
describe holding BOOTSEL while connecting USB to expose `RPI-RP2`. Ordinary
UF2 application firmware is written to external flash and cannot overwrite the
ROM bootloader.

Authoritative references:

- [RP2040 datasheet, boot sequence and USB boot](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Raspberry Pi Pico getting started: BOOTSEL and UF2](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html#resetting-flash-memory)
- [Pico W datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf)

This firmware does not write boot ROM, OTP, security fuses, debug locks, or
clock/voltage settings. It does not call runtime ROM USB boot. Physical damage
to the board, connector, cable, BOOTSEL button, or power circuitry is outside
what software recovery can fix.

## Firmware does not boot or USB is not recognized

1. Disconnect the Pico W.
2. Hold BOOTSEL.
3. Reconnect it directly to a PC with a known USB data cable.
4. Release BOOTSEL after connection.
5. Confirm `RPI-RP2` appears.
6. Copy the correct normal Pico W UF2 from `release`.

If `RPI-RP2` appears, the ROM recovery path is working even if the application
was wrong or corrupt. If it does not appear, try a known cable and another PC
port before suspecting hardware.

## Wrong firmware or corrupted application

Repeat the BOOTSEL procedure and copy
`xbox-pico-v0.1.0-beta.1-pico-w.uf2`. There is no need to erase
all flash first. Do not use a flash-nuke image as a routine repair step because
it erases bonding state and adds risk without helping a normal UF2 overwrite.

## Bad pairing state

Use the smallest applicable maintenance image, then reflash the normal image:

- keyboard role only;
- mouse role only; or
- both roles.

The maintenance code deletes the application authorization record first, then
removes its matching BTstack identity key when the record is valid. If power is
lost between those operations, an orphan bond can remain, but normal firmware
has no authorization record and will not reconnect to it. If power is lost,
the Pico is unplugged, or the role-clear image reports failure, run the
**clear-all** maintenance UF2 before reflashing normal firmware. Do not rely on
rerunning only the same role-clear image: its role tag may already be gone and
can no longer identify the orphan database entry.

A malformed role record can be deleted, but it cannot safely identify which
BTstack key belonged to it. Use the clear-all maintenance UF2 when deterministic
recovery matters; it removes and verifies every BLE database entry instead of
guessing which unrelated key belonged to the damaged tag.

## Console does not recognize the device

Return to the PC test stages. Confirm the PC enumerates one keyboard interface
and one mouse interface and that both peripherals work independently. Do not
keep reflashing, erase the console, alter Xbox storage, or try controller
firmware. Xbox/game compatibility may be the limitation even when the Pico and
PC tests pass.

## Power safety

For this project, power the Pico W only through Micro-USB. Do not connect a GPIO
5 V supply, VSYS supply, bench supply, or a second USB power source at the same
time. The firmware requests a standard USB 2.0 configured-current maximum and
does not overclock or change voltage.
