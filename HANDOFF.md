# Agent handoff

Last updated: 2026-09-02 (America/Toronto)

## Objective and release boundary

Finish and validate defensive firmware for an original Raspberry Pi Pico W that
bridges one BLE HID keyboard and one BLE HID mouse to two fixed USB HID boot
interfaces. The target host is an Xbox Series X, but no physical Pico W,
keyboard, mouse, or Xbox had been tested when the software gate was completed.
Physical testing is now in progress as recorded below. Until the remaining
tests pass, the only truthful release label is **PRE_HARDWARE_TEST / HARDWARE
TEST IN PROGRESS**. Never describe it as Xbox-tested or production-ready.

Repository: `https://github.com/n-pouliot/xbox-pico` (private)

Branch: `main`

Upstream baseline: `2c6a303d1f172e56b271283af978efdcc483a389`
(`20260830_7`)

## Live hardware status (2026-09-02)

- Original Pico W BOOTSEL flashing and normal reboot physically passed.
- Logitech MX Mechanical Easy-Switch slot 3 completed direct BLE pairing with
  fixed passkey `739241`; typing reached Windows through the Pico with Windows
  Bluetooth disconnected. This verifies the real keyboard data path.
- Logitech M196 (M-R0114) entered pairing mode and Windows could detect it, but
  mouse enrollment did not complete on passive-scan firmware SHA-256
  `FF0EE7D18E4A4F0E420EE7AE4BC2990A069714BBE272E3A277270BA28ED0690F`.
- The active-scan candidate
  `release/pico_w_dual_ble_hid_bridge_M196_ACTIVE_SCAN_TEST.uf2`, SHA-256
  `E40CFF334A855924A02FBE1B62E1FE35740F93F7E81F8BBF742448766082383D`, found
  the mouse but did not enroll it. The diagnostic build then repeatedly showed
  failure stage 2, proving that security negotiation—not discovery, HIDS, or
  Report Map parsing—was the blocking stage.
- The mouse-only security policy was broadened to the mandatory HOGP
  unauthenticated Level 2 baseline: encrypted bonded Just Works, 7- to 16-byte
  keys, with legacy fallback accepted. The strict keyboard policy is unchanged.
- `release/pico_w_dual_ble_hid_bridge_M196_COMPATIBILITY_TEST.uf2`, SHA-256
  `7FA5350C1624AB35790336BA7B0E118A837E3831F66C1D5CA23C2DB2306C3A78`, then
  physically paired the M196. Mouse input and keyboard input both worked on
  Windows. The LED first showed the mouse-only two-pulse state while the saved
  keyboard reconnected, then became solid, confirming both roles ready.
- The user then connected the same Pico, MX Mechanical, and M196 to an Xbox
  Series X. Keyboard input worked in the console UI, the dashboard did not react
  to the mouse as expected, and both keyboard and mouse input worked inside The
  Sims 4, whose Store capability includes `Console Keyboard & Mouse`. This is a
  title-specific end-to-end Xbox pass, not a claim for every game.
- PC descriptor capture, extended simultaneous input, and power-cycle reconnect
  stress remain pending.

The original four release binaries were built from first-run compatibility
checkpoint `7d506fc28cb67bf3c629a4656508233419c6bf14`. The current normal binary was
rebuilt after active scanning, failure diagnostics, and the M196 mouse-security
compatibility change; its authoritative hash is recorded below and in
`release/SHA256SUMS.txt`.

The final artifact/documentation update is the newest commit on `main` after
the final push. GitHub reported repository visibility `PRIVATE`; verify local
and remote synchronization with `git status -sb` before resuming.

## Current implementation

- USB enumerates one keyboard interface and one mouse interface at boot; BLE
  state never changes USB descriptors or interface count.
- BLE enrollment and reconnect support one keyboard plus one mouse, with
  serialized connection/discovery work and persisted role assignments.
- Keyboard responder-input pairing uses fixed public passkey `739241` with the
  Pico as `DisplayOnly` and requires an authenticated 16-byte Secure
  Connections bond. No-input/no-output mice use encrypted bonded Just Works
  with 7- to 16-byte keys; LE Secure Connections is preferred and legacy
  fallback is accepted for HOGP Level 2 interoperability. Numeric Comparison,
  Pico-input passkey, and OOB are declined.
