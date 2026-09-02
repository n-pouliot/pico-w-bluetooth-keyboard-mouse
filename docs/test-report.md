# Test report

## Current gate status

**PUBLIC BETA (`v0.1.0-beta.1`) with hardware testing in progress.**

All available software-only gates pass. Physical BOOTSEL flashing, the MX
Mechanical BLE-to-Pico-to-Windows keyboard path, Logitech M196 mouse input, and
basic simultaneous input passed on 2026-09-02. The passive-scan build failed to
enroll the M196; active scanning reached security, diagnostic code 2 isolated
that stage, and the mouse-specific HOGP Level 2 policy completed pairing.
Current, latency, suspend behavior, full USB descriptor capture, extended
simultaneous use, and reconnect stress remain **NOT TESTED**. The user also
confirmed keyboard and mouse input through the Pico on an Xbox Series X in The
Sims 4. The Xbox dashboard ignored the mouse as expected.

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
| Arm Release build | PASS | All 4 targets passed at the initial checkpoint; changed normal target passed again after M196 policy update |
| Empty-directory build A | PASS | Python 3.12.10 with PyCryptodome pinned |
| Empty-directory build B | PASS | Separate directory, same pinned inputs |
| Same-day reproducibility | PASS — 4/4 initial UF2 hashes identical | Initial checkpoint only; UF2 embeds build date and the M196 follow-up was not rebuilt twice |
| Picotool inspection | PASS | Current normal: RP2040, `pico_w`, SDK 2.2.0, Release, 2026-09-02 |
| Stack-usage inspection | PASS for software gate | Largest observed project-owned frame 152 B; explicit 8 KiB Core-1 stack plus 128 B canary |

The only warning in clean firmware-build output came from the separately built,
pinned `picotool` dependency (`%lu` versus host `size_t`). No project source
warning occurred.

## Final artifact evidence

| Packaged file | Bytes | SHA-256 |
|---|---:|---|
| `xbox-pico-v0.1.0-beta.1-pico-w.uf2` | 940,032 | `7FA5350C1624AB35790336BA7B0E118A837E3831F66C1D5CA23C2DB2306C3A78` |
| `pico_w_clear_keyboard_pairing_MAINTENANCE.uf2` | 788,480 | `7C83902AC8CEE984B2B6E0415232559616EED425F2C6F843ED37E4CABA62D65C` |
| `pico_w_clear_mouse_pairing_MAINTENANCE.uf2` | 788,480 | `A32CE5B2CE34677EB433381DB319CBCDD8E5387DEF1F3614ED8D2B77610CAE65` |
| `pico_w_clear_all_pairings_MAINTENANCE.uf2` | 786,944 | `7AC4A193BCF73AB2BE4DCFC97AA8E5C4D9412CC181D1068A69F2233B93D6DC5D` |

The active-scan, diagnostic, and M196 compatibility filenames were development
artifacts. Their hashes remain in Git history and the handoff, but only the
successful byte-identical beta image is distributed in the public release set.

The original normal ELF reported 473,664 B `text`, 0 B `data`, and 46,072 B
`bss`. The current normal/successful compatibility ELF reports
474,040/0/46,080 B and binary end `0x10072BBC`.
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
| Pico W boot and LED | PARTIAL PASS — BOOTSEL flash, reboot, and green LED observed |
| PC USB enumeration and descriptor capture | PARTIAL PASS — keyboard input works through USB; full descriptor capture pending |
| Real BLE keyboard | PARTIAL PASS — MX Mechanical paired and typed through Pico |
| Real BLE mouse | PARTIAL PASS — M196 paired and produced cursor input with the compatibility build |
| Both BLE links simultaneously | PARTIAL PASS — both worked and solid-ready LED was observed; 15-minute stress pending |
| Disconnect/reconnect and held-input release | PARTIAL PASS — both roles reconnected after moving Pico from PC to Xbox; repeated and held-input release tests pending |
| Flash power-loss and maintenance recovery | NOT TESTED |
| USB configured/unconfigured/suspend current | NOT TESTED |
| End-to-end latency | NOT TESTED |
| Xbox and game behavior | PARTIAL PASS — keyboard and mouse work in The Sims 4 on Xbox Series X; dashboard mouse rejection is expected; broader titles pending |

Run the remaining checks using `docs/first-hardware-test.md`. Any failed stage
must be recorded and may block promotion from beta to a stable release.
