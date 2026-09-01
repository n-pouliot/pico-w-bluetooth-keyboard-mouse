# USB/TinyUSB architecture research

Date: 2026-09-01

Role: Researcher B — USB/TinyUSB architecture

Repository revision inspected: `2c6a303d1f172e56b271283af978efdcc483a389`

Scope: research and architecture only; no firmware was changed or tested.

## Evidence labels

- **VERIFIED (REPOSITORY):** established by reading this repository at the revision above.
- **DOCUMENTED:** stated by an official TinyUSB, Raspberry Pi, USB-IF, BTstack, or Microsoft source.
- **INFERRED:** an engineering conclusion from documented behavior; it still needs testing.
- **UNVERIFIED:** no adequate public evidence or physical test was found.

## Recommendation

Use one **static USB composite device with two separate HID interfaces**:

1. HID instance/interface 0: canonical keyboard, interrupt IN endpoint `0x81`, no Report ID.
2. HID instance/interface 1: canonical mouse, interrupt IN endpoint `0x82`, no Report ID.

Both interfaces and both report descriptors must be fixed at firmware build time and available from power-on, whether zero, one, or two BLE peripherals are connected. Parse each BLE peripheral's Report Map internally and translate supported fields into the fixed USB reports. BLE connect, disconnect, sleep, replacement, and reconnect must never change the USB descriptor set or force USB re-enumeration.

**INFERRED:** This is the safest architecture for the target because it gives the host two ordinary, independently identifiable HID functions; permits independent keyboard and mouse boot-interface protocol declarations; isolates endpoint scheduling; eliminates Report-ID collisions between unrelated BLE devices; and avoids exposing untrusted peripheral descriptors to the USB host.

**UNVERIFIED:** No public Microsoft source found in this research specifies whether Xbox Series X prefers separate HID interfaces or one HID interface with multiple Report IDs. Xbox hardware testing remains mandatory. Microsoft's public material establishes support for wired USB keyboards and mice and native keyboard/mouse input APIs, but not descriptor-topology compatibility.

## What the repository implements today

### USB topology

**VERIFIED (REPOSITORY):** `src/tusb_config.h` sets `CFG_TUD_HID` to 1 and `CFG_TUD_HID_EP_BUFSIZE` to 64. `src/usb_descriptors.c` declares one HID interface (`ITF_NUM_HID`) and one interrupt IN endpoint (`0x81`). Its interface subclass and protocol are both zero (`HID_SUBCLASS_NONE`, `HID_ITF_PROTOCOL_NONE`). There is no interrupt OUT endpoint.

**VERIFIED (REPOSITORY):** Before BLE is ready, the single USB interface exposes a TinyUSB-generated report descriptor containing keyboard, mouse, consumer-control, and gamepad top-level collections with Report IDs 1–4. This means the pre-connection USB shape is not merely a keyboard plus mouse; it also advertises consumer-control and generic gamepad input.

### Dynamic descriptor mirroring

**VERIFIED (REPOSITORY):** The implementation dynamically substitutes the connected BLE peripheral's Report Map:

- `tud_hid_descriptor_report_cb()` returns BTstack's cached BLE descriptor when the singleton application state is `READY`; otherwise it returns the built-in four-collection descriptor.
- `tud_descriptor_configuration_cb()` independently selects either the BLE descriptor length or the built-in descriptor length and writes that value into the HID descriptor's `wDescriptorLength`.
- The BLE side allocates one shared 2,048-byte descriptor-storage array and retrieves descriptor data by the singleton `hids_cid`.
- On successful HIDS discovery, Core 1 sets `g_usb_reinit_request`. Core 0 calls `tud_disconnect()`, waits 100 ms, clears the HID queue, and calls `tud_connect()` so the host enumerates again.

This behavior is also described in the project README as byte-for-byte pass-through followed by USB re-enumeration.

**VERIFIED (REPOSITORY):** Incoming HIDS reports are copied into a shared queue and sent with `tud_hid_report(0, report, report_len)`. The separately stored `ST_HID_RPT.report_id` is not used by the USB sender.

