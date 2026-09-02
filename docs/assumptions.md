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
| Two active HOGP links work through CYW43439 on a physical Pico W | PARTIAL PASS | MX Mechanical and M196 operated simultaneously on Windows and in The Sims 4 on Xbox; extended stress remains pending. |
| Serialized HIDS discovery avoids overlapping shared Report Map allocation | VERIFIED | Connection manager permits one active connect/security/discovery operation; pinned allocator was inspected. |
| Static USB topology is two HID interfaces, endpoints `0x81` and `0x82`, total configuration length 59 | VERIFIED / PARTIAL PASS | Compile-time assertions, source inspection, cross-build, and real input passed. Full physical descriptor capture remains pending. |
| TinyUSB supports the chosen two-interface HID topology | DOCUMENTED | Pinned TinyUSB macros/examples and successful compilation. |
| A normal PC accepts and uses this exact descriptor topology | PARTIAL PASS | Windows 11 accepted simultaneous keyboard and mouse input; full descriptor capture remains pending. |
| Xbox Series X accepts this exact composite HID device | PARTIAL PASS | Keyboard navigation worked, and both keyboard and M196 mouse input worked through the Pico inside The Sims 4. Console software version and broader coverage remain unrecorded. |
| A particular Xbox game accepts keyboard and mouse input | VERIFIED for one title | The Sims 4 physically accepted both roles on Xbox Series X. Title-specific behavior remains outside firmware control. |
| MX Mechanical and Logitech M196 are BLE HOGP devices with one supported HIDS service and compatible maps | PARTIAL PASS | Both physically paired and sent their respective input through the Pico; full feature coverage and extended stress remain pending. |
| Supported security associations can pair the chosen peripherals | PARTIAL PASS | MX Mechanical fixed-code authenticated Secure Connections passed with `739241`; M196 encrypted bonded Level 2 pairing passed. Numeric Comparison, Pico-input passkey, OOB, and proprietary transports remain unsupported. Legacy fallback is allowed only for mice. |
| Report compiler rejects malformed/oversized inputs without allocation or overflow | VERIFIED | Host tests, strict compiler diagnostics, and source review. |
| Disconnect release barriers cannot be displaced by queued state | VERIFIED | Mailbox unit tests cover release priority, stale generations, and completion tokens. |
| USB transfer completion and failure callbacks behave as expected on RP2040 | DOCUMENTED / UNVERIFIED | Pinned TinyUSB API was inspected; physical endpoint behavior needs hardware. |
| Bridge-added post-notification scheduling is normally under one 1 ms USB frame | INFERRED | Parsing is bounded and mailbox-only; actual latency and BLE interval require measurement. |
| Declaring 500 mA is accepted and actual Pico W draw remains within USB limits | DOCUMENTED / UNVERIFIED | Descriptor value is legal for configured USB 2.0 high-power; real pre-configured, active, and suspend current needs measurement. |
| Onboard LED timings are visible and match documentation | PARTIAL | The onboard green LED works and showed operational patterns during keyboard/mouse testing; exact timing counts were not measured. |
| Maintenance images clear real TLV/bond state correctly | VERIFIED in code / UNVERIFIED on flash | Serializer and sequencing were tested/inspected; power-cycle and flash behavior need a board. |

Primary sources and precise citations are recorded under `docs/research/`.