- Active scanning requests scan responses, improving support for peripherals
  that omit useful HID identity fields from their initial advertisement.
- A rejected connection displays a long green lead pulse plus one to six short
  pulses for 20 seconds: connection, security, HIDS service, Report Map,
  runtime report, or internal failure respectively.
- An accepted keyboard passkey request resets the onboard LED to a distinctive
  three-short-flashes-and-pause prompt, telling the beginner exactly when to
  type the code. Empty-role enrollment remains bounded but now lasts 180
  seconds.
- The empirical MX Mechanical keyboard-interface NKRO descriptor captured via
  Logi Bolt is a regression fixture and normalizes correctly. Direct BLE map
  identity/topology remains a hardware gate.
- Report-map parsing is bounded and translates only a strict supported subset.
- Cross-core keyboard/mouse mailboxes provide release barriers and overflow
  fail-safe behavior.
- USB suspend quiesces BLE; resume restarts it.
- Maintenance firmware variants clear keyboard, mouse, or all pairing state.
- Rejected enrollment cleanup is limited to one exact matching identity in a
  database slot that was empty before enrollment.
- Failed Bluetooth radio power requests are retried after a bounded backoff;
  internal recovery remains requested until an off/on cycle succeeds.

## Verification completed so far

- Strict host tests: PASS.
- Clang ASan + UBSan host tests: PASS. On Windows, add
  `C:\Program Files\LLVM\lib\clang\22\lib\windows` to `PATH` so the test
  executable can load `clang_rt.asan_dynamic-x86_64.dll`.
- Clang static analyzer over host-compatible policy/parser/store/mailbox code:
  PASS with no diagnostics at checkpoint `7d506fc`; not rerun after the M196
  follow-up.
- MSVC 19.44 Release with `/W4 /WX /permissive-`: PASS, 1/1 CTest at the
  software-gate checkpoint.
- Pico W Release cross-build with Pico SDK 2.2.0 and Arm GNU 14.2.1: all four
  targets PASS at checkpoint `7d506fc`. The changed normal target and current
  Clang ASan/UBSan host test both passed again after the M196 policy change.
- At the initial software checkpoint, two empty-directory same-day release
  builds produced identical UF2 hashes:
  normal `FF0EE7D18E4A4F0E420EE7AE4BC2990A069714BBE272E3A277270BA28ED0690F`,
  keyboard clear `7C83902AC8CEE984B2B6E0415232559616EED425F2C6F843ED37E4CABA62D65C`,
  mouse clear `A32CE5B2CE34677EB433381DB319CBCDD8E5387DEF1F3614ED8D2B77610CAE65`,
  and clear-all `7AC4A193BCF73AB2BE4DCFC97AA8E5C4D9412CC181D1068A69F2233B93D6DC5D`.
- Largest observed project-owned callback frame: 152 bytes. Core 1 has an
  explicit 8 KiB stack plus a 128-byte canary.

MX Mechanical and M196 interoperability plus basic simultaneous input have
passed on Windows. Both devices also passed end-to-end through the Pico on Xbox
Series X in The Sims 4. Extended simultaneous use, reconnect stress, USB
suspend current, and broader Xbox/game acceptance remain **NOT TESTED**.

The current normal ELF is 474,040 B `text`, 0 B `data`, and 46,080 B `bss`;
its binary end is `0x10072BBC`, well below the two 4 KiB BTstack banks starting
at `0x101FE000` on the 2 MiB board. The current normal UF2 is 940,032 B.

## Pinned build environment

- CMake: `C:\Program Files\CMake\bin\cmake.exe`
- Ninja: `C:\Users\n_p-i\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe`
- LLVM/Clang: `C:\Program Files\LLVM\bin`
- Arm GNU: locate the `arm-none-eabi-gcc.exe` recorded in the final build's
  `CMakeCache.txt`; toolchain version must be 14.2.1.