**DOCUMENTED:** Current upstream BTstack inserts the Report ID at the start of the report data emitted in `GATTSERVICE_SUBEVENT_HID_REPORT`. **INFERRED:** The repository relies on corresponding behavior in its SDK-bundled `hids_client` API, whose exact version is not pinned in this repository.

**VERIFIED (REPOSITORY):** A BLE disconnection sets the application state away from `READY` and starts reconnection, but it does not request USB re-enumeration and does not enqueue a keyboard or mouse release report. A later BLE report can force `READY` and another USB re-enumeration.

### Important defects/limits in the current USB model

- **HIGH — descriptor mutability and split selection:** The configuration-descriptor callback and report-descriptor callback consult mutable BLE state separately on Core 0 while Core 1 owns the HIDS state/storage. A state transition between requests can make the advertised report length disagree with the returned descriptor. Even without a race, the same VID/PID/serial presents materially different report descriptors over time.
- **HIGH — dual-device mirroring is not composable:** Two arbitrary Report Maps can reuse the same Report IDs, contain unrelated top-level collections, or contain reports without IDs. Concatenating them would require validated descriptor rewriting and corresponding packet rewriting. Keeping one map per USB interface would still make each USB interface unavailable or mutable until its BLE device connects. Either approach defeats stable enumeration.
- **HIGH — stuck-input path:** No all-keys/all-buttons release is generated on disconnect. The host can retain the last pressed state indefinitely.
- **HIGH — unsafe truncation semantics:** BLE reports longer than `CMN_HID_RPT_DATA_SIZE` are silently truncated to 512 bytes rather than rejected. The USB interrupt endpoint advertises only 64 bytes, so reports over 64 bytes cannot be transferred as declared at full speed. Truncating a structured HID report can turn it into different data, not merely incomplete data.
- **MEDIUM — version-sensitive Report-ID framing:** **DOCUMENTED:** Current upstream BTstack unconditionally inserts the Report ID at the start of emitted HIDS report data, including ID zero. **INFERRED:** If the project's SDK-bundled `hids_client` does the same for a no-ID report, raw forwarding adds a byte that the mirrored USB Report Map does not declare. The bundled source contract must be checked and pinned; canonical normalization avoids depending on this convention.
- **MEDIUM — queue policy:** The ring can become full and silently drops a report. A dropped release report creates stuck input. One shared FIFO also allows high-rate mouse traffic to delay keyboard traffic.
- **MEDIUM — unintended initial identity:** Before BLE connection the Xbox/PC can observe keyboard, mouse, consumer-control, and gamepad collections, and after connection it observes an arbitrary peripheral map. This is avoidable uncertainty for a keyboard/mouse-only bridge.
- **MEDIUM — cache/re-enumeration behavior:** TinyUSB's examples deliberately use a distinct PID per descriptor/interface set to obtain a fresh host driver match. The repository keeps the same VID, PID, `bcdDevice`, and serial while changing the report descriptor. Host behavior, especially Xbox behavior, is not guaranteed.
- **MEDIUM — target/version ambiguity:** `src/CMakeLists.txt` requests Pico SDK 2.2.0 but does not vendor or directly pin TinyUSB; TinyUSB is whatever that SDK installation supplies. It also defaults `PICO_BOARD` to `pico2_w`, not `pico_w`. A Pico W release must explicitly pin and record its SDK/TinyUSB revisions and build target.

## Authoritative constraints

### TinyUSB capabilities

**DOCUMENTED:** TinyUSB 0.21.0 is the latest upstream release found as of this report. The repository requests Pico SDK 2.2.0, so upstream 0.21.0 APIs must not be assumed to match the project's actual bundled version without checking the initialized SDK submodule.

**DOCUMENTED:** TinyUSB supports both candidate designs:

- `hid_composite` uses one HID interface and one interrupt IN endpoint. Its report descriptor combines keyboard, mouse, and other collections using Report IDs.
- `hid_multiple_interface` sets `CFG_TUD_HID` to 2, returns a descriptor based on HID instance, declares keyboard and mouse interfaces with endpoints `0x81` and `0x82`, and sends through `tud_hid_n_keyboard_report()` / `tud_hid_n_mouse_report()`.

