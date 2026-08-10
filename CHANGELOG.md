# Changelog

## 20260810

### Added

- Pico 2 W (RP2350) support alongside the Pico W. `PICO_BOARD` picks the target
  and defaults to `pico2_w`.
- `ENABLE_USB_LOGGING`, `ENABLE_HEARTBEAT_LOGS` and `UART_BAUD_RATE` build
  options.

### Fixed

- Keyboards that place a descriptor after the CCC descriptor no longer stall in
  GATT discovery, via BTstack's `ENABLE_GATT_LEGACY_CCC_DISCOVERY`.
- The HID report descriptor cache grew from 500 bytes to 2 KB, so descriptors
  from keyboards with media keys or multi-device switching are not truncated.
- Core 0 no longer touches the CYW43-attached status LED before Core 1 has
  finished `cyw43_arch_init()`, which could hang Core 0 during boot.
- A 12.5-15 ms connection interval is requested once the link is encrypted,
  instead of accepting the device's power-saving default.
