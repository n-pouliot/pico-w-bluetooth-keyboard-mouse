# Test report

## Current gate status

**READY FOR HARDWARE TEST under the PRE_HARDWARE_TEST label.**

All available software-only gates pass on first-run compatibility source
checkpoint `7d506fc`. No physical hardware was available. Pico W boot, BLE peripherals, PC
USB enumeration, current, latency, suspend behavior, and Xbox behavior remain
**NOT TESTED** and cannot be inferred from these results.

## Environment

| Component | Version / revision used |
|---|---|
| Windows | Windows 11 |
| Upstream baseline | `2c6a303d1f172e56b271283af978efdcc483a389` |
| Pico SDK | 2.2.0, `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| BTstack | `501e6d2b86e6c92bfb9c390bcf55709938e25ac1` |
| TinyUSB | 0.18.0, `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` |
| CYW43 driver | `dd7568229f3bf7a37737b9e1ef250c26efe75b23` |
| Arm GNU Toolchain | 14.2.Rel1 / GCC 14.2.1 |
| MSVC | 19.44.35228.0 |
| CMake | 4.4.3 |
| Ninja | 1.13.2 |
| Python | 3.12.10 |
| PyCryptodome | 3.23.0 |
| LLVM/Clang host tools | 22.1.8 |

`cppcheck` and `clang-tidy` were not installed and therefore were **NOT RUN**.
This is disclosed rather than treated as a pass.

## Host logic tests

The native executable has one CTest target, four suites, and 30 named scenario
groups:

- 12 HID Report Map/parser/normalizer groups, including malformed and mutated
  descriptors, keyboard arrays/NKRO, the empirical MX Mechanical descriptor,
  and 8/16-bit mouse movement;
- five persistence/CRC/layout/validation groups;
- six mailbox groups, including per-interface resync, generation/release
  barriers, boot mouse shape, delta chunking, and overflow fail-safe behavior;
- seven BLE policy groups covering callback forms/framing, Appearance, address
  matching, radio restart/backoff decisions, exact bond-removal authority, and
  fixed-passkey display-event authorization plus the passkey LED prompt.

The mutation group flips every bit in the canonical boot-keyboard descriptor
and compiles/exercises 5,000 deterministic pseudorandom descriptors up to 255
bytes.

| Test configuration | Result | Evidence boundary |
|---|---|---|
| Clang 22 Release, strict warnings as errors | PASS — 1/1 CTest target | Native pure logic only |
| MSVC Release, `/W4 /WX /permissive-` | PASS — 1/1 CTest target | Native pure logic only |
| Clang AddressSanitizer + UndefinedBehaviorSanitizer | PASS — 1/1 CTest target | Native pure logic; Windows leak sanitizer unavailable |

On Windows, the sanitizer runtime directory
`C:\Program Files\LLVM\lib\clang\22\lib\windows` was added to `PATH`. Without
it, Windows reports a missing `clang_rt.asan_dynamic-x86_64.dll`; that loader
error is not a firmware failure.

## Static and build checks

| Check | Result | Notes |
|---|---|---|
| Clang static analyzer | PASS — no diagnostics | Four host-compatible production units: parser, pairing store, mailbox, BLE policy |
| Project-source compiler warnings | PASS | `-Wall -Wextra -Wpedantic -Werror` on every project-owned Pico C unit |
| Arm Release build | PASS — 4/4 targets | Original `pico_w` / RP2040, SDK 2.2.0, GCC 14.2.1 |
| Empty-directory build A | PASS | Python 3.12.10 with PyCryptodome pinned |
| Empty-directory build B | PASS | Separate directory, same pinned inputs |
| Same-day reproducibility | PASS — 4/4 UF2 hashes identical | UF2 embeds build date; cross-date equality not claimed |
| Picotool inspection | PASS | RP2040, `pico_w`, SDK 2.2.0, Release, 2026-09-01 |
| Stack-usage inspection | PASS for software gate | Largest observed project-owned frame 152 B; explicit 8 KiB Core-1 stack plus 128 B canary |

The only warning in clean firmware-build output came from the separately built,
pinned `picotool` dependency (`%lu` versus host `size_t`). No project source
warning occurred.

## Final artifact evidence

| Packaged file | Bytes | SHA-256 |
|---|---:|---|
| `pico_w_dual_ble_hid_bridge_PRE_HARDWARE_TEST.uf2` | 939,520 | `FF0EE7D18E4A4F0E420EE7AE4BC2990A069714BBE272E3A277270BA28ED0690F` |
| `pico_w_clear_keyboard_pairing_MAINTENANCE.uf2` | 788,480 | `7C83902AC8CEE984B2B6E0415232559616EED425F2C6F843ED37E4CABA62D65C` |
| `pico_w_clear_mouse_pairing_MAINTENANCE.uf2` | 788,480 | `A32CE5B2CE34677EB433381DB319CBCDD8E5387DEF1F3614ED8D2B77610CAE65` |
| `pico_w_clear_all_pairings_MAINTENANCE.uf2` | 786,944 | `7AC4A193BCF73AB2BE4DCFC97AA8E5C4D9412CC181D1068A69F2233B93D6DC5D` |

The normal ELF reports 473,664 B `text`, 0 B `data`, and 46,072 B `bss`.
Keyboard/mouse maintenance ELFs each report 398,208/0/19,460 B; clear-all
reports 397,448/0/19,460 B.

## Coverage boundary and physical matrix

Host tests do not compile BTstack, TinyUSB, `ble_hid_bridge.c`, `main.c`, or the
maintenance executable into a simulated controller. Their pure transition and
data policies are tested, while full callback ordering is covered by source
review, strict ARM compilation, and fail-safe guards. Real controller timing,
flash interruption, and USB callbacks remain hardware gates rather than being
misrepresented as simulated proof.

| Hardware layer | Result |
|---|---|
| Pico W boot and LED | NOT TESTED |
| PC USB enumeration and descriptor capture | NOT TESTED |
| Real BLE keyboard | NOT TESTED |
| Real BLE mouse | NOT TESTED |
| Both BLE links simultaneously | NOT TESTED |
| Disconnect/reconnect and held-input release | NOT TESTED |
| Flash power-loss and maintenance recovery | NOT TESTED |
| USB configured/unconfigured/suspend current | NOT TESTED |
| End-to-end latency | NOT TESTED |
| Xbox Series X and game behavior | NOT TESTED |

Run these in order using `docs/first-hardware-test.md`. Any failed hardware
stage keeps the PRE_HARDWARE_TEST label and must be recorded rather than hidden.