TinyUSB's multi-interface API uses a zero-based HID **instance**, which is not necessarily the USB `bInterfaceNumber` if other class interfaces are interleaved. The proposed design has only two HID interfaces, but code should still name constants as instances and interface numbers separately.

**DOCUMENTED:** For current TinyUSB, `TUD_HID_DESCRIPTOR(..., HID_ITF_PROTOCOL_KEYBOARD, ...)` or `...MOUSE...` sets the interface to the boot subclass; `HID_ITF_PROTOCOL_NONE` sets no subclass. TinyUSB tracks boot/report mode per HID instance, handles `GET_PROTOCOL` and `SET_PROTOCOL`, exposes `tud_hid_n_get_protocol()`, and invokes `tud_hid_set_protocol_cb()` after a host protocol change.

**DOCUMENTED:** `tud_hid_n_report()` prepends a nonzero Report ID. A design with no Report IDs must pass zero and provide only the report payload. This is simpler than the current raw-buffer convention and removes ambiguity about whether the first BLE byte is an ID.

**DOCUMENTED:** TinyUSB's host-side helper `tuh_hid_parse_report_descriptor()` is described by its own example as suitable for a "common/simple enough" descriptor; the same example then assumes keyboard and mouse reports use boot layouts. It is not a generic field-level translator for arbitrary BLE Report Maps and must not be used as if it were one.

### RP2040 / Pico W USB limits

**DOCUMENTED:** Pico W uses RP2040. RP2040 has an integrated USB 1.1 PHY and a USB 2.0-compatible full-speed (12 Mbit/s) device controller.

**DOCUMENTED:** In device mode RP2040 supports endpoint numbers 0–15 in both IN and OUT directions (32 endpoint addresses), control/isochronous/bulk/interrupt transfers, and double buffering. Its USB controller has 4,096 bytes of dual-port SRAM, of which 3,840 bytes are usable for endpoint buffers.

**INFERRED:** Two interrupt IN endpoints with 8-byte buffers are comfortably within those limits. Endpoint count and USB DPRAM are not reasons to combine keyboard and mouse into one interface. Exact DPRAM use is TinyUSB-driver/configuration dependent and should be read from the final build/map rather than guessed.

**DOCUMENTED:** A full-speed interrupt endpoint can carry at most 64 bytes per transaction on RP2040. The canonical keyboard report is 8 bytes and the proposed report-protocol mouse report is 5 bytes, so 8-byte endpoint buffers are sufficient for both TinyUSB instances.

### HID interface, Report ID, and endpoint rules

**DOCUMENTED:** HID 1.11 requires an interrupt IN pipe for input; an interrupt OUT pipe is optional. Endpoint 0 is shared for control requests, including output reports when no OUT endpoint is present. Therefore a keyboard LED output report does not require a third data endpoint.

**DOCUMENTED:** If a report descriptor declares Report IDs, a one-byte ID prefix is added to every transferred report. Report IDs are the standards-compliant way to carry keyboard and mouse reports over one HID endpoint.

**DOCUMENTED:** `bInterfaceProtocol` has meaning for boot-subclass interfaces and has one value: 1 for keyboard or 2 for mouse. One interface cannot simultaneously declare both values. A one-interface keyboard+mouse design can be standards-compliant in report protocol, but cannot independently identify both boot protocols.

**DOCUMENTED:** Devices initialize in report protocol. A boot host may issue `SET_PROTOCOL` per interface. The boot keyboard input format is 8 bytes (modifier, reserved, six keycodes). The boot mouse prefix is buttons, relative X, and relative Y; later bytes may be device-specific.

**INFERRED:** Giving keyboard and mouse separate boot-capable interfaces broadens compatibility with simple hosts and firmware while preserving full report-protocol behavior. It does not prove Xbox compatibility.

### Descriptor stability

**DOCUMENTED:** The USB host obtains device, configuration, interface, endpoint, HID, and report descriptors through control transfers, primarily during enumeration, and creates pipes from the selected interface/endpoint descriptors. Windows also uses VID, PID, and `bcdDevice` in hardware/compatible IDs.