- Pico SDK: `C:\Users\n_p-i\.cache\xbox-pico-tools\pico-sdk-2.2.0`
- Python with PyCryptodome:
  `C:\Users\n_p-i\AppData\Local\Programs\Python\Python312\python.exe`

For deterministic BTstack GATT generation, always pass the Python 3.12 path
above. The machine's Python 3.14 lacks `Crypto`, causing a random generated GATT
database hash and therefore non-reproducible binaries.

When configuring an empty Pico build directory from the repository's `src/`
entry point, put LLVM, Arm GNU, CMake, and
Ninja on `PATH`; set host `CC=clang.exe`, `CXX=clang++.exe`, and
`RC=llvm-rc.exe`; and pass:

```text
-DPICO_BOARD=pico_w
-DPICO_SDK_PATH=C:\Users\n_p-i\.cache\xbox-pico-tools\pico-sdk-2.2.0
-DPICO_COMPILER=pico_arm_cortex_m0plus_gcc
-DPython3_EXECUTABLE=C:\Users\n_p-i\AppData\Local\Programs\Python\Python312\python.exe
-DCMAKE_BUILD_TYPE=Release
-G Ninja
```

The host compiler variables are needed by nested host tools; the top-level Pico
compiler remains Arm GNU because `PICO_COMPILER` is explicitly pinned.

The exact packaged files are in `release/`; `release/SHA256SUMS.txt` is the
authoritative manifest. The normal image is
`pico_w_dual_ble_hid_bridge_PRE_HARDWARE_TEST.uf2`; it is byte-identical to the
physically successful `pico_w_dual_ble_hid_bridge_M196_COMPATIBILITY_TEST.uf2`.

## Continuation checklist

All concrete findings from the independent code and adversarial reviews were
resolved and regression-checked. A focused review of the first fixed-passkey
revision found a HIGH Just Works downgrade; checkpoint `e595ebf` fixed it. The
same reviewer then returned explicit `APPROVE`, confirming that keyboard
candidates now require both passkey-event evidence and an authenticated bond
while mouse Just Works remains accepted.

1. Continue at Stage 4/5 of `docs/first-hardware-test.md`: verify every M196
   button and wheel action, then exercise both devices together for 15 minutes.
   Next, perform the ten-second Pico power-cycle reconnect test. The Xbox
   Series X / The Sims 4 keyboard-and-mouse stage has passed; do not generalize
   it to unsupported games or promote the release label until the remaining
   hardware gates pass.
2. If any hardware stage fails, preserve the exact firmware hash, device model,
   LED state, PC descriptor capture, and reproduction steps before changing
   code.

For the user's first keyboard enrollment: flash the normal UF2 on a Windows PC,
do not use Windows Bluetooth settings, hold MX Mechanical Easy-Switch `3` for
about three seconds until rapid blinking, wait for the Pico's repeating
three-short-flashes-and-pause prompt, type `739241` on the MX Mechanical, and
press Enter. Then put only the separate BLE mouse into pairing mode. The G502
LIGHTSPEED is not a BLE candidate. If the 180-second window expires,
power-cycle the Pico normally without BOOTSEL and retry only the missing role.

The `release` directory now contains `START-HERE.txt` and a double-click
`RUN-WINDOWS-DIAGNOSTIC.cmd` wrapper. The diagnostic is read-only with respect
to device state and writes a targeted USB/BOOTSEL report to the user's Desktop.

## Known residual limitation

The pinned BTstack HIDS client does not expose enough characteristic metadata to
prove that a peripheral has no duplicate input characteristics sharing a Report
ID. The bridge contains this by enforcing strict report framing and lengths and
disconnecting immediately on invalid data. This remains an accepted,
documented interoperability risk for the first hardware tests.

## Recovery warning

If power fails or the Pico is unplugged while a role-specific maintenance UF2 is
clearing pairing state, boot and run the **clear-all** maintenance UF2 before
re-enrolling. A role tag can disappear before the corresponding BLE database
entry; rerunning only the same role-clear image may then have nothing left to
identify. The clear-all variant is the deterministic recovery path.
