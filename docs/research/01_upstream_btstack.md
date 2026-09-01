# Researcher A: upstream architecture and BTstack multi-link feasibility

Date: 2026-09-01  
Scope: read-only analysis of upstream baseline `2c6a303d1f172e56b271283af978efdcc483a389`; no firmware implementation, build, commit, or hardware test.

## Executive conclusion

**SOURCE-VERIFIED:** BTstack is architected to hold multiple simultaneous LE, GATT, and HIDS client/host contexts. In the Pico SDK 2.2.0 dependency, `hids_client.h` explicitly says that its descriptor storage is shared by all connections and that the connection count is controlled by `MAX_NR_HIDS_CLIENTS`. The implementation keeps a linked list of clients, allocates one `hids_client_t` per HCI handle, gives each a distinct HIDS CID and callback, and routes notifications by HCI handle. The current BTstack 1.8.2 API retains the same design under the renamed `hids_host` API and `MAX_NR_HIDS_HOSTS`.

**DOCUMENTED, NOT HARDWARE-VERIFIED HERE:** CYW43439 firmware is capable of more than two LE links according to an Infineon moderator (theoretical link-management maximum 15, with the practical count determined by connection-event scheduling). A Raspberry Pi maintainer separately states that the host-side limit is the smaller of `MAX_NR_HCI_CONNECTIONS` and `MAX_NR_GATT_CLIENTS`, while noting that the CYW43439 public datasheet does not publish a hard practical number. Two low-bandwidth HID links are therefore technically credible, but this repository has no dual-link hardware evidence.

**BLOCKED IN THE BASELINE:** the application itself is strictly single-device. It has one application state, bonded address, target address, connection handle, HIDS CID, protocol mode, connection timer, descriptor view, USB re-enumeration flag, and HID report stream. It also compiles only one GATT client and one HIDS client. Every relevant callback either uses those globals or discards the connection discriminator supplied by BTstack. Merely changing the two pool constants would corrupt routing and lifecycle state.

**IMPORTANT UPSTREAM CAVEAT:** BTstack's descriptor byte store is global and accounts only for bytes already written, not space reserved for an in-progress client. If two HIDS discoveries interleave before either report map has accumulated bytes, both can be assigned the same offset. Multiple HID service instances within one peer have the same overlap risk. Dual-HIDS discovery must therefore be serialized, or the allocator must be fixed/replaced. Active links may remain simultaneous after serialized discovery.

Overall feasibility assessment: **feasible for an engineered, bounded two-device design; not feasible as a constants-only modification; not yet verified on Pico W hardware.**

## Evidence versus inference labels

- **SOURCE-VERIFIED**: observed directly in the baseline or pinned upstream source.
- **DOCUMENTED**: stated by an official API/reference or vendor/maintainer source.
- **INFERRED**: engineering conclusion from source behavior; requires a focused test.
- **UNVERIFIED**: requires a build, protocol trace, or physical hardware.

## Versions and provenance