**INFERRED:** A BLE connection is runtime state, not a valid reason to change the USB device's declared shape. Keeping descriptors constant avoids dependence on host cache invalidation, disconnect timing, and repeated composite-device setup. If a future firmware release changes the interface set or report layouts, it should use an appropriately assigned new PID or at least a deliberate device-revision strategy and be regression-tested on supported hosts.

### Xbox evidence boundary

**DOCUMENTED:** Xbox states that wired USB mice and keyboards can be used for navigation and in select games/apps. Microsoft GDK documentation treats keyboards and mice as native, separate input kinds on Xbox-family/Series platforms and states that individual keyboard and mouse state is tracked separately.

**UNVERIFIED:** Public Microsoft documentation found here does not promise that every standards-compliant HID topology, every composite parent, every Report-ID arrangement, consumer controls, NKRO, or boot protocol is accepted by retail Xbox Series X. Nor does it compare a two-interface device with a one-interface, two-Report-ID device. Only testing the final descriptor and firmware on a retail console can answer that.

## Architecture comparison

| Criterion | Two static HID interfaces | One static HID interface + Report IDs | Dynamic BLE descriptor mirroring |
|---|---|---|---|
| Standards compliance | Strong; ordinary composite HID | Strong in report protocol | Depends on every remote descriptor and rewrite strategy |
| TinyUSB support | First-party example; `CFG_TUD_HID=2` | First-party example; `CFG_TUD_HID=1` | Application-specific and stateful |
| USB resources | EP0 + two IN endpoints | EP0 + one IN endpoint | Usually EP0 + one/two IN endpoints |
| RP2040 fit | Ample headroom | Ample headroom | Descriptor/report buffers dominate, not endpoints |
| Boot protocol | Keyboard and mouse independently declared | Cannot declare both on one interface | Depends on remote map/interface and re-enumeration |
| Report IDs | Not needed on USB | Required; shared namespace | Collision/rewrite problem with two unrelated devices |
| Traffic isolation | Independent endpoint readiness and queues | One transfer stream; needs arbitration | Depends on design |
| BLE reconnect | No USB change | No USB change | Descriptor may become stale or require re-enumeration |
| Untrusted descriptors reach host | No | No | Yes unless fully sanitized |
| Core feature scope | Naturally keyboard + mouse | One combined logical HID function | Arbitrary/unbounded peripheral features |
| Xbox confidence | **INFERRED** best of the three; still untested | Standards-valid but shared-interface parsing is unverified | Lowest; repeated shape changes are unverified |

The one-interface design saves one tiny endpoint buffer and about one HID interface descriptor block. RP2040 does not need that saving. The two-interface design therefore wins on isolation, boot semantics, implementation clarity, and risk reduction.

## Proposed static USB descriptor contract

The exact byte arrays must be generated/validated against the pinned TinyUSB version, but the contract should be:

| Field | Keyboard | Mouse |
|---|---:|---:|
| HID instance | 0 | 1 |
| `bInterfaceNumber` | 0 | 1 |
| Class | HID (`0x03`) | HID (`0x03`) |
| Subclass | Boot (`0x01`) | Boot (`0x01`) |
| Protocol | Keyboard (`0x01`) | Mouse (`0x02`) |
| Interrupt IN endpoint | `0x81` | `0x82` |
| `wMaxPacketSize` | 8 | 8 |
| `bInterval` | 1 full-speed frame (1 ms) | 1 full-speed frame (1 ms) |
| USB Report ID | None | None |
| Report-protocol input | 8-byte 6KRO keyboard | 5 buttons, signed relative X/Y, vertical wheel, horizontal pan |
| Boot-protocol input | Same 8 bytes | 3-byte buttons/X/Y prefix |

Implementation consequences:

- Set `CFG_TUD_HID` to 2 and `CFG_TUD_HID_EP_BUFSIZE` to 8.
- Use a constant configuration descriptor with two `TUD_HID_DESCRIPTOR` blocks. With no optional descriptors or OUT endpoints, its expected total is 59 bytes: 9-byte configuration plus two 25-byte HID interface blocks. Validate this at compile time and with a descriptor parser.
- `tud_hid_descriptor_report_cb(instance)` must return one of two constant arrays solely from `instance`; it must never inspect BLE state.
- Use `tud_hid_n_ready()` and `tud_hid_n_report()`/typed helpers with the appropriate instance.
- Do not add an Interface Association Descriptor: the two HID interfaces are two independent functions, matching TinyUSB's official multi-interface example.
- Do not add interrupt OUT endpoints initially. Receive keyboard LEDs with `SET_REPORT` over endpoint 0. Forwarding those LEDs to BLE is optional compatibility work, but requests must be parsed safely.
- Remove BLE-triggered `tud_disconnect()`/`tud_connect()` behavior. USB connects once and remains stable.
- Use a deliberate, authorized VID/PID. The current `0xCAFE` is a TinyUSB example VID and should not be treated as a production allocation. Changing from the old mutable single-interface shape also warrants a new product identity/revision to avoid cached descriptors.
- Advertise measured, justified bus power. The current descriptor requests 500 mA; that value is independent of the HID topology and should be validated by the power/safety workstream.

## Boot protocol policy

The interfaces should be boot-capable even though normal modern hosts use report protocol.

- Keyboard: keep the report-protocol layout identical to the 8-byte boot layout. No Report ID is present in either mode. Implement the HID idle request behavior required for keyboards and accept the one-byte LED output report.
- Mouse: in report protocol send the fixed 5-byte TinyUSB-style report. When `tud_hid_n_get_protocol(mouse_instance)` is boot, send only buttons/X/Y. Do not send a Report ID. Wheel, horizontal pan, and buttons 4–5 are unavailable in strict boot mode.
- Implement/test `tud_hid_set_protocol_cb()` per instance. Protocol changes on the keyboard must not alter mouse behavior and vice versa.

**INFERRED:** This policy is simpler and more compatible than declaring both interfaces non-boot. **UNVERIFIED:** Xbox may never issue `SET_PROTOCOL`; boot support is a compatibility feature, not an Xbox-specific requirement.

## Robust canonical BLE-to-USB translation

Canonical output is only safe if the BLE Report Map is actually parsed. It is not acceptable to cast arbitrary bytes to `hid_keyboard_report_t` or `hid_mouse_report_t`.

### 1. Validate and compile each BLE Report Map

At HIDS discovery, before marking a slot usable:

1. Enforce a documented maximum descriptor length before storage/copy. Reject, rather than truncate, oversize maps.
2. Parse HID short items with bounds checks. Correctly track global state, Push/Pop, collections, Usage Page, Usage/Usage Min/Max, Report ID, Report Size, Report Count, logical ranges, and Input flags.
3. Track bit offsets and total expected byte length independently for every `(report_id, report_type)`; use checked arithmetic and reject overflow or impossible sizes.
4. Require a relevant top-level Application Collection: Generic Desktop Keyboard for a keyboard slot, or Generic Desktop Mouse/Pointer for a mouse slot. Device name or BLE Appearance may help discovery but must not override the Report Map.
5. Build an immutable field plan for supported input fields. Ignore explicitly unsupported fields only after their bit ranges have been validated.
6. Reject malformed maps, duplicate/ambiguous ownership, unsupported long items if the parser cannot prove them safe, unknown report-ID structure, or input reports above the configured maximum.

Prefer the pinned BTstack `btstack_hid_parser`/usage-iterator implementation where it supplies the required field-level semantics; BTstack's own HID host example uses that parser. Wrap it with project limits, exact-length checks, and tests. Do not substitute TinyUSB's simple top-level report-info helper, whose example still assumes boot-formatted payloads.

### 2. Normalize HIDS report identity and length

Current BTstack HIDS host events carry `report_id` as metadata and also insert it into the event's report byte sequence. The project's exact SDK-bundled API must be checked and pinned. Normalize once at the callback boundary into:

`{device_slot, report_id, payload_without_id, payload_length}`

