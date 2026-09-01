# Research D: HID parsing, classification, pairing security, and persistence

Date: 2026-09-01  
Scope: research and design only; no firmware was changed and no hardware was tested.

## Executive conclusion

Do not extend the current byte-for-byte BLE-to-USB descriptor passthrough to two peripherals. Treat each BLE Report Map as untrusted input, validate it once, and compile only a deliberately supported keyboard or mouse subset into bounded extraction plans. Convert accepted BLE reports into two fixed canonical USB states. A candidate is not a keyboard or mouse until its validated top-level HID Application Collection and fields say so; its name and GAP Appearance are only discovery hints.

Use two independent connection contexts and two independent persistent role records. Pair only during an explicit physical enrollment window. With the Pico advertising `NoInputNoOutput`, the practical baseline is bonded, encrypted **Just Works** pairing: it is not MITM-authenticated. Devices that require passkey entry or authenticated pairing must fail clearly rather than being silently weakened. A replacement transaction must validate the new device and commit its role record before deleting the old role's bond.

The current implementation is not safe to scale by changing connection-count macros. Its singleton state, unchecked Report Map passthrough, blind numeric-comparison confirmation, ignored re-encryption status, missing disconnect releases, one-slot TLV record, and source-less raw report queue all require redesign.

## Evidence basis and pinned source revisions

The local audit was performed at project/upstream commit `2c6a303d1f172e56b271283af978efdcc483a389`. `src/CMakeLists.txt` selects Pico SDK 2.2.0. The SDK 2.2.0 Git tree is `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779`; its BTstack submodule is `501e6d2b86e6c92bfb9c390bcf55709938e25ac1`, and its TinyUSB submodule is `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`.

Primary references:

- Bluetooth SIG, [HID over GATT Profile](https://www.bluetooth.com/specifications/specs/hogp-1-0/) and [HOGP implementation conformance statement](https://files.bluetooth.com/wp-content/uploads/2024/10/HOGP.ICS.p8.pdf).
- Bluetooth SIG, [Human Interface Device Service 1.0](https://www.bluetooth.com/specifications/specs/human-interface-device-service-1-0/) and [HIDS test suite](https://files.bluetooth.com/wp-content/uploads/dlm_uploads/2025/02/HIDS.TS_.p7.pdf).
- Bluetooth SIG, [Assigned Numbers](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/index-en.html), including GAP Appearance values.
- Bluetooth SIG, [Core 6.0 Security Manager](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/host/security-manager-specification.html) and [association-model overview](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/architecture%2C-change-history%2C-and-conventions/architecture.html).
- USB-IF, [Device Class Definition for HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf) and [HID Usage Tables](https://www.usb.org/hid).
- BlueKitchen, [BTstack HIDS client API](https://bluekitchen-gmbh.com/btstack/develop/appendix/apis/#sec:hids_host_api), [SMP documentation](https://bluekitchen-gmbh.com/btstack/protocols.html), [pinned HIDS client source](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/gatt-service/hids_client.c), and [pinned HID parser source](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/btstack_hid_parser.c).
- BlueKitchen, [pinned two-bank TLV source](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/platform/embedded/btstack_tlv_flash_bank.c) and [pinned LE device database source](https://github.com/bluekitchen/btstack/blob/501e6d2b86e6c92bfb9c390bcf55709938e25ac1/src/ble/le_device_db_tlv.c).
- Raspberry Pi, [pinned Pico BTstack flash-bank integration](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_btstack/btstack_flash_bank.c), [BTstack/CYW43 TLV setup](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_cyw43_driver/btstack_cyw43.c), and [flash API constraints](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash).
- Raspberry Pi, [official runtime BOOTSEL example](https://github.com/raspberrypi/pico-examples/blob/master/picoboard/button/button.c).
- Raspberry Pi, [Pico W datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf); Winbond, [W25Q16JV datasheet](https://www.winbond.com/resource-files/w25q16jv%20spi%20revd%2008122016.pdf).

## 1. What HOGP provides, and what it does not

A HOGP peripheral exposes one or more HID Service instances. A Report Map characteristic contains a USB-style HID report descriptor. Each GATT Report characteristic has a two-byte Report Reference descriptor identifying its Report ID and Report Type. Boot keyboard/mouse characteristics are separate optional paths. The Report Reference identifies a transport report; it does not classify the semantic role of the bytes.

BTstack 501e6d2 supports multiple HIDS client objects when its pools are configured accordingly, and its descriptor storage is explicitly shared by all clients. Its HIDS event includes the HIDS CID, service index, Report ID, and report data. The pinned implementation inserts the Report ID as byte zero of every emitted report, including ID zero. An application must therefore treat the BTstack event as a framed internal representation, not automatically as a USB-wire report.

The HIDS source also compacts the shared descriptor buffer when a client disconnects. No pointer returned by `hids_client_descriptor_storage_get_descriptor_data()` may be cached across disconnects or used concurrently by the USB core. Parse/copy the map into an independent per-device plan while the connection context owns it. There is also an apparent upstream compaction defect: the offset-adjustment loop iterates `client->num_instances` while modifying each remaining `conn`. If the disconnected and remaining clients have different HIDS instance counts, not all remaining offsets are adjusted. This is still present in [current upstream `hids_host.c`](https://github.com/bluekitchen/btstack/blob/master/src/ble/gatt-service/hids_host.c#L177-L205); avoiding multiple HIDS instances in the initial supported subset prevents exposure, but the behavior still needs a regression test or upstream fix before that subset is expanded.

## 2. Standards-based classification

Classification is a post-connection state, not an advertising decision.

1. During an explicitly armed enrollment window, consider advertisements containing HID Service UUID `0x1812` or a HID Appearance. Outside that window, only resolve/connect identities already stored in a role slot.
2. Establish the intended security level before trusting GATT data.
3. Confirm that a HID Service and Report Map exist, then parse the complete map.
4. Inspect top-level **Application** Collections:
   - keyboard role: Usage Page Generic Desktop (`0x01`), Usage Keyboard (`0x06`) or Keypad (`0x07`), with at least one supported Keyboard/Keypad-page input;
   - mouse role: Usage Page Generic Desktop (`0x01`), Usage Mouse (`0x02`), with supported relative X and Y inputs.
5. Produce a role set. Exactly `{keyboard}` or `{mouse}` can be enrolled. Empty, malformed, or `{keyboard, mouse}` is rejected in the first release.
6. Treat GAP Appearance `0x03C1` (keyboard) and `0x03C2` (mouse) only as hints. Missing or Generic HID `0x03C0` is acceptable. A specific Appearance that contradicts the validated map is suspicious and should be rejected. Device names never decide the role.
7. Do not persist a role or forward input until parsing, class validation, security validation, and user confirmation have all succeeded.

This order handles peripherals that do not advertise enough information to classify before connection. It also prevents a device named “Keyboard,” or advertising a keyboard Appearance while supplying mouse fields, from occupying the keyboard slot.

## 3. Parser choice and precise supported subset

### Decision: constrained descriptor compiler

A fully generic HID parser would need to preserve arbitrary Input semantics, units, arrays, variable fields, delimiters, nested collections, multiple roles, output/feature reports, and host-specific behavior. Mirroring those maps onto USB would also combine two independently chosen Report-ID namespaces. That breadth is not needed for the core bridge and greatly enlarges the malformed-input surface.

Use a bounded descriptor compiler. It may reuse BTstack's item iterator only after its behavior is covered by host tests; the application must still enforce the limits and semantic rules below. The output is a compact list of checked bit-extraction operations keyed by `(device slot, HIDS CID, service index, report ID)`. No descriptor byte is interpreted in the notification callback.

### Hard limits for release 1

| Item | Limit / policy |
|---|---|
| BLE devices | Exactly one keyboard slot and one mouse slot |
| HIDS instances | Exactly one HID Service instance per BLE device |
| Report Map | At most 2048 bytes per device; 4096 bytes shared staging total |
| Report characteristics | At most BTstack's pinned limit of 15 per HIDS client |
| Role-relevant input plans | At most 8 Report IDs per device |
| Top-level Application Collections | At most 8 |
| Collection nesting | At most 8 |
| Global Push stack | At most 4 |
| Input fields represented in a plan | At most 256, with ranges kept compact where possible |
| Input report payload | At most 64 bytes after removing BTstack's synthetic ID byte |
| Field width extracted | 1 through 16 bits |
| Dynamic allocation | None in parser or report callback |
| Long items (`0xFE`) | Unsupported: reject the map |
| Output/Feature reports | Not translated in release 1; keyboard lock LEDs may therefore not illuminate |

Reject, rather than truncate, any object over a limit. All additions and multiplications for bit positions, `Report Size * Report Count`, offsets, and lengths must use checked arithmetic before narrowing.

### Keyboard inputs

Supported:

- Keyboard/Keypad page (`0x07`) modifier usages `0xE0` through `0xE7` as one-bit Data/Variable fields with logical range 0..1.
- Ordinary key arrays containing 8-bit Keyboard/Keypad usage IDs, including modifiers if a device redundantly supplies them.
- One-bit NKRO variable fields on the Keyboard/Keypad page with logical range 0..1.
- Multiple Report IDs and multiple collections within the one Report Map.
- Function keys and navigation/keypad keys that are ordinary Keyboard/Keypad-page usages.

Translation target: canonical eight-byte boot-style keyboard input (modifier, reserved, six key usages). Maintain a set of active usages, sort by usage ID for deterministic output, and emit at most six non-modifier usages. If more than six are active, send HID `ErrorRollOver` (`0x01`) in all six key positions; recover to the real set as soon as it returns to six or fewer. This recognizes NKRO input maps safely but deliberately does not promise NKRO end to end.

Consumer, system-control, vendor-defined, macro, and media-key inputs are ignored only after their bit ranges have been structurally validated. An additional independent Consumer Control collection does not disqualify an otherwise supported keyboard. A map that makes the same Report ID ambiguous between keyboard and mouse roles, contains no supported keyboard input, or gives a supported usage incompatible semantics is rejected.

### Mouse inputs

Supported:

- Generic Desktop X (`0x30`) and Y (`0x31`) as Data/Variable/**Relative** signed fields, 8 or 16 bits, with a logical range crossing zero.
- Button page usages 1 through 5 as one-bit Data/Variable fields with logical range 0..1.
- Generic Desktop Wheel (`0x38`) as a signed Relative 8- or 16-bit field.
- Multiple Report IDs and harmless padding/constant fields.

Translate to five buttons, signed 8-bit relative X/Y, and signed 8-bit vertical wheel. Preserve a 16-bit movement or wheel delta by splitting it into bounded `[-127, 127]` chunks over successive USB reports; do not clamp and lose motion. Horizontal pan and buttons above five are parsed/ignored but not forwarded. Absolute X/Y, digitizers, acceleration/vendor fields masquerading as X/Y, or a mouse without both supported relative axes are rejected.

### Multiple Report IDs and collections

- A Report ID item with value zero is invalid; zero is only the implicit ID for a map with no Report ID items.
- Each input Report ID must have exactly one computed payload length. Duplicate GATT input Report References for the same `(service index, ID)` are rejected because the BTstack event does not preserve the characteristic value handle.
- Key plans by service index as well as ID even though release 1 accepts service index zero only. This avoids baking a false globally unique-ID assumption into the design.
- A candidate exposing both accepted keyboard and mouse Application Collections is reported as “combo device unsupported,” not assigned arbitrarily.
- Reports for ignored collections remain length-validated and are then dropped.

### Malformed-map and packet defenses

Descriptor validation must reject: truncated short or long items; unsupported long items; reserved/invalid item forms; collection or global-stack underflow/overflow; unclosed collections; Report ID zero items; reversed or incomplete Usage ranges; missing Report Size/Count; zero or excessive sizes/counts; arithmetic overflow; contradictory logical bounds; duplicate/conflicting IDs; and any computed report beyond the hard limit.

For every HIDS report event:

1. Locate the context by HIDS CID and verify its connection handle/generation is current.
2. Verify service index, event Report ID, and plan exist.
3. Require event length at least one, require `event_data[0] == event_report_id`, then remove that BTstack-inserted byte.
4. Require the remaining payload length to equal the descriptor-computed length exactly. Neither short nor long packets are accepted.
5. Bounds-check every extraction against `payload_len * 8`, sign-extend only the declared field width, and reject values outside the declared logical range where applicable.
6. Publish a canonical state/delta; never copy arbitrary remote bytes into the USB queue.

Unknown IDs, unsupported maps, or malformed packets increment a bounded diagnostic counter and fail inertly. Repeated malformed packets should disconnect/quarantine that slot with backoff; they must not reset the other device or USB stack.

## 4. Concrete weaknesses in the current implementation

Severity follows the project brief. These are code-review findings, not hardware-test results.

| Severity | Finding and evidence | Consequence / required change |
|---|---|---|
| HIGH | `hog_host_demo.c:112-131` has one `app_state`, bonded/target address, connection handle, HIDS CID, descriptor store, and timer. SM and HCI handlers do not route by per-device context. `btstack_config.h:39-42` allows one GATT/HIDS client. | Raising a count cannot make two links safe. Use two contexts, handle/CID lookup, independent timers and teardown. |
| HIGH | `hog_host_demo.c:540-544` treats every `SM_EVENT_REENCRYPTION_COMPLETE` as success without checking status. | A missing key/authentication failure can proceed to HIDS discovery. Check event handle and status; never fall back to pairing outside explicit enrollment. |
| HIGH | `hog_host_demo.c:438-447` automatically confirms Numeric Comparison even though the Pico is configured `NoInputNoOutput`, and merely prints passkeys to an inaccessible UART. | Unexpected association methods are blindly accepted or unusable. Confirm only Just Works during an armed window; decline unexpected numeric/passkey events. |
| HIGH | `hog_host_demo.c:123-127,214-225` and `usb_descriptors.c:93-102,146-152` hand the unvalidated remote map directly to the USB host. | A malformed/hostile peripheral controls the USB HID grammar. Replace with fixed USB descriptors and canonical translation. |
| HIGH | `hog_host_demo.c:889-912` truncates reports to 512 bytes and queues raw data; `main.c:185-195` sends it to a 64-byte TinyUSB HID endpoint. Failed sends leave the queue head in place. | Descriptor/report mismatch and possible permanent head-of-line blocking. Require exact bounded lengths and publish canonical reports only. |
| HIGH | No disconnect/error path publishes all-keys-up or all-buttons-up. `HCI_EVENT_DISCONNECTION_COMPLETE` only resets the BLE singleton; `GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED` only logs. | Keys/buttons can remain stuck at the USB host. Centralize idempotent release on every terminal path. |
| HIGH | `ST_HID_RPT` has no source slot, handle, or generation; queued reports survive BLE disconnect until a USB reinitialization happens. | Stale input from an old connection can be delivered after disconnect or replacement. Use per-role canonical mailboxes/epochs and a release barrier. |
| HIGH | BTstack inserts a Report ID byte even when it is zero, while `main.c:187-191` forwards the event buffer verbatim and ignores the separately stored ID. | A no-Report-ID map receives an illegal leading zero on USB. Strip BTstack framing during translation. |
| MEDIUM | Advertising discovery accepts the first device with any HID Appearance `0x03C0..0x03CF` or HID UUID (`hog_host_demo.c:304-345,779-800`) and never parses role. | A joystick, gamepad, wrong role, or spoof can replace the singleton. Pair only in an explicit window and classify from the map. |
| MEDIUM | Getters always request HIDS service index zero; incoming `service_index` is discarded. | Additional HIDS instances are silently misrepresented. Reject them in release 1 and retain the index in all routing structures. |
| MEDIUM | A single raw TLV tag `HOGD` stores one compiler-layout-dependent struct; startup validates only its byte count (`hog_host_demo.c:81-82,638-653`). | No keyboard/mouse role separation, schema version, field validation, or corruption check. Use two versioned, checksummed records. |
| MEDIUM | The application calls `store_tag(HOGD, ...)` on every successful HIDS connection (`hog_host_demo.c:592-596`), even when identity is unchanged. | Needless flash page programs, eventual bank migrations/sector erases, and avoidable flash stalls. Write only on enrollment/replacement/clear. |
| MEDIUM | Authentication failure deletes the target, saved bond, and one application tag globally (`hog_host_demo.c:519-532`). | A dual implementation copied from this logic would destroy the healthy device's persistence. Quarantine/repair only the failing slot after physical authorization. |
| MEDIUM | Return status from `hids_client_connect()` is ignored (`hog_host_demo.c:549-554`); an input report can force global `READY` from any state (`889-897`). | Allocation/state errors can strand the machine or bypass orderly acceptance. Check every return and drop reports until the context is accepted. |

Two positive observations should be preserved: the code requests bonding/encryption before HIDS discovery, and `main.c:67-71` calls `flash_safe_execute_core_init()` on the non-Bluetooth core before launching the Bluetooth core, matching the Pico SDK's multicore flash-lockout requirement.

## 5. Disconnect and stuck-input prevention

Use a fixed USB descriptor from boot onward, so BLE connect/disconnect never re-enumerates USB. The USB side owns canonical role state; BLE events only publish validated changes.

Each role needs a monotonically increasing RAM-only generation. A message/mailbox update contains role, generation, connected flag, complete keyboard/button state, and bounded mouse deltas. USB discards updates from older generations. Mouse movement should use a checked accumulator, while buttons use latest state.

One idempotent `release_role(reason)` operation must be called on all of these paths: HCI disconnect; HIDS disconnect; pairing or re-encryption failure; descriptor rejection; malformed-report quarantine; connection timeout; local replacement; explicit unpair; and internal state-machine abort. It must:

1. increment the role generation before accepting any later report;
2. clear pending reports/movement for that generation;
3. publish keyboard modifiers/keys all zero or mouse buttons all zero (movement/wheel zero);
4. clear internal pressed state; and
5. leave the other role untouched.

Release cannot be a best-effort enqueue behind a full FIFO. Reserve a per-role high-priority mailbox or let a zero-state generation supersede the backlog. If USB is unmounted, clear internal state and make the first report after the next mount a zero state. Test duplicate and reordered disconnect callbacks; release must remain harmless when repeated.

## 6. Pairing security and no-display UX

### Realistic association methods

| Method | Pico hardware requirement | Release-1 decision |
|---|---|---|
| Just Works | No display/input; optional physical accept button | Supported only in explicit pairing mode. Encrypted and bonded, but **not MITM-authenticated**. |
| Passkey: Pico displays, user types on keyboard | A reliable six-digit display channel | Not supported. A single LED could technically encode digits, but that is error-prone and not a credible beginner UI. UART logs require wiring and are not normal-use UI. |
| Passkey: Pico enters number shown by peer | Decimal input | Not supported; Pico has no keypad and HOGP input is not available before pairing. |
| Numeric Comparison | Six-digit display plus Yes/No input | Not supported. Blind auto-confirmation is forbidden. |
| OOB | NFC, QR/camera, or another authenticated channel | Not available on the bare Pico W. |
| Fixed passkey | No extra hardware | Rejected; reuse/predictability defeats the intended authentication value. |

HOGP's conformance requirements make unauthenticated LE Security Mode 1 pairing mandatory for a Report Host and authenticated pairing optional. The Bluetooth Core association table likewise maps `NoInputNoOutput` to unauthenticated Just Works when no OOB channel exists. LE Secure Connections improves key establishment, but Just Works still does not authenticate the peer against an active MITM.

Recommended release policy:

- `NoInputNoOutput`, bonding required, 16-byte encryption key required.
- Prefer and, for the safety-first build, require LE Secure Connections. Legacy-pairing-only peripherals are unsupported unless a later explicitly labelled compatibility build accepts the weaker policy.
- Confirm `SM_EVENT_JUST_WORKS_REQUEST` only when the event handle belongs to the single currently armed candidate.
- Decline passkey input/display and numeric-comparison events; do not log-and-continue.
- After pairing, verify the resulting key size and secure-connections status from the LE device database before enrollment.
- Never auto-pair because a known device is absent. A spoofed identity with `PIN_OR_KEY_MISSING` is a repair condition, not permission to make a new bond.

This subset will exclude keyboards that insist on the familiar “type this six-digit code” flow. That is an explicit security/UI limitation, not a bug to work around by disabling authentication checks.

### Proposed physical workflow

Normal operation is non-bondable for application purposes: reconnect the two stored identities and ignore unknown HID advertisers. Pairing opens for 60 seconds only after a deliberate action.

- Hold BOOTSEL for 3 seconds: enroll an **empty** role only. No existing role can be overwritten.
- Hold BOOTSEL for 8 seconds: open replacement mode. A validated candidate's map determines the role; the old role remains active/persisted during validation.
- In either mode, repeating one-pulse pattern means a keyboard candidate and two pulses means a mouse candidate. A short BOOTSEL press confirms that candidate; nothing is persisted before this press. Rapid alternating blink means pairing/security in progress. Three slow pulses means unsupported/rejected. Solid means both roles connected; one brief pulse every two seconds means keyboard only; two means mouse only; slow even blink means neither.
- Confirmation times out after 10 seconds. Timeout/failure deletes only the uncommitted candidate bond and leaves the old role intact.

Runtime BOOTSEL is not an ordinary GPIO. Raspberry Pi's example executes from SRAM, disables interrupts, temporarily releases flash chip-select, and explicitly warns that it does not work while another core accesses flash. This project runs USB and Bluetooth on separate cores. Therefore the button workflow is **conditional** on a cross-core-safe, RAM-resident implementation and stress tests proving that polling does not disrupt USB, CYW43, or TLV writes. Do not copy the single-core example directly. If that gate fails, the safe no-solder fallback is a separately reviewed maintenance UF2 that clears role records through normal BOOTSEL flashing; do not use a flash-nuke image merely to re-pair.

The physical button proves user presence, not peripheral identity. With no display, the user must put only the intended device into pairing mode and keep the pairing window short. This residual Just Works risk must be documented.

## 7. Two-role persistence design

### Storage layers

Keep responsibilities separate:

- BTstack's `le_device_db_tlv` owns LTK/IRK/address-resolution/security material.
- The application owns only the mapping from a validated identity to `keyboard` or `mouse`.
- Report Maps, handles, RPAs, HIDS CIDs, connection state, retry timers, and pressed keys are not persistent. Rediscover and revalidate after boot.

Pico SDK 2.2.0 configures one BTstack TLV instance for both layers. On RP2040 its default reservation is two 4 KiB banks at the end of flash (8 KiB total). The BTstack backend writes a new value before its entry header, then invalidates the older tag; migration copies live entries to the erased alternate bank before writing that bank's newer epoch header. This gives useful interruption recovery, but the format has no per-value CRC. The application record needs its own integrity check.

The Pico W datasheet identifies a Winbond W25Q16JV. The manufacturer specifies 256-byte pages, 4 KiB sectors, and at least 100,000 program/erase cycles per sector. The Pico adapter programs complete 256-byte pages for mutations; TLV bank migration causes sector erase. This is ample for enrollment events, not a reason to write on every reconnect.

### Role records

Use independent tags so replacing one role never rewrites the other:

- keyboard tag: ASCII `XHKB` packed as a 32-bit TLV tag;
- mouse tag: ASCII `XHMS`.

Proposed 24-byte, explicitly serialized little-endian value (never persist a C struct directly):

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `XHP1` |
| 4 | 2 | Schema version = 1 |
| 6 | 2 | Record length = 24 |
| 8 | 1 | Role: 1 keyboard, 2 mouse |
| 9 | 1 | Identity address type: public or random identity only |
| 10 | 2 | Flags/reserved, must be zero |
| 12 | 6 | Identity address in documented BTstack byte order |
| 18 | 2 | Reserved, must be zero |
| 20 | 4 | CRC-32/ISO-HDLC over bytes 0..19 (`poly 0x04C11DB7`, reflected `0xEDB88320`, init/xorout `0xFFFFFFFF`) |

On load, require exact TLV length, magic, version, embedded length, tag/role agreement, legal address type, nonzero/non-`FF` identity, zero reserved fields, and valid CRC. Then enumerate the BTstack LE database for the same identity and require acceptable key size/security flags. A valid application record with no matching key is `needs repair`, not a partially connected role. Corruption in one tag must not invalidate the other.

### Enrollment/replacement transaction

1. Pair candidate during the explicit window; keep its reports quarantined.
2. Discover, bound-check, parse, classify, and obtain physical confirmation.
3. Verify the candidate bond exists with the required security properties.
4. Serialize/store the new role record under that role's tag; read it back and validate it.
5. Atomically switch the RAM role generation and release old input state.
6. Only now delete the old identity's BTstack bond if it differs.

Power loss before step 4 preserves the old role and may leave an orphan candidate bond. Power loss after step 4 selects the new role and may leave the old bond orphaned. Either orphan is safe because application role records alone authorize automatic reconnection. Cleanup may remove unreferenced bonds on a later explicit maintenance action; it must not guess a role from an orphan.

For an intentional clear, delete the application role tag first, then its matching BTstack bond. If interrupted, an orphan key remains but is not auto-connected. Never erase the whole TLV or the other role for a single-slot error.

### Write policy

Application `store_tag` is allowed only for successful enrollment/replacement or a schema migration; `delete_tag` only for explicit role clear/recovery. Boot, reconnect, successful encryption, HIDS discovery, reports, disconnects, backoff, LED changes, and last-seen timestamps cause **zero application flash writes**. A host-side spy test should enforce these counts.

## 8. Test implications

### Descriptor compiler tests

Golden accepts:

- no-ID boot keyboard; Report-ID keyboard; separate keyboard and Consumer Control collections; six-key array; NKRO bitmap; unusual but valid bit offsets; modifiers in a separate report;
- no-ID three-button mouse; five-button ID mouse; 16-bit relative X/Y; wheel; extra buttons/horizontal pan ignored;
- multiple top-level collections and IDs whose namespaces do not conflict.

Golden rejects:

- name/Appearance/map mismatches; mouse and keyboard in one candidate; more than one HIDS service; duplicate input ID; Report ID item zero; no relative X/Y; absolute pointer;
- every truncation point in 1-, 2-, and 4-byte short items; unsupported/truncated long item; collection/global-stack underflow and overflow; unclosed collection; reversed Usage range; missing globals; count/size/offset overflow; report over 64 bytes; too many IDs/fields/collections; conflicting computed lengths.

Use mutation/fuzz tests under ASan/UBSan on a host build. Assert termination, no allocation, no out-of-bounds access, and deterministic accept/reject output for the same bytes.

### Runtime report tests

- BTstack synthetic ID zero is stripped; nonzero IDs match both event metadata and byte zero.
- Unknown service/ID, zero-length, one-byte-short, one-byte-long, and over-limit packets are ignored/quarantined without USB output.
- Array keys, modifiers, release, NKRO up to six, seven-key rollover, and recovery from rollover.
- Mouse sign extension, 16-bit delta chunking without loss, five buttons, wheel, ignored extras, and high-rate reports under USB backpressure.
- Reports arriving before classification/confirmation, after disconnect, from an old handle/CID, or from an old generation never reach USB.

### Disconnect/security tests

Inject every terminal path while modifiers/keys/buttons are held. The next observable canonical USB report must be zero, even when the normal queue/mailbox is saturated. Repeat/disorder HCI and HIDS disconnect callbacks. Disconnect keyboard while mouse reports continue and vice versa.

Exercise Just Works success, pairing timeout/cancel, MITM-required failure, passkey display/input, unexpected Numeric Comparison, re-encryption success, key missing, wrong handle, and a spoofed advertising identity. Only the active candidate in the physical window may receive Just Works confirmation. A failure in one role must not delete or disconnect the other.

### Persistence/power-cut tests

- Empty, keyboard-only, mouse-only, and both-role boots; wrong length/version/role/address type/reserved bits; every single-bit corruption; matching record with missing/corrupt key; duplicate/orphan keys.
- Reset injection after candidate key creation, value write, entry-header write, old-tag invalidation, each migration copy chunk, new-bank header, role switch, and old-key deletion. After reboot, either old or new validated role may win at defined boundaries, but the other role must remain intact and no malformed record may be used.
- Instrument TLV calls: zero application writes on reports, reconnects, failures, ordinary boots, and power cycles; exactly one role-record store for a successful enrollment/replacement before any old-bond deletion.
- On hardware, repeat controlled power removal during replacement and run long reconnect loops while monitoring the two reserved sectors. These remain **NOT TESTED** until performed on a Pico W.

### BOOTSEL/UX tests

Stress runtime button sampling with both cores active, both BLE links reporting, USB at 1 ms polling, and TLV migration. Verify no USB enumeration loss, missed releases, CYW43 stalls, flash corruption, or entry into ROM BOOTSEL. Test debounce, 3/8-second separation, timeout, accidental taps, and power cycling while held. If any cross-core safety claim is not demonstrated, use the maintenance-UF2 fallback.

## 9. Assumptions and open limits

| Status | Statement |
|---|---|
| VERIFIED (code/source) | Current firmware has one BLE/HIDS context, one role-neutral TLV record, raw descriptor/report passthrough, and no disconnect release. |
| VERIFIED (upstream source) | BTstack 501e6d2 can allocate multiple HIDS clients when configured, shares/compacts descriptor storage, and prepends the Report ID to emitted reports. |
| DOCUMENTED | Keyboard/mouse identity is represented by Generic Desktop top-level usages; Report IDs multiplex report formats; GAP Appearance defines keyboard/mouse hints. |
| DOCUMENTED | `NoInputNoOutput` pairing provides Just Works without MITM authentication; HOGP host support for authenticated pairing is optional. |
| DOCUMENTED | Pico SDK's BTstack backend uses two flash banks; Pico W's W25Q16JV has finite sector endurance. |
| INFERRED, requires implementation test | Two canonical per-role USB interfaces/states avoid BLE Report-ID collisions and permit independent releases without re-enumeration. |
| INFERRED, requires upstream regression test | Limiting release 1 to one HIDS service per device avoids the observed shared-descriptor offset bug when instance counts differ. |
| UNVERIFIED | Which real keyboards accept Secure-Connections Just Works; passkey-demanding keyboards are intentionally unsupported. |
| UNVERIFIED | Runtime BOOTSEL polling can be made sufficiently safe and latency-neutral in this two-core CYW43/USB design. |
| UNVERIFIED | Physical power-cut behavior and long-term flash wear on the target board. |
| OUT OF SCOPE HERE | PC/Xbox recognition of the final canonical USB topology; this document only defines the BLE parsing, security, release, and persistence contract. |

## Release recommendation for this workstream

**NOT READY for firmware implementation approval until** the architecture adopts canonical translation, per-device contexts, explicit pairing mode, checked re-encryption status, guaranteed release mailboxes, and two versioned role records. Before expanding beyond the release-1 subset, separately resolve/test BTstack shared descriptor compaction, multiple HIDS services, authenticated pairing UI, consumer controls, horizontal pan, and end-to-end NKRO.