| Component | Baseline/intended version | Exact evidence | Current authoritative version checked |
|---|---:|---|---:|
| Starting project | commit `2c6a303d1f172e56b271283af978efdcc483a389` (`Minor docs fix`) | The local `HEAD` and public upstream `main` both resolved to this commit on 2026-09-01. [Tree](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/tree/2c6a303d1f172e56b271283af978efdcc483a389) | same baseline |
| Pico SDK | intended `2.2.0`, commit `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` | `src/CMakeLists.txt` sets VS Code metadata `sdkVersion 2.2.0`; the project does **not** lock `PICO_SDK_PATH` or a fetch tag. [SDK tag tree](https://github.com/raspberrypi/pico-sdk/tree/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779) | `2.3.0`, commit `98a542c1a62fb549ffb5d66a3e5892b06276b670`, released 2026-07-03. [Release](https://github.com/raspberrypi/pico-sdk/releases/tag/2.3.0) |
| BTstack bundled by SDK | submodule commit `501e6d2b86e6c92bfb9c390bcf55709938e25ac1` (2025-01-14), described by the SDK release as the 1.6.2 generation | [Pinned BTstack tree](https://github.com/bluekitchen/btstack/tree/501e6d2b86e6c92bfb9c390bcf55709938e25ac1) | SDK 2.3.0 pins BTstack `v1.8.2`, commit `075a0780f0fad7ff67d58ac19f46e8953656a752`. [Tag](https://github.com/bluekitchen/btstack/tree/v1.8.2) |
| TinyUSB bundled by SDK | commit `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`, tag `0.18.0` | [Pinned TinyUSB tree](https://github.com/hathach/tinyusb/tree/86ad6e56c1700e85f1c5678607a762cfe3aa2f47) | SDK 2.3.0 still pins the same `0.18.0` commit |
| CYW43 driver bundled by SDK | commit `dd7568229f3bf7a37737b9e1ef250c26efe75b23` | [Pinned driver tree](https://github.com/georgerobotics/cyw43-driver/tree/dd7568229f3bf7a37737b9e1ef250c26efe75b23) | SDK 2.3.0 pins `055d64274b014dd7b1c2fc94d26e8a18face7124` |
| Toolchain metadata | `14_2_Rel1`; picotool `2.2.0-a4` | Baseline [`src/CMakeLists.txt`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/CMakeLists.txt#L18-L20) | no local installed toolchain was available to verify a build |

The dependency qualification above is the intended SDK 2.2.0 graph, not proof of how the committed UF2 files were built. The import file accepts any caller-provided `PICO_SDK_PATH`, and if `PICO_SDK_FETCH_FROM_GIT` is enabled without a tag it selects `master`. Reproducible future work needs an explicit SDK version/commit check.

The CMake default is `pico2_w`; an original Pico W build requires `-DPICO_BOARD=pico_w`. This does not prevent Pico W support, but a release pipeline must select and record the board explicitly.

## Actual baseline execution path

### 1. Startup and core ownership

**SOURCE-VERIFIED:** [`main()`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/main.c#L51-L74) performs this sequence:

1. `board_init()`.
2. `tud_init(BOARD_TUD_RHPORT)` before Bluetooth is ready. The host initially enumerates the fallback TinyUSB report descriptor.
3. `stdio_init_all()` and the cross-core HID queue/critical section initialization.
4. `flash_safe_execute_core_init()` on Core 0 so Core 1's later BTstack TLV flash mutations can cooperatively lock out the other core.
5. Launch `ble_host_main()` on Core 1.
6. Keep Core 0 permanently in `usb_dev_main()`, calling `tud_task()` and attempting one queued HID report on every loop.

Core 1 calls `cyw43_arch_init()`, which initializes the SDK's BTstack/CYW43 integration, then invokes the application `btstack_main()` and BTstack run loop. Pico SDK 2.2.0's [`btstack_cyw43.c`](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_cyw43_driver/btstack_cyw43.c#L28-L66) initializes BTstack memory, the async-context run loop, HCI transport, the global flash TLV instance, and the LE device database.

### 2. Discovery

**SOURCE-VERIFIED:** when BTstack reaches `HCI_STATE_WORKING`, `hog_start_connect()` loads one custom `HOGD` TLV address record and starts a passive, 100% duty-cycle scan (`interval = window = 48`, or 30 ms). There is no direct connection attempt to the stored identity; reconnect is advertisement-driven.

For each advertising report, the application:

1. Normalizes public/random identity address types to their low bit.
2. Treats the advertisement as HID if it contains service UUID `0x1812` **or any Appearance from `0x03C0` through `0x03CF`**.
3. If any bond exists and the advertising address is an RPA, starts an asynchronous Security Manager address-resolution lookup and caches only `{RPA, has_hid_service}`.
4. Otherwise connects on a direct address match with the one stored bond.
5. Otherwise connects to the first advertisement classed as HID, even when a different device is stored.

The source is [`GAP_EVENT_ADVERTISING_REPORT`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/hog_host_demo.c#L304-L349) and [`adv_event_contains_hid_service()`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/hog_host_demo.c#L779-L801).

**DOCUMENTED:** Bluetooth Assigned Numbers distinguishes Generic HID (`0x03C0`), Keyboard (`0x03C1`) and Mouse (`0x03C2`). The baseline discards that distinction by accepting the entire range. [Bluetooth SIG Assigned Numbers](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/index-en.html)

**Consequence:** discovery identifies only “some HID,” not keyboard versus mouse. A third nearby HID, joystick, gamepad, pen, or generic HID can be selected. Device names are not used, which is good, but there is no Report Map classification stage.

### 3. Physical LE connection, security, and HIDS connection

`hog_connect()` sets the one global state to `W4_CONNECTED`, arms the one connection timer for 3 seconds, and calls `gap_connect()` for the one global target. On LE Connection Complete, the handler records one global HCI handle, sets `W4_ENCRYPTED`, and requests pairing.

Security is configured as:

- IO capability: `NO_INPUT_NO_OUTPUT`.
- requirements: Secure Connections plus bonding.
- Just Works requests: automatically confirmed.
- Numeric comparison: automatically confirmed while merely printing the number to UART.
- Display-passkey events: printed to UART; there is no passkey input handler.

Pairing or re-encryption success asks for a 12.5–15 ms connection interval, zero peripheral latency, and 3-second supervision timeout, then calls `hids_client_connect()` with Report Protocol.

**Security limitation:** a keyboard that requires the host to display a passkey for entry on the keyboard has no normal no-soldering UX. UART logs require external wiring, and automatic numeric-comparison confirmation is not meaningful user confirmation on a device without a display. Supported pairing methods must be stated as a compatibility constraint, not assumed universal.

### 4. HIDS discovery and Report Map retrieval

The baseline initializes the HIDS client once with a 2048-byte global descriptor buffer. BTstack then, per HCI connection:

1. Discovers up to three primary HID Service instances.
2. Discovers each service's characteristics.
3. Reads each Report Map characteristic with the GATT long-value procedure.
4. Discovers external Report Reference descriptors.
5. Discovers Report characteristics and reads their Report Reference descriptors to obtain Report ID and report type.
6. Enables notifications on input reports.
7. Emits `GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED` with HIDS CID, status, protocol mode and number of service instances.

Pinned source: [`hids_client.c`](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.c#L680-L852) and [`hids_client.h`](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.h#L61-L210).

The project exposes only `hids_client_descriptor_storage_get_descriptor_data(hids_cid, 0)` and length for **service index 0**. Thus a peer with multiple HID Service instances can produce notifications from service 1 or 2 while USB advertises only service 0's map. The BTstack event supplies `service_index`; the project discards it.

### 5. Notification forwarding

BTstack notification routing is connection-aware internally. It finds the HIDS client by the notification's HCI handle, finds the Report characteristic by value handle, creates a HIDS report event containing HIDS CID, service index, Report ID and report bytes, then invokes that client's callback. See [`handle_notification_event()`](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.c#L612-L639).

The application throws away the callback `channel`, does not read the event's HIDS CID, ignores `service_index`, and copies only the returned report bytes to the single global queue. The queue record has a `report_id` member, but Core 0 never uses it.

There is an important pass-through nuance: BTstack's HIDS client explicitly inserts the characteristic's Report ID as the first byte and increases the exposed report length by one ([source](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.c#L555-L575)). The baseline sends that buffer verbatim with `tud_hid_report(0, ...)`. Therefore the README's “byte for byte” statement is accurate only after BTstack's transformation, not relative to the original GATT notification. For a report map with no `Report ID` item and a Report Reference ID of zero, the inserted leading zero is **inferred to be an invalid extra USB payload byte**. This needs descriptor/report tests against real no-ID devices.

### 6. TinyUSB initialization and descriptors

The fallback HID report descriptor has keyboard, mouse, consumer-control and gamepad collections on one HID interface, using Report IDs 1–4. Once BLE HIDS discovery succeeds, the report-descriptor callback instead returns the remote service-index-0 map, and the dynamic configuration descriptor advertises its length.

Current USB topology:

- one configuration;
- one HID interface, subclass/protocol `NONE`;
- one interrupt IN endpoint, `0x81`;
- no interrupt OUT endpoint;
- 64-byte HID endpoint buffer and packet size;
- 1 ms polling interval;
- remote-wakeup attribute;
- advertised maximum power 500 mA;
- direct TinyUSB device use; CDC, MSC, MIDI and vendor classes disabled.

On first HIDS-ready, Core 1 sets `g_usb_reinit_request`. Core 0 disconnects from USB if mounted, waits 100 ms, clears all queued input, and reconnects so the host requests the now-dynamic report descriptor. There is no descriptor change on later disconnect; the USB host retains the last dynamic descriptor.

TinyUSB 0.18.0 copies reports into a fixed `CFG_TUD_HID_EP_BUFSIZE` buffer and rejects an oversized copy ([`tud_hid_n_report`](https://github.com/hathach/tinyusb/blob/86ad6e56c1700e85f1c5678607a762cfe3aa2f47/src/class/hid/hid_device.c#L113-L131)). The application queue accepts and retries up to 512 bytes. Reports over 64 bytes cannot be forwarded by the current USB configuration and may leave the queue head permanently unsent. Any bridge design must validate the report's USB wire size before enqueueing and define a strict supported maximum.

### 7. Bonding and TLV persistence

Two distinct persistence layers exist:

1. Pico SDK configures BTstack's LE device database on the global flash TLV. It stores identity address/type, IRK, LTK and associated security metadata in `BTD<index>` tags. The baseline permits 16 entries.
2. The application stores exactly one `le_device_addr_t` (address plus type) under custom tag `HOGD`. This is only a reconnect selector; it is not the bond keys.

The SDK flash backend reserves two 4 KiB sectors near the end of Pico W's 2 MiB flash (8 KiB total, one 4 KiB bank active at a time), appends TLV records, and migrates live records to the other bank when needed. Sources: [`btstack_flash_bank.h`](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_btstack/include/pico/btstack_flash_bank.h#L18-L40), [`btstack_flash_bank.c`](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_btstack/btstack_flash_bank.c), and [`le_device_db_tlv.c`](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/le_device_db_tlv.c#L48-L180).

On key-missing or authentication failure, the baseline deletes `HOGD`, calls `gap_delete_bonding()` for the target and stored bond, and clears its only bond flag. In a two-device bridge this policy would incorrectly erase an unaffected keyboard because a mouse failed, or vice versa.

### 8. Reconnect and disconnect behavior

At boot and after every disconnect, the application reloads `HOGD` and scans indefinitely. Scan is restarted every 5 seconds. A physical disconnect, regardless of handle, invalidates the one global handle, clears all pending RPA lookup records, changes the one global state away from `READY`, waits 300 ms, and restarts the complete connection loop.

Notably, disconnect does **not**:

- clear the USB queue immediately;
- emit all-keys-up or all-buttons-up;
- reset `hids_cid`;
- force USB back to the fallback descriptor;
- distinguish an HIDS-service teardown from the physical link teardown;
- preserve an unaffected second link (none can be represented).

For dual input devices, disconnect must be routed by HCI handle/HIDS CID and scoped to one device context. The USB-side canonical state for that device must be released immediately to prevent stuck keys/buttons while the other device continues.

### 9. BOOTSEL and firmware recovery

**SOURCE-VERIFIED:** the repository has no BOOTSEL read, no runtime BOOTSEL command, no pairing action bound to BOOTSEL, and no recovery implementation or instructions. Because it uses TinyUSB directly and disables USB stdio, it also does not inherit the SDK's default USB-stdio reset-to-BOOTSEL mechanism.

**DOCUMENTED:** physical BOOTSEL recovery is independent of this application. Raspberry Pi documents that BOOTSEL mode resides in read-only on-chip ROM, cannot be rewritten, and presents `RPI-RP2` when BOOTSEL is held while connecting an RP2040 Pico. This prevents ordinary application firmware from permanently bricking the board through software. [Official Raspberry Pi documentation](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html#reset-flash-memory)

Runtime use of the physical BOOTSEL button is not an ordinary GPIO read. Raspberry Pi's official `picoboard/button` example describes temporarily suspending flash access. In this multicore firmware, Core 1 also owns BTstack and may mutate TLV flash, so a naive BOOTSEL polling routine would be unsafe. If a later pairing UX uses BOOTSEL, it must use the documented flash-safe/multicore mechanism and be tested alongside CYW43 and TLV activity; otherwise leave BOOTSEL exclusively for ROM recovery.

## Singleton/global state and callbacks

### Application-owned global/singleton state

| Location | State | Why it blocks two devices |
|---|---|---|
| `hog_host_demo.c` | `app_state` | One linear connection lifecycle cannot represent two independent links. |
| | `bonded_device`, `has_bonded_device` | Only one reconnect identity/validity flag. |
| | `target_device` | Only one scan/connect candidate. |
| | `connection_handle` | Pairing, parameter update, disconnect and error paths all target one link. |
| | `hids_cid` | Descriptor access and HIDS operations target one client. |
| | `protocol_mode` | Global rather than per peer/service. |
| | `pending_rpa_lookups[4]`, round-robin index | Entries lack target slot/bond identity; capacity exceeds SM's configured three lookup entries; return status is ignored. |
| | `hid_descriptor_storage[2048]` | Shared BTstack store; unsafe for interleaved discovery and not partitioned per role. |
| | `connection_timer` | Reused for scan refresh, connect timeout and reconnect delay. One outgoing LE create procedure can be serialized, but independent per-slot backoff cannot be represented. |
| | `led_timer` and local `led_state` | LED reflects only one global `READY` bit. |
| | HCI and SM callback registrations | Registrations may remain singleton, but handlers must route each event by handle/address. |
| | global TLV implementation/context pointers | The TLV service is intentionally singleton; this is acceptable, but records need two application slots. |
| `main.c` | `g_usb_reinit_request` | One volatile flag cannot describe which USB mapping changed; it is also not a C memory-model synchronization primitive. |
| | USB reinit static state/timestamp | One device's descriptor change disconnects the complete USB device. |
| | static send scratch report | Acceptable for one Core-0 sender, but queue records need source/USB destination metadata. |
| `Common.c` | one HID queue, one static 32-record array, one critical section | A shared queue can remain, but each item currently lacks device slot/interface and validated canonical type. |
| `usb_descriptors.c` | one remote descriptor callback view, one dynamic configuration buffer, one string buffer | Only one HID instance and service-index-0 descriptor can be represented. Descriptor reads race Core 1's HIDS list/store mutation. |
| `bt_example_common.c` | one informational HCI callback registration | Safe as a singleton; it does not own per-link state. |

Cross-core calls `is_ble_app_state_ready()` and `hids_client_descriptor_storage_get_*()` read Core-1-owned non-atomic state and linked-list-backed descriptor metadata from Core 0. `volatile` on the request flag does not make those other accesses synchronized. This is a **source-level concurrency blocker** even before adding a second client. Descriptor data should be copied into immutable application-owned snapshots and published to Core 0 through an explicit synchronized handoff.

### Callback/event routing inventory

| Callback | Current discriminator available | Baseline handling | Required dual-link handling |
|---|---|---|---|
| informational BTstack state callback in `bt_example_common.c` | stack state | log only | unchanged |
| application HCI/GAP `packet_handler` | advertising address; LE complete address/handle; disconnection handle | writes global target/handle/state; disconnection handle ignored | route advertisement to candidate/slot; associate the one active initiator with a slot; route disconnect by HCI handle |
| `sm_packet_handler` | request/pairing/re-encryption handles; identity/RPA addresses | confirms by event handle but writes/acts on global state and target | map handle/address to slot; never delete the other slot's bond |
| HIDS/GATT callback | HIDS CID in packet and callback channel; service index; report ID | callback channel ignored; CID ignored; service index ignored | map CID to device context and validate every event against current handle/generation |
| scan/connect/reconnect timer callbacks | timer source pointer | all use one global timer/state | retain one serialized initiator timer plus per-slot retry deadlines or timers |
| LED timer | global application state | blink or solid | render aggregate/pairing state without owning connection logic |
| TinyUSB mount/unmount/suspend/resume | USB device state | logs only | mostly unchanged |
| TinyUSB report-complete | HID instance and transmitted buffer | ignored | use instance if two USB HID interfaces are chosen; normal scheduler can still be queue-driven |
| TinyUSB GET_REPORT/SET_REPORT | HID instance, Report ID/type | GET stalls; SET only logs | output/LED handling needs explicit routing if later supported |

## Resource limits at the baseline

### Project and BTstack compile-time limits

| Resource | Baseline limit | Dual-link implication |
|---|---:|---|
| HCI connections | `MAX_NR_HCI_CONNECTIONS = 2` | Already permits two host-side HCI contexts; both slots are consumed by keyboard + mouse. |
| GATT clients | `MAX_NR_GATT_CLIENTS = 1` | Must be at least 2. One per LE ATT/GATT link is allocated lazily. |
| HIDS clients | `MAX_NR_HIDS_CLIENTS = 1` | Must be 2 on SDK 2.2.0. With BTstack 1.8+, the name is `MAX_NR_HIDS_HOSTS`. |
| Classic HID host connections | `MAX_NR_HID_HOST_CONNECTIONS = 1` | Irrelevant to BLE HOGP, but confirms Classic HID is not the path used. |
| SM lookup entries | 3 | Application can track four pending RPAs and ignores lookup failure. Serialize or increase with measured RAM. |
| pending application RPA lookups | 4 | Needs slot/identity context, not only address + HID boolean. |
| whitelist entries | 16 | Not currently used for reconnect. |
| in-memory LE device DB entries | 16 | Sufficient for two bonds. |
| persistent LE device DB entries | `NVM_NUM_DEVICE_DB_ENTRIES = 16` | Sufficient for two keys, but application selectors remain one. |
| controller ACL buffers | 3 | Shared by both links; likely sufficient for HID but must be measured under simultaneous reports. |
| host ACL buffers | 3 × 1024-byte payload | Shared resource; no dual-HID trace exists. |
| HCI ACL payload/chunk | 1695 bytes, 4-byte alignment | Much larger than HID reports; not the USB limit. |
| L2CAP channels/services | 4 / 3 | LE ATT uses its fixed channel; no source evidence that this pool blocks two ordinary GATT links. |
| local ATT database | 512 bytes | Local GAP name service only; not remote Report Map storage. |
| HID service instances per HIDS client | 3 | Project exports only instance 0; upstream descriptor offsets can overlap for multiple instances. |
| Report characteristics per HIDS client | 15 | Additional reports are ignored by upstream HIDS client. |
| External reports per HIDS client | 15 | Same fixed-array limit. |
| shared Report Map storage | 2048 bytes total | Not 2048 per device. Must be budgeted/partitioned or replaced; reject overflow. |

Raising the HIDS pool from one to two adds a second large fixed `hids_client_t`, which includes two arrays of 15 report records and notification listeners. A manual RP2040-layout estimate is roughly 1.1 KiB for the added HIDS object, before the second GATT/HCI contexts or descriptor bytes. This is **INFERRED**, not a map-file measurement; the final build must report actual `.bss`/RAM deltas.

### Application and USB limits

| Resource | Baseline limit/behavior | Consequence |
|---|---:|---|
| HID queue array | 32 packed records | Ring leaves one slot empty, so effective capacity is 31. |
| queue record | 515 bytes (`1 + 512 + 2`, packed) | Array alone consumes 16,480 bytes of static RAM. |
| BLE report copy cap | 512 bytes | Oversize data is silently truncated, which is unsafe semantic corruption. |
| TinyUSB HID instances | `CFG_TUD_HID = 1` | Only one USB HID interface can be opened. |
| TinyUSB EP0 | 64 bytes | Normal full-speed control endpoint size. |
| TinyUSB HID endpoint buffer | 64 bytes | Reports larger than 64 bytes cannot be sent by current code. |
| USB HID endpoints | one IN endpoint `0x81`, no OUT | Keyboard LED/output reports are not forwarded to BLE. |
| USB poll interval | 1 ms | USB scheduler contribution is bounded by the host's polling behavior after enqueue. |
| dynamic configuration buffer | one config + one HID interface/HID descriptor/endpoint | No room/topology for a second interface without redesign. |
| default requested USB power | 500 mA | Descriptor value, not measured consumption. |
| scan/connect/reconnect timers | 5 s / 3 s / 300 ms | One global lifecycle only. |
| USB re-enumeration delay | 100 ms | Disconnects the whole USB device whenever the one dynamic map is first installed. |

## BTstack multi-link and multi-HIDS feasibility

### What upstream explicitly supports

1. **HCI contexts are pooled.** `gap_connect()` allocates an `hci_connection_t` from the `MAX_NR_HCI_CONNECTIONS` pool. It forbids a second *outgoing create procedure* while one is active, but it does not forbid starting another procedure after the first link is established. [Pinned `hci.c`](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/hci.c#L8564-L8603)
2. **GATT is per HCI handle.** `gatt_client.c` keeps a linked list of GATT contexts, finds one by connection handle, and allocates a new context from `MAX_NR_GATT_CLIENTS` when needed. [Pinned `gatt_client.c`](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt_client.c#L181-L240)
3. **HIDS is per HCI handle/HIDS CID.** `hids_client_connect()` rejects only a second HIDS client on the *same* HCI handle, allocates a client from the HIDS pool, assigns a unique CID, stores the caller's callback, and adds it to a linked list. [Pinned `hids_client.c`](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.c#L1449-L1473)
4. **The API documentation is unambiguous.** SDK-era [`hids_client.h`](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.h#L214-L239) says storage is shared across all connections and the maximum is `MAX_NR_HIDS_CLIENTS`. The current official [BTstack HIDS Host API](https://bluekitchen-gmbh.com/btstack/develop/appendix/apis/#sec:hids_host_api) says the same with `MAX_NR_HIDS_HOSTS`.
5. **Disconnect is per HCI handle internally.** HIDS listens for HCI disconnection, finds the matching client, emits that CID's disconnect event, frees only that client, and compacts shared descriptor storage. [Pinned source](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.c#L1423-L1482)

### Upstream limitations that matter

#### Shared Report Map allocator overlap

`hids_client_descriptor_storage_get_available_space()` subtracts only each client's **current descriptor length**. `hids_client_descriptor_storage_init()` records an offset and maximum but does not reserve that range. Consequently:

- Client A can receive offset 0 while its descriptor length is still zero.
- Client B can also receive offset 0 before A's first bytes arrive.
- Their subsequent long-read fragments overwrite one another.

The same sequence occurs when one peer exposes multiple HID Service instances because all services are discovered and initialized before Report Maps are read. This is direct source behavior, not speculation: [allocator source](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.c#L132-L204).

BTstack 1.8.2's renamed [`hids_host.c`](https://github.com/bluekitchen/btstack/blob/075a0780f0fad7ff67d58ac19f46e8953656a752/src/ble/gatt-service/hids_host.c#L148-L207) retains the same algorithm. Upgrading the SDK does not remove this constraint.

Safe choices are:

- serialize complete HIDS discovery and require exactly one HID Service instance per supported peripheral;
- copy the completed map into a bounded, application-owned per-device buffer before starting the next HIDS discovery; and
- reject/disconnect maps exceeding the per-device policy; or
- patch upstream storage to reserve fixed per-client/per-service regions and test compaction/disconnect thoroughly.

The first option is the smallest safe starting point for two ordinary keyboard/mouse peers.

#### No upstream dual-HOG demo or hardware claim

Both the pinned and current BTstack `hog_host_demo.c` use one remote address, one HCI handle and one HIDS CID. The API supports multiple clients, but the example does not demonstrate them. No dual-HIDS Pico test is present in the inspected upstream tree.

Infineon's accepted vendor-forum answer says CYW43439 firmware theoretically permits 15 LE links, with the usable count governed by intervals and event lengths. [Infineon answer](https://community.infineon.com/t5/AIROC-Wi-Fi-and-Wi-Fi-Bluetooth/cyw43439-max-BLE-connection-count/td-p/399375) Raspberry Pi's own issue answer is more conservative and says the datasheet gives no hard number, while host-side BTstack limits are configurable. [Raspberry Pi issue #313](https://github.com/raspberrypi/pico-feedback/issues/313)

Therefore:

- **DOCUMENTED/SOURCE-VERIFIED:** host stack structures can represent two links and two HIDS clients.
- **DOCUMENTED BY VENDOR STAFF:** CYW43439 firmware's link-management ceiling is above two.
- **INFERRED:** two low-throughput HID links at sane intervals should fit radio scheduling and three ACL buffers.
- **UNVERIFIED:** simultaneous keyboard + mouse stability, latency, reconnect and pairing on an actual Pico W with this firmware/firmware blob.

### SDK 2.3.0 migration note

BTstack 1.8 renamed “HIDS Client” to “HIDS Host”:

- `hids_client.*` → `hids_host.*`;
- `hids_client_*` → `hids_host_*`;
- `MAX_NR_HIDS_CLIENTS` → `MAX_NR_HIDS_HOSTS`.

Pico SDK 2.3.0 conditionally includes the new source. A future SDK upgrade therefore needs source/API migration; carrying `MAX_NR_HIDS_CLIENTS = 2` unchanged would no longer configure the intended pool.

## Concrete blockers and required refactors

| ID | Blocker | Evidence | Required refactor |
|---|---|---|---|
| A1 | Single application lifecycle | one `app_state` and one handle/CID | Introduce two explicit device contexts plus a small global radio orchestrator. |
| A2 | Callback discriminators discarded | HCI handle, SM handle, HIDS CID/channel and service index are available but ignored | Route every event to a context by HCI handle, HIDS CID, identity/RPA, and a connection-generation token. Ignore stale events safely. |
| A3 | Pools permit only one GATT/HIDS client | `MAX_NR_GATT_CLIENTS = 1`, `MAX_NR_HIDS_CLIENTS = 1` | Set each to 2 for SDK 2.2.0 (or `MAX_NR_HIDS_HOSTS = 2` on 1.8+), retain `MAX_NR_HCI_CONNECTIONS = 2`, and measure RAM/map deltas. |
| A4 | Only one outgoing LE create procedure is allowed at a time | `gap_connect()` returns command-disallowed while an LE create request is active | Serialize scan selection and connection attempts. Simultaneous *active* links do not require simultaneous create procedures. |
| A5 | Shared descriptor allocator can overlap in-progress maps | pinned and current BTstack source | Serialize HIDS discovery, enforce one HID Service instance initially, and copy completed maps to fixed per-device buffers; or patch allocator. |
| A6 | Only service-index-0 map is exposed; report service index is discarded | project getters hardcode `0` | Reject unsupported multi-service peers initially, or build a validated composite/canonical mapping that retains service/report identity. |
| A7 | Raw report stream has no source identity | queue item lacks slot/CID/interface | Add source slot and validated destination/report type to queue records; never infer from global state. |
| A8 | Report ID transformation and no-ID format are not validated | BTstack prepends ID; TinyUSB is called with ID 0 | Parse/validate Report Maps and construct the exact USB wire report. Add tests for Report ID 0, nonzero IDs, short/long packets and multiple reports. |
| A9 | 512-byte BLE queue versus 64-byte USB endpoint | project and TinyUSB source | Define a supported maximum, reject rather than truncate, and ensure no unsendable record can block the queue. |
| A10 | One custom bond selector | one `HOGD` tag and global deletion policy | Store versioned keyboard and mouse slot records independently; preserve BTstack key DB; delete only the selected slot's bond after verified identity mapping. |
| A11 | Discovery does not classify keyboard/mouse | all HID appearances accepted; map not parsed | Treat Appearance as a hint. Classify only after Report Map validation using HID usages; reject ambiguous combo/unsupported devices or define an explicit policy. |
| A12 | One failure resets all state | global disconnect/reconnect path | Keep the unaffected link and USB state operating; retry only the failed slot with bounded backoff. |
| A13 | No stuck-input release | disconnect does not emit release state | Clear and send canonical all-released state for that device on every terminal/error/re-pair path. |
| A14 | Cross-core descriptor/list access is unsynchronized | Core 0 calls Core-1-owned HIDS getters | Publish immutable descriptor/classification snapshots through a lock/queue/barrier. Do not traverse BTstack state from Core 0. |
| A15 | USB descriptor passthrough is singular and re-enumeration is global | one HID instance and one dynamic map | The USB architecture workstream must choose fixed canonical descriptors or two validated fixed interfaces. Concatenating two arbitrary remote maps is not safe because Report IDs/usages may collide. |
| A16 | Pairing UX cannot support all keyboards | no input/display, UART-only passkey log | Define supported association models; do not silently auto-confirm methods that require user verification. |
| A17 | Runtime BOOTSEL would contend with flash/CYW work | button is QSPI-related; no current implementation | Keep BOOTSEL recovery-only unless a documented flash-safe, multicore-tested runtime reader is designed. |

### Minimum device context

The concrete names can change, but each keyboard/mouse slot needs ownership of at least:

- intended role and classification status;
- persisted identity address/type and record validity/version;
- current advertising/RPA address and pending resolution state;
- HCI connection handle and connection generation;
- HIDS CID and protocol/service count;
- pairing/encryption/security state;
- bounded application-owned Report Map and parsed report metadata;
- notification/report routing state;
- retry deadline/backoff and last failure;
- canonical USB key/button state for forced release;
- enrollment/replacement state.

Global state should be limited to resources that are genuinely global: one scanner, one active initiator, one BTstack run loop, one TLV backend, one USB device, and aggregate LED/UI state.

## Recommended feasibility constraints for the first implementation

These are design recommendations, not firmware changes made by this workstream:

1. Use BLE HOGP only; do not mix Classic HID into the initial two-link design.
2. Permit exactly two slots: one keyboard and one mouse.
3. Serialize connection creation and full HIDS discovery. Keep both links active after each reaches ready.
4. Initially accept exactly one HID Service instance per peer.
5. Bound each Report Map (for example, a reviewed limit per slot) and reject overflow without truncation.
6. Parse Report Maps before classification or forwarding. Appearance may prioritize candidates but must not authorize byte interpretation.
7. Copy parsed descriptor metadata to application-owned per-slot storage; do not expose BTstack's mutable shared store to Core 0.
8. Keep USB descriptors fixed across Bluetooth reconnects if the USB architecture workstream can support canonical translation. This avoids global USB re-enumeration when one radio link changes.
9. Preserve an unaffected slot through failure, pairing, sleep and reconnect of the other.
10. Treat the dual-link radio/controller claim as a hardware release gate: simultaneous reports, sleep/wake, disconnect/reconnect and pairing must be exercised on original Pico W.

## Open questions requiring implementation or hardware evidence

- Exact RP2040 RAM/flash deltas for two HCI + two GATT + two HIDS contexts under the selected SDK and warning flags.
- Whether three controller/host ACL buffers sustain the selected connection intervals under simultaneous high-rate mouse and keyboard input without starvation.
- Actual CYW43439 scheduler behavior while scanning for one device and maintaining the other HID link.
- Pairing behavior of the user's keyboard and mouse, especially passkey association requirements.
- Report Map formats and service-instance counts of the actual peripherals.
- Whether the USB architecture selected by the other workstream enumerates and behaves correctly on PC and Xbox; no Xbox claim follows from this BTstack feasibility result.

## Sources

### Baseline source

- [Starting project at `2c6a303`](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/tree/2c6a303d1f172e56b271283af978efdcc483a389)
- [Bluetooth application](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/hog_host_demo.c)
- [USB/Core-0 main loop](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/main.c)
- [USB descriptors](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/usb_descriptors.c)
- [BTstack resource configuration](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/btstack_config.h)
- [TinyUSB configuration](https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/tusb_config.h)

### Pico SDK and BTstack

- [Pico SDK 2.2.0 exact tree](https://github.com/raspberrypi/pico-sdk/tree/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779)
- [Current Pico SDK 2.3.0 release](https://github.com/raspberrypi/pico-sdk/releases/tag/2.3.0)
- [Official Pico SDK networking/BTstack API documentation](https://www.raspberrypi.com/documentation/pico-sdk/networking.html)
- [BTstack commit bundled by SDK 2.2.0](https://github.com/bluekitchen/btstack/tree/501e6d2b86e6c92bfb9c390bcf55709938e25ac1)
- [BTstack 1.8.2 bundled by SDK 2.3.0](https://github.com/bluekitchen/btstack/tree/075a0780f0fad7ff67d58ac19f46e8953656a752)
- [Current official BTstack HIDS Host API](https://bluekitchen-gmbh.com/btstack/develop/appendix/apis/#sec:hids_host_api)
- [Pico SDK flash TLV implementation](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_btstack/btstack_flash_bank.c)
- [Pico SDK CYW43/BTstack initialization](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_cyw43_driver/btstack_cyw43.c)
- [Raspberry Pi discussion of concurrent BLE limits](https://github.com/raspberrypi/pico-feedback/issues/313)
- [Infineon CYW43439 concurrent-link answer](https://community.infineon.com/t5/AIROC-Wi-Fi-and-Wi-Fi-Bluetooth/cyw43439-max-BLE-connection-count/td-p/399375)

### Standards and recovery

- [Bluetooth SIG HOGP 1.2](https://www.bluetooth.com/specifications/specs/hid-over-gatt-profile-hogp/)
- [Bluetooth SIG Assigned Numbers](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/index-en.html)
- [Raspberry Pi Pico BOOTSEL/recovery documentation](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html#reset-flash-memory)
- [Official Pico examples, including runtime BOOTSEL-button caveat](https://github.com/raspberrypi/pico-examples#pico-board)
- [TinyUSB 0.18.0 HID device implementation](https://github.com/hathach/tinyusb/blob/86ad6e56c1700e85f1c5678607a762cfe3aa2f47/src/class/hid/hid_device.c)

## Workstream status

- Repository/code trace: complete for all tracked text source and documentation at baseline; committed UF2 binaries were treated as opaque build artifacts.
- Upstream source research: complete for intended SDK 2.2.0 and current SDK 2.3.0/BTstack 1.8.2.
- Firmware changes: none.
- Build/test/hardware work: not performed in this read-only research role.
- Commits: none.