Then look up the compiled field plan by Report ID and require the exact or explicitly permitted length. Unknown IDs, short packets, long packets, and ID/prefix disagreement must be dropped and counted; never guess and never truncate.

### 3. Keyboard subset and state model

Support initially:

- Keyboard/Keypad usage page (`0x07`).
- Left/right Control, Shift, Alt, and GUI modifiers (`0xE0`–`0xE7`).
- Array-style key fields and variable/bitmap NKRO fields when the descriptor maps them unambiguously.
- Report IDs and multiple input reports.

Maintain each report's contribution separately, then compute the union of current keyboard usages. Updating Report ID A must not release fields owned by Report ID B. Emit one canonical 8-byte state snapshot. If more than six non-modifier keys are down, emit the boot-defined ErrorRollOver value in all six array slots rather than silently choosing keys. Document the resulting USB side as 6KRO even when a BLE keyboard is NKRO.

Consumer/media, system-control, vendor-defined, macro, and locale-specific extra collections are out of the initial USB contract. Ignore them safely. If consumer controls are added later, prefer a deliberate third non-boot HID interface and a new descriptor/product revision over adding Report IDs to the boot keyboard interface without compatibility testing.

### 4. Mouse subset and state model

Support initially:

- Button usages 1–5 as absolute state.
- Generic Desktop X and Y only when marked relative.
- Generic Desktop Wheel when relative.
- Consumer AC Pan for horizontal scrolling when relative.
- Report IDs and fields at arbitrary validated bit offsets/sizes.

Reject absolute X/Y devices (touchpads/tablets) for the mouse slot unless an explicit conversion is designed. Sign-extend values according to descriptor width/logical minimum. Accumulate relative deltas in a wider signed type, then split values outside `[-127, 127]` across multiple USB reports so movement is not silently lost. Keep button state in every emitted mouse report.

### 5. Queueing, concurrency, and release guarantees

- Use separate keyboard and mouse output paths; do not retain the current shared raw-report FIFO.
- Keyboard reports are state snapshots. A bounded transition queue may be used, but overflow must schedule a highest-priority all-keys-released report and reset state; it must never silently discard a release.
- Mouse relative motion/wheel values should be accumulated, while buttons retain latest state. Split accumulated deltas as endpoint capacity becomes available.
- Cross-core handoff must copy canonical values, not pointers into BTstack event memory or descriptor storage. Protect it with a bounded mailbox/critical section whose ownership is explicit.
- On every terminal path for a BLE slot—disconnect, HIDS error, pairing replacement, parser rejection after prior use, timeout teardown—clear that slot's canonical state and prioritize a zero keyboard report or zero-button mouse report. Do not reset or re-enumerate the other USB interface.
- Keep USB output inert until a descriptor has been accepted for the corresponding BLE slot. An unsupported peripheral must produce no USB input.

## Latency analysis

**DOCUMENTED/VERIFIED (REPOSITORY):** RP2040 operates as a full-speed USB device. The current code and proposed descriptors use `bInterval = 1`, which requests a 1 ms polling interval. The repository requests a BLE connection interval of 12.5–15 ms with peripheral latency zero after encryption, but the peripheral/controller negotiation determines the actual interval.

**INFERRED:** Once a BLE notification has arrived at the Pico, validated field extraction plus cross-core handoff should normally be much less than 1 ms, and the next USB poll adds approximately 0–1 ms when the endpoint is ready. End-to-end input latency is therefore likely dominated by the BLE peripheral's own scan/report timing and negotiated connection interval, not by canonical translation.

Separate endpoints avoid head-of-line blocking between keyboard and mouse and allow each interface to have one transfer in flight. This is a robustness advantage; it is not a claim that the host will schedule both with identical latency.

**UNVERIFIED:** No firmware timing instrumentation, USB capture, BLE sniffer trace, PC test, or Xbox latency test was performed in this workstream. The final build should timestamp BLE callback arrival, canonical enqueue, TinyUSB submission, and report-complete callbacks under simultaneous high-rate mouse and keyboard traffic.

