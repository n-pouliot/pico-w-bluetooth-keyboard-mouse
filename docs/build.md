# Reproducible build

The release target is the original Raspberry Pi Pico W (`pico_w`). The tested
build environment was Windows 11 with:

| Component | Tested version / revision |
|---|---|
| Upstream application baseline | `2c6a303d1f172e56b271283af978efdcc483a389` |
| Raspberry Pi Pico SDK | tag `2.2.0`, commit `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| BTstack SDK submodule | `501e6d2b86e6c92bfb9c390bcf55709938e25ac1` |
| TinyUSB SDK submodule | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` (0.18.0) |
| CYW43 driver submodule | `dd7568229f3bf7a37737b9e1ef250c26efe75b23` |
| Arm GNU Toolchain | `14.2.Rel1`, GCC 14.2.1 |
| CMake | 4.4.3 |
| Ninja | 1.13.2 |
| Python | 3.12.10 |
| PyCryptodome | 3.23.0 |
| LLVM host tools | 22.1.8 |

The Pico SDK and its submodules are not vendored into this repository. Pinning
the exact SDK commit and recursively initializing submodules is required.

## Windows command-line build

Install CMake, Ninja, Python, LLVM, and Arm GNU Toolchain. Clone the SDK:

```powershell
git clone --branch 2.2.0 --depth 1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git C:\pico-sdk-2.2.0
C:\Path\To\Python312\python.exe -m pip install pycryptodome==3.23.0
```

Check the SDK revision:

```powershell
git -C C:\pico-sdk-2.2.0 rev-parse HEAD
```

It must print:

```text
a1438dff1d38bd9c65dbd693f0e5db4b9ae91779
```

From the repository root, configure a new empty build directory. Adjust tool
paths if the installers used different locations:

```powershell
$env:CC = 'C:\Program Files\LLVM\bin\clang.exe'
$env:CXX = 'C:\Program Files\LLVM\bin\clang++.exe'
$env:RC = 'C:\Program Files\LLVM\bin\llvm-rc.exe'
$env:PATH = 'C:\Program Files\LLVM\bin;C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin;C:\Program Files\CMake\bin;C:\Path\To\Ninja;' + $env:PATH

cmake -S src -B build-pico-w-release -G Ninja `
  -DPICO_BOARD=pico_w `
  -DCMAKE_BUILD_TYPE=Release `
  -DPICO_SDK_PATH=C:\pico-sdk-2.2.0 `
  -DPICO_COMPILER=pico_arm_cortex_m0plus_gcc `
  -DPython3_EXECUTABLE=C:\Path\To\Python312\python.exe

cmake --build build-pico-w-release --parallel
```

Pin Python 3.12 explicitly and verify `import Crypto` succeeds. The tested
machine also had Python 3.14, but that interpreter lacked PyCryptodome; BTstack's
GATT generator then used a random database hash, making otherwise identical
builds differ. `PICO_COMPILER` keeps the firmware compiler on Arm GNU while the
`CC`/`CXX`/`RC` environment variables let nested host tools use LLVM.

Expected UF2 outputs:

```text
build-pico-w-release\xbox_pico.uf2
build-pico-w-release\xbox_pico_clear_keyboard.uf2
build-pico-w-release\xbox_pico_clear_mouse.uf2
build-pico-w-release\xbox_pico_clear_all.uf2
```

The three clear images are one-shot maintenance programs. They are not normal
bridge firmware.

## Host tests

Build the allocation-free parser, serializer, and mailbox tests with a native
compiler:

```powershell
cmake -S tests -B build-host-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host-tests --parallel
ctest --test-dir build-host-tests --output-on-failure
```

Non-MSVC builds use `-Wall -Wextra -Wpedantic -Werror`; MSVC uses
`/W4 /WX /permissive-`.

## Warning policy

Every project-owned C translation unit is compiled with
`-Wall -Wextra -Wpedantic -Werror`. The Pico SDK, BTstack, TinyUSB, and CYW43
sources are pinned dependencies compiled into the target, so project CMake does
not impose extra warning flags on their source files. The exact project sources
compile without warnings. A fresh SDK build may build its host-side `picotool`
dependency; warnings from that separately maintained utility are not firmware
warnings and must still be recorded in the test report.

## Release resource budget

The current integrated Release build reports:

| Image | `text` | `data` | `bss` | UF2 file |
|---|---:|---:|---:|---:|
| Normal bridge | 472,616 B | 0 B | 46,072 B | 937,472 B |
| Clear keyboard | 398,208 B | 0 B | 19,460 B | 788,480 B |
| Clear mouse | 398,208 B | 0 B | 19,460 B | 788,480 B |
| Clear all | 397,448 B | 0 B | 19,460 B | 786,944 B |

RP2040 provides 2 MiB external flash and 264 KiB SRAM. The normal image uses
about 22.5% of flash by ELF `text` and 45.0 KiB of static `bss`. The latter
includes the explicit 8 KiB Core-1 stack and its 128-byte canary. This leaves
substantial headroom, although runtime stack high-water marks remain a physical
test item.

The four packaged same-day UF2 hashes are recorded in
`release/SHA256SUMS.txt`. Two independent empty build directories produced
identical hashes. UF2 metadata embeds the date, so cross-date byte identity is
not claimed.

Major fixed limits:

- two HCI connections, two GATT clients, two HIDS clients;
- 4,096-byte BTstack shared Report Map staging store;
- 2,048-byte accepted Report Map per peripheral;
- 64-byte maximum BLE HID input payload;
- eight Report IDs and 256 compiled fields per peripheral;
- two TinyUSB HID instances with 8-byte endpoint buffers;
- no dynamic allocation in the report compiler, normalizer, serializer, or
  cross-core mailbox.

UF2 file size includes UF2 framing and is not the amount of flash occupied.
