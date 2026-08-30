# Troubleshooting

## LED signal interpreting

| LED       | Meaning                                                 |
|-----------|---------------------------------------------------------|
| Off       | Core 1 has not finished starting the wireless chip.     |
| Blinking  | Scanning for a device, or reconnecting to a bonded one. |
| Steady on | Discovery finished; input is being forwarded.           |

## Reading the logs

Logs go to UART0 — GPIO 0 (TX, physical pin 1) and GPIO 1 (RX, pin 2) — so you
need a USB-to-serial adapter, with its ground tied to a ground pin on the board.
The firmware only prints, so wiring the adapter's RX to GPIO 0 and the grounds
together is enough.
(Leaving the adapter's TX disconnected also avoids driving 5 V into a
3.3 V input.)

Both cores share the console, so each line is tagged with the subsystem that
wrote it: `[SYS]`, `[BLE]` or `[USB]`. Some receivers cannot keep up with the
default 115200 — a bit-banged software UART on an AVR, for instance, is
unreliable much above 38400.
So, build with a matching [`UART_BAUD_RATE`, say 9600](build.md).

## A keyboard pairs, then nothing happens

Pairing succeeds, `[BLE] Search for HID service.` is printed, and nothing
follows. The keyboard keeps flashing its pairing light, eventually sleeps, and
the link drops about 30 seconds later.

BTstack normally discovers the Client Characteristic Configuration (CCC)
descriptor with a shortcut that assumes the CCC is the last descriptor of a
characteristic. Keyboards that place a Report Reference after it — which is
common — leave the state machine unable to send the write that enables
notifications, so discovery never finishes and the security manager times out.

The firmware defines `ENABLE_GATT_LEGACY_CCC_DISCOVERY` in `btstack_config.h` to
select the older two-step discovery instead. It costs one extra round trip and
copes with descriptors in any order.

## The keyboard re-appears on the PC when the BLE link comes up

Expected. Once the bridge has the HID report descriptor of the BLE device, it
disconnects and reconnects itself so the PC re-reads that descriptor and sees
the real keyboard rather than the placeholder one.