## Compatibility and implementation risk register

| Severity | Risk | Status / required mitigation |
|---|---|---|
| HIGH | Malformed or adversarial BLE Report Map causes out-of-bounds parse, overflow, or wrong field extraction | Open for implementation: fixed limits, checked parser, exact lengths, fuzz/unit tests, fail inertly |
| HIGH | Keyboard keys or mouse buttons remain held after disconnect/error/queue overflow | Open for implementation: priority release on every terminal path and explicit tests |
| HIGH | Raw/dynamic descriptor reaches USB host or changes after enumeration | Architecturally removed: use only constant canonical descriptors |
| HIGH | Two BLE devices' Report IDs/state become conflated | Architecturally removed at USB boundary; implementation still needs per-device parser/state contexts |
| MEDIUM | Retail Xbox rejects the composite parent or one of the interface/report layouts | **UNVERIFIED:** validate on PC first, then retail Xbox; do not claim compatibility beforehand |
| MEDIUM | Pinned Pico SDK's TinyUSB API differs from current upstream 0.21.0 | Record SDK/TinyUSB commits; compile against the pin; adapt callbacks to that version; do not mix examples blindly |
| MEDIUM | 6KRO output loses NKRO capability | Accepted initial limitation: ErrorRollOver beyond six; document and test common gaming chords |
| MEDIUM | Consumer/media/system keys are absent | Accepted initial limitation for lowest-risk keyboard/mouse identity; consider a separately versioned interface later |
| MEDIUM | Absolute pointing devices, high-resolution wheels, vendor buttons, or unusual units are unsupported | Reject/ignore by explicit rules; list supported descriptor forms and test fixtures |
| MEDIUM | Mouse delta saturation loses movement | Accumulate in wide integers and split into multiple reports; test extreme/bursty values |
| MEDIUM | Caps/Num/Scroll LED state is not returned to BLE keyboard | Implement safe USB `SET_REPORT`; forwarding over HIDS output is optional but must be documented |
| MEDIUM | Endpoint/instance numbers are confused | Separate named HID-instance and USB-interface constants; descriptor and callback tests |
| MEDIUM | Host caches the former descriptor shape under the same identity | Use authorized product identity/revision for the new fixed topology and test unplug/reflash/replug |
| MEDIUM | Unauthorized/example VID/PID creates collisions or compliance issues | Replace `0xCAFE` with an authorized allocation before release |
| LOW | Two endpoints consume slightly more TinyUSB RAM/DPRAM and bus schedule than one | RP2040 limits provide ample margin; confirm final map and descriptor dump |

## Required validation before architecture approval becomes release approval

1. Compile-time assertions for configuration length, interface count, endpoint addresses, endpoint packet sizes, and canonical report struct sizes.
2. Parse the built descriptor with an independent USB descriptor checker; verify exactly two HID interfaces and no gamepad, mass-storage, vendor, or controller interfaces.
3. Unit/fuzz tests for Report Maps: boot keyboard, NKRO bitmap, multiple IDs, consumer+keyboard collection, 3/5-button mice, wheel/pan, non-byte-aligned fields, absolute X/Y, malformed/truncated items, oversized counts, unknown IDs, and wrong packet lengths.
4. State tests for simultaneous keyboard/mouse traffic, ordering, queue pressure, mouse delta conservation, and all disconnect/error release paths.
5. Protocol tests: issue `GET_PROTOCOL`/`SET_PROTOCOL` independently to both interfaces; verify 8-byte keyboard, 5-byte report mouse, and 3-byte boot mouse behavior.
6. USB enumeration captures before BLE connection, with only keyboard, with only mouse, with both, during disconnect, and after reconnect. Every descriptor byte must remain identical; no USB disconnect should occur.
7. PC input tests before Xbox: typing, modifiers, six-key rollover/error case, held-key disconnect, movement, buttons 1–5, wheel/pan, simultaneous input, suspend/resume, and power cycle.
8. Retail Xbox Series X test, clearly reported as physical hardware evidence. Verify dashboard keyboard behavior and only games that officially support keyboard/mouse. Do not interpret generic HID enumeration as proof that every game accepts input.

