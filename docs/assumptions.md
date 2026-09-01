# Assumptions and evidence ledger

Labels mean:

- **VERIFIED** — demonstrated by source inspection, build, or automated test;
- **DOCUMENTED** — stated by an authoritative primary source;
- **INFERRED** — reasoned from evidence but not physically demonstrated here;
- **UNVERIFIED** — requires the user's hardware or environment.

| Assumption / claim | Status | Evidence or consequence |
|---|---|---|
| Baseline was imported without local changes | VERIFIED | Upstream commit `2c6a303d1f172e56b271283af978efdcc483a389` was built before modification. |
| Pico W target has RP2040, 264 KiB SRAM, 2 MiB flash, CYW43439, and USB 1.1 controller/PHY | DOCUMENTED | Raspberry Pi Pico W and RP2040 datasheets. |
| Pico W can normally recover through `RPI-RP2` BOOTSEL ROM after bad application firmware | DOCUMENTED | RP2040 boot ROM and Raspberry Pi getting-started documentation. |
| Normal firmware does not access ROM boot, OTP, fuses, debug lock, voltage, or overclock controls | VERIFIED | Source/static inspection. |
| BTstack build has two HCI, GATT, and HIDS client pool entries | VERIFIED | `src/btstack_config.h` and clean build. |
| Pinned BTstack represents HIDS clients per connection/CID | VERIFIED | `hids_client.c/.h` source inspection. |
| Two active HOGP links work reliably through CYW43439 on a physical Pico W | UNVERIFIED | Source capacities and vendor information support feasibility; RF/controller behavior requires hardware. |
| Serialized HIDS discovery avoids overlapping shared Report Map allocation | VERIFIED | Connection manager permits one active connect/security/discovery operation; pinned allocator was inspected. |
| Static USB topology is two HID interfaces, endpoints `0x81` and `0x82`, total configuration length 59 | VERIFIED | Compile-time assertions, source inspection, cross-build. Physical enumeration still unverified. |
| TinyUSB supports the chosen two-interface HID topology | DOCUMENTED | Pinned TinyUSB macros/examples and successful compilation. |
| A normal PC accepts and uses this exact descriptor topology | UNVERIFIED | Requires descriptor capture and functional PC test. |
| Xbox Series X accepts this exact composite HID device | UNVERIFIED | Microsoft documents USB keyboard/mouse use, but topology and console-version behavior need testing. |
| A particular Xbox game accepts keyboard and mouse input | UNVERIFIED | Title-specific behavior is outside firmware control. |
| MX Mechanical and the separate inexpensive mouse are BLE HOGP devices with one supported HIDS service and compatible maps | PARTIAL / UNVERIFIED | Logitech documents direct BLE support, and an empirical MX Mechanical keyboard-interface descriptor is accepted and normalized in a regression test. The direct BLE Report Map/topology and physical device remain untested; the mouse model remains unspecified. The G502 LIGHTSPEED is not a candidate BLE mouse. |
| Supported Secure Connections association can pair the chosen peripherals | UNVERIFIED | Host-displayed passkey entry uses fixed code `739241`; no-input/no-output mice use Just Works. Numeric Comparison, Pico-input passkey, legacy pairing, and proprietary transports remain unsupported. |
| Report compiler rejects malformed/oversized inputs without allocation or overflow | VERIFIED | Host tests, strict compiler diagnostics, and source review. |
| Disconnect release barriers cannot be displaced by queued state | VERIFIED | Mailbox unit tests cover release priority, stale generations, and completion tokens. |
| USB transfer completion and failure callbacks behave as expected on RP2040 | DOCUMENTED / UNVERIFIED | Pinned TinyUSB API was inspected; physical endpoint behavior needs hardware. |
| Bridge-added post-notification scheduling is normally under one 1 ms USB frame | INFERRED | Parsing is bounded and mailbox-only; actual latency and BLE interval require measurement. |
| Declaring 500 mA is accepted and actual Pico W draw remains within USB limits | DOCUMENTED / UNVERIFIED | Descriptor value is legal for configured USB 2.0 high-power; real pre-configured, active, and suspend current needs measurement. |
| Onboard LED timings are visible and match documentation | UNVERIFIED | Logic is compiled, but CYW43439-controlled LED was not observed. |
| Maintenance images clear real TLV/bond state correctly | VERIFIED in code / UNVERIFIED on flash | Serializer and sequencing were tested/inspected; power-cycle and flash behavior need a board. |

Primary sources and precise citations are recorded under `docs/research/`.
