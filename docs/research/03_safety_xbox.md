# Research C: Pico W safety, recovery, power, BOOTSEL, and Xbox constraints

Research date: 2026-09-01  
Repository baseline: `2c6a303d1f172e56b271283af978efdcc483a389` (`main`)  
Intended board in this assessment: Raspberry Pi Pico W (RP2040)

## Scope and evidence labels

This is a source and repository review only. **No Pico W, Xbox Series X, keyboard, mouse, USB cable, current meter, or other hardware was tested. This report does not claim Xbox compatibility.**

- **VERIFIED** — observed directly in this repository at the baseline above, or in the pinned upstream source used by it.
- **DOCUMENTED** — stated by Raspberry Pi, Microsoft/Xbox, USB-IF, or another named upstream project.
- **INFERRED** — engineering conclusion drawn from verified code and documented behavior; it still needs validation where noted.
- **UNVERIFIED** — not established by the available documentation or source review; requires a build artifact, measurement, or physical hardware test.

## Executive determination

- **DOCUMENTED:** A Pico W cannot normally be made permanently unrecoverable by bad application firmware. RP2040's USB BOOTSEL loader is in on-chip read-only ROM, while the application is in external QSPI flash. Holding BOOTSEL while connecting USB enters the ROM loader even when the application flash is invalid. Raspberry Pi explicitly describes this as protection against permanently bricking the board through software.
- **VERIFIED:** This repository contains no BOOTSEL-reading code and makes no `reset_usb_boot`/`rom_reset_usb_boot` call. Pressing BOOTSEL while the present application is already running is therefore not a pairing/reset command.
- **INFERRED:** Runtime use of BOOTSEL as a pairing button is possible only with careful flash-bus and multicore coordination. Raspberry Pi's example warns that its method is unsafe if the other core or another engine accesses execute-in-place flash concurrently. That warning applies directly to this dual-core USB/Bluetooth design.
- **VERIFIED:** The default board in `src/CMakeLists.txt:26` is `pico2_w`, not `pico_w`. A release for the requested Pico W must explicitly select and verify the Pico W board target.
- **VERIFIED:** The current application presents one vendor-defined TinyUSB HID interface, initially with keyboard, mouse, consumer-control, and gamepad report collections. After a BLE HID connection, it disconnects and reconnects USB with the BLE device's report descriptor.
- **DOCUMENTED:** Xbox supports ordinary wired USB keyboard and mouse input in the console platform, but game support is title-by-title and is controlled by each game developer. Generic USB HID gamepad data is not equivalent to an Xbox controller protocol.
- **UNVERIFIED:** The current descriptor, dynamic re-enumeration behavior, power draw, wake behavior, and BLE-to-USB reports have not been accepted or exercised by an Xbox Series X.

## 1. BOOTSEL, ROM recovery, and realistic brick risks

### What is protected

- **DOCUMENTED:** Pico W uses RP2040. RP2040 contains a boot ROM and has no internal application program flash; Pico W stores the second-stage loader, application, and application data in external QSPI flash.
- **DOCUMENTED:** At reset, RP2040 samples the flash chip-select path. BOOTSEL forces the ROM's USB device boot mode. If normal flash boot cannot load a valid second-stage image, ROM boot also provides a USB recovery path.
- **DOCUMENTED:** The BOOTSEL loader itself is in read-only memory inside RP2040. Application firmware cannot erase or replace this ROM.
- **DOCUMENTED:** The Pico SDK exposes `reset_usb_boot()` (and the underlying ROM API) so a running application may request a reboot into BOOTSEL. This transfers control to ROM; it does not rewrite the ROM.
- **VERIFIED:** This application does not call that API and contains no OTP, fuse, security-lock, overclock, or voltage-control operation.

### What application firmware can damage

- **DOCUMENTED:** Firmware with flash-write access can erase or corrupt external-flash content, including the second-stage loader, application image, and stored data. That may stop the application from booting or destroy pairing state.
- **INFERRED:** Such damage is a recoverable software failure, not a permanent brick, as long as the RP2040, USB connector/path, BOOTSEL mechanism, and power are physically functional.
- **INFERRED:** Physical faults—overvoltage, shorts, electrostatic discharge, connector damage, flash-chip damage, or a mechanically failed BOOTSEL switch—can prevent the standard recovery procedure. ROM recovery does not protect against physical damage.
- **UNVERIFIED:** Recovery has not been performed on the user's specific board, cable, or computer.