## Sources

Official/primary sources, accessed 2026-09-01:

- TinyUSB releases; 0.21.0 is the latest listed release (2026-06-29): <https://github.com/hathach/tinyusb/releases/tag/0.21.0>
- TinyUSB multiple-HID-interface descriptors: <https://github.com/hathach/tinyusb/blob/0.21.0/examples/device/hid_multiple_interface/src/usb_descriptors.c>
- TinyUSB multiple-HID-interface configuration (`CFG_TUD_HID = 2`): <https://github.com/hathach/tinyusb/blob/0.21.0/examples/device/hid_multiple_interface/src/tusb_config.h>
- TinyUSB multiple-HID-interface send APIs: <https://github.com/hathach/tinyusb/blob/0.21.0/examples/device/hid_multiple_interface/src/main.c>
- TinyUSB one-interface HID composite example: <https://github.com/hathach/tinyusb/blob/0.21.0/examples/device/hid_composite/src/usb_descriptors.c>
- TinyUSB HID device implementation and protocol/report behavior: <https://github.com/hathach/tinyusb/blob/0.21.0/src/class/hid/hid_device.c>
- TinyUSB HID API/templates: <https://github.com/hathach/tinyusb/blob/0.21.0/src/class/hid/hid_device.h>
- TinyUSB HID descriptor macro behavior: <https://github.com/hathach/tinyusb/blob/0.21.0/src/device/usbd.h>
- TinyUSB host parser/example limitation: <https://github.com/hathach/tinyusb/blob/0.21.0/examples/host/cdc_msc_hid/src/hid_app.c>
- Raspberry Pi RP2040 Datasheet, USB chapter pp. 383–385: <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- Raspberry Pi Pico-series documentation (Pico W uses RP2040; USB 1.1 device/host): <https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html>
- Raspberry Pi SDK RP2040 USB DPRAM definitions (`USB_NUM_ENDPOINTS`, DPRAM size, packet size): <https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2040/hardware_structs/include/hardware/structs/usb_dpram.h>
- USB-IF Device Class Definition for HID 1.11: <https://www.usb.org/sites/default/files/documents/hid1_11.pdf>
- USB-IF HID specifications/tools and current HID Usage Tables 1.7: <https://www.usb.org/hid>
- USB-IF USB 2.0 Specification library entry: <https://www.usb.org/document-library/usb-20-specification>
- BTstack HIDS host implementation, including inserted Report ID: <https://github.com/bluekitchen/btstack/blob/master/src/ble/gatt-service/hids_host.c>
- BTstack HID host example using the field-level HID parser: <https://github.com/bluekitchen/btstack/blob/master/example/hid_host_demo.c>
- Microsoft standard USB descriptor/enumeration documentation: <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/standard-usb-descriptors>
- Microsoft Xbox accessibility statement for wired USB mice/keyboards: <https://www.xbox.com/en-US/community/for-everyone/accessibility/>
- Microsoft GDK keyboard and mouse documentation: <https://learn.microsoft.com/en-us/xbox/gdk/docs/features/common/input/advanced/input-keyboard-mouse>
- Microsoft GameInput platform overview: <https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-overview>
- Inspected upstream repository file at the audited revision: <https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/usb_descriptors.c>
- Inspected upstream BLE/HIDS implementation at the audited revision: <https://github.com/shiomachisoft/picow_ble_usb_hid_bridge/blob/2c6a303d1f172e56b271283af978efdcc483a389/src/hog_host_demo.c>

## Bottom line

**INFERRED ARCHITECTURE DECISION:** Adopt two static canonical HID interfaces, not one shared Report-ID interface and not dynamic BLE descriptor mirroring. The endpoint cost is negligible on RP2040, TinyUSB directly supports the topology, boot semantics remain clean, and each BLE device can fail/reconnect without changing USB identity or disrupting the other interface.

**RELEASE STATUS:** Architecture recommendation only. Xbox compatibility, actual parser safety, descriptor bytes, latency, and disconnect-release behavior remain unverified until implementation and the tests above are completed.
