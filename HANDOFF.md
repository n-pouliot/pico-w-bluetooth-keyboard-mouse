# Agent handoff

Last updated: 2026-09-01 (America/Toronto)

## Objective and release boundary

Finish and validate defensive firmware for an original Raspberry Pi Pico W that
bridges one BLE HID keyboard and one BLE HID mouse to two fixed USB HID boot
interfaces. The target host is an Xbox Series X, but no physical Pico W,
keyboard, mouse, or Xbox has been tested in this workspace. Until those tests
pass, the only truthful release label is **PRE_HARDWARE_TEST / READY FOR HARDWARE
TEST**. Never describe it as Xbox-tested or production-ready.

Repository: `https://github.com/n-pouliot/xbox-pico` (private)

Branch: `main`

Upstream baseline: `2c6a303d1f172e56b271283af978efdcc483a389`
(`20260830_7`)

Release binaries were built from source checkpoint: `2b4255c`

Packaged artifact/documentation checkpoint `6d84328` is pushed to
`origin/main`; GitHub reported repository visibility `PRIVATE`. The working
tree and `origin/main` were synchronized immediately after that push.

## Current implementation

- USB enumerates one keyboard interface and one mouse interface at boot; BLE
  state never changes USB descriptors or interface count.
- BLE enrollment and reconnect support one keyboard plus one mouse, with
  serialized connection/discovery work and persisted role assignments.
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
  PASS with no diagnostics after checkpoint `2b4255c`.
- MSVC 19.44 Release with `/W4 /WX /permissive-`: PASS, 1/1 CTest.
- Pico W Release cross-build with Pico SDK 2.2.0 and Arm GNU 14.2.1: all four
  targets PASS at checkpoint `2b4255c`.
- Two empty-directory same-day release builds produced identical UF2 hashes:
  normal `F8E536B517E6DD82881E52D4A87060EF4EF522FEE5BEE3F692DB04FA6560A819`,
  keyboard clear `7C83902AC8CEE984B2B6E0415232559616EED425F2C6F843ED37E4CABA62D65C`,
  mouse clear `A32CE5B2CE34677EB433381DB319CBCDD8E5387DEF1F3614ED8D2B77610CAE65`,
  and clear-all `7AC4A193BCF73AB2BE4DCFC97AA8E5C4D9412CC181D1068A69F2233B93D6DC5D`.
- Largest observed project-owned callback frame: 160 bytes. Core 1 has an
  explicit 8 KiB stack plus a 128-byte canary.

Physical BLE interoperability, USB suspend current, Xbox USB acceptance, and
end-to-end input behavior are **NOT TESTED**.

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
`pico_w_dual_ble_hid_bridge_PRE_HARDWARE_TEST.uf2`.

## Continuation checklist

All concrete findings from the visible independent code and adversarial review
reports were resolved and regression-checked. Post-fix reviewer turns completed
without returning a visible verdict payload through the task runner; no new
finding was delivered. If an organizational process requires a literal
independent `APPROVE`, repeat a narrow read-only audit of the immutable pushed
commit rather than treating the missing payload as approval or rejection.

1. Follow `docs/first-hardware-test.md` when the board arrives and record every
   result. Do not promote the release label until the hardware gates pass.
2. If any hardware stage fails, preserve the exact firmware hash, device model,
   LED state, PC descriptor capture, and reproduction steps before changing
   code.

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