### Beginner-safe ROM recovery procedure

1. Disconnect the Pico W from the Xbox and from every other power source.
2. Hold the Pico W's **BOOTSEL** button.
3. While holding it, connect the Pico W to a computer using a known data-capable micro-USB cable.
4. Release BOOTSEL after the `RPI-RP2` mass-storage volume appears.
5. Copy a UF2 built specifically for **Pico W / `pico_w`** to that volume. The volume normally ejects and the board reboots automatically.
6. If a clean external-flash erase is required, use Raspberry Pi's official `flash_nuke.uf2`, then repeat the procedure with the correct application UF2. This intentionally erases the application and persisted BLE/TLV state.

- **DOCUMENTED:** This USB drag-and-drop recovery is performed from an ordinary computer host. An Xbox is not documented as a UF2 programming/file-copy host.
- **INFERRED:** Preserve a known-good Pico W UF2 and its source revision before testing new firmware.

Official sources:

- [Raspberry Pi Pico-series documentation — BOOTSEL mode and reset](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html#resetting-flash-memory)
- [Raspberry Pi Pico W datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf)
- [RP2040 datasheet — boot ROM and boot sequence](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Pico SDK 2.2.0 boot-ROM API](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_bootrom/include/pico/bootrom.h)

## 2. Runtime BOOTSEL caveat on Pico W

BOOTSEL is not wired as an ordinary, independent GPIO button. It affects the QSPI flash chip-select line.

- **DOCUMENTED:** Raspberry Pi's runtime-button example executes the sampling routine from SRAM, temporarily makes flash unavailable, and disables interrupts while it reads the BOOTSEL state.
- **DOCUMENTED:** The example explicitly says this approach does not work safely if another core or another engine, such as the XIP streamer, is trying to access flash at the same time.
- **VERIFIED:** This repository is multicore: USB runs on core 0 and BTstack/BLE runs on core 1 (`src/main.c:56-71`, `src/hog_host_demo.c:191-198`). Both application paths can execute code or read constants from external flash.
- **INFERRED:** Copying the example's `get_bootsel_button()` helper into this application without first quiescing core 1 and all interrupt/DMA/XIP users could stall or fault the system. A production design would need explicit cross-core coordination, SRAM-resident critical code/data, interrupt control, debounce, and a deliberate press gesture such as a long press.
- **DOCUMENTED:** Pico W's on-board LED is `WL_GPIO0` on the CYW43439 wireless device, not a directly controlled RP2040 GPIO. The activity-GPIO argument to `reset_usb_boot()` drives a direct RP2040 GPIO mask, so the on-board Pico W LED must not be assumed to work as the ROM-loader activity indicator. `reset_usb_boot(0, 0)` requests no activity GPIO and leaves both USB boot interfaces enabled.
- **UNVERIFIED:** No runtime BOOTSEL design has been implemented or tested for this repository. Its future pairing semantics, hold duration, flash coordination, and interaction with active USB/BLE traffic remain open design items.

Official source:

- [Raspberry Pi `pico-examples` SDK 2.2.0 runtime BOOTSEL example](https://github.com/raspberrypi/pico-examples/blob/sdk-2.2.0/picoboard/button/button.c)

## 3. External flash and BTstack TLV safety

### Current persistence behavior

- **VERIFIED:** The build links `pico_flash` and `pico_btstack_flash_bank` (`src/CMakeLists.txt:58-69`).
- **VERIFIED:** `flash_safe_execute_core_init()` runs on core 0 before core 1 starts (`src/main.c:67-71`). The custom BTstack TLV operations run from the BLE host on core 1.
- **DOCUMENTED:** Pico SDK requires `flash_safe_execute_core_init()` on the *other* core when `pico_multicore` coordination is used. The current initialization pattern therefore matches a core-1 flash writer coordinating with core 0.
- **INFERRED:** If a future feature performs flash-safe operations from core 0, the present one-sided arrangement cannot simply be assumed sufficient. The writer/other-core roles and all callers must be audited again.
- **VERIFIED:** On every successful HIDS service connection, the code calls `store_tag` for custom tag `TLV_TAG_HOGD` (`src/hog_host_demo.c:592-596`), even when the bonded identity did not change. It does not check the storage result.
- **VERIFIED:** On startup, a stored identity is accepted when the returned byte count equals `sizeof(bonded_device)` (`src/hog_host_demo.c:641-650`). The custom record has no application-level magic, schema version, checksum, or semantic validation.
- **VERIFIED:** Authentication/key failures delete both the custom identity tag and BTstack bonding information (`src/hog_host_demo.c:521-531`).

### SDK storage boundaries and interruption behavior

- **DOCUMENTED:** In Pico SDK 2.2.0 on RP2040, the default BTstack flash-bank backend reserves two flash sectors at the end of external flash. Each RP2040 bank is one 4096-byte sector, for 8192 bytes total by default.
- **DOCUMENTED:** The backend range-checks bank and offset accesses and uses `flash_safe_execute()` for erase/program operations. No flash-resident code or data may be accessed during the physical erase/program window.
- **DOCUMENTED:** The backend has an overlap assertion against `__flash_binary_end`, but that runtime check is compiled only when `NDEBUG` is not defined.
- **INFERRED:** A release pipeline must inspect the linked image/map and confirm that the application ends below the configured BTstack storage offset. A release must not depend on the debug-only assertion as its sole overlap guard.
- **DOCUMENTED:** BTstack's two-bank TLV algorithm writes a value before its tag header, migrates valid entries into an erased alternate bank, and writes the new bank header last. These ordering choices are designed to make interrupted/partial records ignorable and preserve a previously valid bank during migration.
- **INFERRED:** That algorithm reduces common power-loss corruption risk but does not provide semantic validation for this application's unversioned `bonded_device` structure. A torn write, stale format, unexpected but correctly sized bytes, or unchecked storage failure can still result in lost or misleading application state.
- **INFERRED:** Unconditionally rewriting the same custom tag increases flash program traffic and eventually bank migrations/sector erases. The custom identity should be stored only when its value changes, the result should be checked, and the record should gain a magic value, schema version, length, and integrity check before release.
- **UNVERIFIED:** Actual write frequency, sector erase count, flash-part endurance, corruption behavior under arbitrary power removal, and long-term retention have not been measured on the target Pico W. No specific service-life claim is justified by this review.

Official/upstream sources:

- [Pico SDK 2.2.0 flash-safe API](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_flash/include/pico/flash.h)
- [Pico SDK 2.2.0 BTstack flash-bank configuration](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_btstack/include/pico/btstack_flash_bank.h)
- [Pico SDK 2.2.0 BTstack flash-bank implementation](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_btstack/btstack_flash_bank.c)
- [BlueKitchen BTstack two-bank TLV implementation](https://github.com/bluekitchen/btstack/blob/master/platform/embedded/btstack_tlv_flash_bank.c)

## 4. USB power expectations

### Board electrical limits

- **DOCUMENTED:** Pico W's micro-USB `VBUS` input is specified as `5 V ±10%`. Its `VSYS` operating input range is 1.8 V to 5.5 V. The normal, simplest power path is the micro-USB connector through the board's Schottky diode; no external GPIO power wiring is required for USB-device use.
- **INFERRED:** For this project, power the unmodified Pico W only through its micro-USB connector. Do not simultaneously inject another supply into `VSYS`/`VBUS` unless a separately reviewed power-sharing design requires it.

### What the current USB descriptor asks for

- **VERIFIED:** The dynamically generated configuration descriptor sets `bMaxPower` to 250 (`src/usb_descriptors.c:160-165`). USB 2.0 encodes this field in 2 mA units, so the descriptor declares a maximum configured consumption of 500 mA.
- **DOCUMENTED:** Under classic USB 2.0 rules, a bus-powered function may draw at most one unit load (100 mA) before it is configured and must not exceed its declared/available configured current. A suspended high-power function is generally limited to an average of 2.5 mA under the USB 2.0 suspend rules. See USB 2.0 sections 7.2.1, 7.2.3, and the configuration-descriptor definition in 9.6.3.
- **INFERRED:** A 500 mA `bMaxPower` declaration is within the USB 2.0 descriptor range, but it is neither a current measurement nor a promise that every host port will supply 500 mA in every state.
- **VERIFIED:** The application starts the BLE/CYW43 stack during normal startup and performs no explicit radio, clock, or peripheral power reduction in `tud_suspend_cb()` (`src/main.c:150-156`).
- **VERIFIED:** The descriptor advertises remote-wakeup capability, while `tud_suspend_cb()` ignores the host-provided `remote_wakeup_en` value. The HID path calls `tud_remote_wakeup()` whenever the device is suspended (`src/main.c:177-197`); the application itself does not gate the call on the callback's permission flag.
- **UNVERIFIED:** Whether the pinned TinyUSB layer suppresses signaling when the host did not enable remote wake was not established in this review. USB electrical/compliance testing should include both permitted and non-permitted remote-wake cases.
- **UNVERIFIED:** Pre-configuration current, BLE transmit peaks, steady current, suspend current, inrush, cable voltage drop, and behavior during USB reconnect have not been measured.
- **UNVERIFIED:** Microsoft does not publish, in the Xbox sources reviewed here, a guaranteed Xbox Series X per-port current budget for this device class. Do not substitute an assumed console-port rating for device-side USB compliance.

Official sources:

- [Raspberry Pi Pico W datasheet — power and USB](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf)
- [USB-IF USB 2.0 specification download page](https://www.usb.org/document-library/usb-20-specification)

## 5. Current repository USB enumeration behavior

### Normal application USB, not BOOTSEL USB

- **VERIFIED:** SDK USB stdio is disabled and UART stdio is enabled (`src/CMakeLists.txt:94-96`). The normal application USB device is the project's TinyUSB HID implementation, not the SDK's USB serial console.
- **VERIFIED:** TinyUSB device mode initializes on core 0 (`src/main.c:56`), and `tud_task()` plus HID transmission run in the core-0 loop (`src/main.c:129-130`, `src/main.c:177-197`).
- **VERIFIED:** Only one TinyUSB HID interface is enabled; CDC, MSC, MIDI, and vendor interfaces are disabled (`src/tusb_config.h`). The interface uses one interrupt-IN endpoint, address `0x81`, with a 64-byte packet size and 1 ms full-speed interval (`src/usb_descriptors.c:168-202`).
- **VERIFIED:** The device descriptor uses VID `0xCAFE`, computed PID `0x4004`, USB 2.0, and device-level class/subclass/protocol zero (`src/usb_descriptors.c:37-40`).
- **UNVERIFIED:** Ownership/suitability of that VID/PID for distribution was not established. A release needs an assigned or otherwise authorized USB identity and a host-cache/revision strategy.

### Descriptor changes after BLE connects

- **VERIFIED:** Before BLE HIDS is ready, the fallback report descriptor exposes keyboard (report ID 1), mouse (ID 2), consumer control (ID 3), and gamepad (ID 4) collections (`src/usb_descriptors.c:82-102`). These are collections within one HID interface, not four USB interfaces.
- **VERIFIED:** After one BLE HID device connects, the USB HID report descriptor is replaced with that BLE device's report descriptor. The application disconnects USB, waits approximately 100 ms, clears queued reports, and reconnects (`src/main.c:105-127`; `src/usb_descriptors.c:146-202`). VID, PID, interface number, and endpoint remain the same while report-descriptor content and length may change.
- **VERIFIED:** The bridge supports one configured HIDS client and one active stored identity in the current implementation.
- **INFERRED:** Hosts can cache USB metadata by VID/PID and topology. Reusing the same identity while changing the report descriptor may produce host-specific stale-descriptor or reconnect behavior. A 100 ms software delay does not establish that a particular host has discarded its previous HID state.
- **UNVERIFIED:** Xbox Series X caching, disconnect timing, reconnect acceptance, descriptor-size limits, report-ID handling, and tolerance of a BLE-derived arbitrary HID descriptor are not publicly specified by Microsoft and were not tested here.
- **UNVERIFIED:** The fallback's mixed keyboard/mouse/consumer/gamepad descriptor is not evidence that Xbox will treat the device as any one of those categories.

## 6. Xbox Series X keyboard/mouse and controller constraints

### What Microsoft documents

- **DOCUMENTED:** Microsoft documents native keyboard and mouse device support in the Xbox Game Development Kit, and its supported-platform tables include Xbox Series consoles.
- **DOCUMENTED:** Xbox's public guidance says wired USB keyboards and mice can be used in select games/apps, while a keyboard can additionally be used to get around Xbox. Its support page specifically cautions that a mouse cannot navigate Home or the Xbox user interface and that a controller is needed to configure the mouse.
- **DOCUMENTED:** Game-level keyboard/mouse support is not universally enabled. Xbox states that support is added title-by-title and is at the game developer's discretion; users must check the individual game.
- **DOCUMENTED:** Microsoft's console input documentation treats keyboard and mouse as their own input categories. For controller/gamepad protocol on console, it documents Xbox Gaming Input Protocol (GIP) devices—not arbitrary generic USB HID gamepads.

### Consequences for this bridge

- **INFERRED:** The legitimate compatibility target is an ordinary wired USB keyboard/mouse bridge. It must not be described as an Xbox controller, controller emulator, or authentication bypass.
- **VERIFIED:** This repository implements generic TinyUSB HID reports only. No Xbox GIP stack, licensed-controller authentication, proprietary packet injection, console modification, or security bypass was found.
- **INFERRED:** The fallback gamepad collection should not be interpreted as controller compatibility. Generic HID gamepad reports do not satisfy Microsoft's documented console controller protocol. A keyboard/mouse release should omit unrelated fallback gamepad claims and should expose only the report collections actually required by the accepted keyboard/mouse design.
- **UNVERIFIED:** Microsoft's public documentation reviewed here does not establish whether Xbox Series X accepts this specific one-interface/multiple-report-ID layout, a dynamically replaced report descriptor, the current VID/PID, the current reconnect timing, consumer-control reports, or reports proxied from an arbitrary BLE HID device.
- **UNVERIFIED:** No game was run, no Xbox dashboard navigation was attempted, no USB enumeration trace was captured from an Xbox, and no end-to-end latency was measured. Any statement stronger than “candidate generic USB HID keyboard/mouse device” would exceed the evidence.

Official Microsoft/Xbox sources:

- [Xbox Support — Navigate with a mouse and keyboard](https://support.xbox.com/en-US/help/hardware-network/accessories/mouse-keyboard)
- [Xbox Wire June 2025 update — wired USB keyboard/mouse support and title list](https://news.xbox.com/en-us/2025/06/23/xbox-june-update-copilot-for-gaming-aggregated-gaming-library/)
- [Xbox Wire — mouse and keyboard support is title-by-title](https://news.xbox.com/en-us/2018/09/25/mouse-and-keyboard-support-for-xbox-one-developers/)
- [Microsoft GDK — keyboard and mouse input](https://learn.microsoft.com/en-us/xbox/gdk/docs/features/common/input/advanced/input-keyboard-mouse)
- [Microsoft GDK — input hardware interfaces and console GIP constraint](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/hardware/input-hardware-interfaces)
- [Microsoft GDK — `GameInputMouseState` supported platforms](https://learn.microsoft.com/en-us/xbox/gdk/docs/reference/input/gameinput/structs/gameinputmousestate)

## 7. Safety and release gates

These are research conclusions, not firmware changes.

1. **VERIFIED blocker:** Build with an explicit `PICO_BOARD=pico_w`; capture the CMake configuration, UF2 metadata, and map file. The repository currently defaults to `pico2_w`.
2. **INFERRED gate:** Keep physical BOOTSEL recovery independent of pairing UX. If runtime BOOTSEL is later required, design and review a multicore-safe SRAM routine before implementation.
3. **INFERRED gate:** Add build-time/map validation that the image cannot overlap the final two flash sectors reserved for BTstack; test erased, malformed, previous-version, and interrupted TLV records.
4. **INFERRED gate:** Store the custom bonded identity only when changed, validate storage results, and version/integrity-protect the record.
5. **INFERRED gate:** Measure current from connection through configuration, BLE activity, suspend, remote wake, and USB re-enumeration. Do not release based only on `bMaxPower`.
6. **INFERRED gate:** Make remote-wake behavior honor the host's enable state and verify it with USB traces/compliance tooling.
7. **INFERRED gate:** Freeze a minimal, deterministic keyboard/mouse USB descriptor and authorized VID/PID strategy before Xbox testing. Treat dynamic BLE-derived descriptors as a compatibility risk until proven otherwise.
8. **UNVERIFIED gate:** Test on a real Xbox Series X with the exact Pico W build, cable, keyboard, mouse, and target game. Record enumeration, reconnect behavior, supported inputs, power behavior, latency, and failure recovery. Until then, publish no Xbox-compatibility claim.

## Bottom line

- **DOCUMENTED:** Bad Pico W application firmware is recoverable through immutable ROM BOOTSEL under normal working-hardware conditions.
- **VERIFIED:** The present firmware does not repurpose BOOTSEL; its normal USB identity is a dynamically re-enumerated generic HID device, and its build currently defaults to the wrong board family for Pico W.
- **INFERRED:** The main engineering risks are runtime BOOTSEL/flash concurrency, release-build flash-bank overlap assurance, unchecked/redundant TLV writes, unmeasured USB power/wake behavior, and descriptor compatibility.
- **UNVERIFIED:** Xbox Series X acceptance and game behavior remain entirely untested.
