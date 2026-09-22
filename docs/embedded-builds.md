# Embedded builds

[Embedded builds](../.github/workflows/embedded.yml) runs on pull requests and
pushes to `master`, and can also be started with `workflow_dispatch`. Every
target below is compiled with **both GCC and Clang**, using **both CMake/Ninja
and the project's native Makefile**. Each target/compiler/build-system
combination has an independent job with `fail-fast: false`, so a failing CMake
build does not prevent the corresponding Makefile build from running.
Each target also has a separate GCC diagnostics job with built-in libc checks
enabled across optimization levels. A failure in either job type fails the
workflow.

| Script target | CPU / instruction set | ABI | GCC package | Clang |
| --- | --- | --- | --- | --- |
| `cortex-m0` | Arm Cortex-M0, Thumb | soft float | `arm` | upstream |
| `cortex-m4` | Arm Cortex-M4, Thumb, FPv4-SP-D16 | hard float | `arm` | upstream |
| `cortex-m7` | Arm Cortex-M7, Thumb, FPv5-D16 | hard float | `arm` | upstream |
| `riscv32` | RV32IMAC | ILP32 | `riscv` | upstream |
| `riscv64` | RV64IMAC | LP64 | `riscv` | upstream |
| `esp32` | Xtensa ESP32 | windowed | `esp-xtensa` | Espressif |
| `esp32s2` | Xtensa ESP32-S2 | windowed | `esp-xtensa` | Espressif |
| `esp32s3` | Xtensa ESP32-S3 | windowed | `esp-xtensa` | Espressif |
| `esp32c3` | RV32IMC + Zicsr/Zifencei | ILP32 | `esp-riscv` | Espressif |
| `esp32c6` | RV32IMAC + Zicsr/Zifencei | ILP32 | `esp-riscv` | Espressif |

The Arm entries exercise CPU families used by MCUs such as STM32 and NXP
parts. They do not select a particular board or provide a CAN driver.

## Run locally

The installer supports Linux x86_64 and requires Bash, curl, tar, gzip, xz,
and sha256sum. CMake builds additionally require CMake 3.16 or newer and Ninja;
native Makefile builds require GNU Make and do not invoke CMake.
For Arm and generic RISC-V Clang builds, install Clang too; CI uses Ubuntu
24.04's `clang-18` package.

From the repository root, for example:

```bash
sudo apt-get install cmake ninja-build make clang-18 curl xz-utils

# Cortex-M4 with both compilers and both build systems.
bash .github/scripts/install-embedded-toolchain.sh arm
bash .github/scripts/build-embedded.sh cortex-m4 gcc
CLANG=clang-18 bash .github/scripts/build-embedded.sh cortex-m4 clang

# Generic RISC-V; the same GCC package supports both widths.
bash .github/scripts/install-embedded-toolchain.sh riscv
bash .github/scripts/build-embedded.sh riscv32 gcc
CLANG=clang-18 bash .github/scripts/build-embedded.sh riscv64 clang

# Espressif's Xtensa and RISC-V targets.
bash .github/scripts/install-embedded-toolchain.sh esp-xtensa
bash .github/scripts/install-embedded-toolchain.sh esp-riscv
bash .github/scripts/install-embedded-toolchain.sh esp-clang
bash .github/scripts/build-embedded.sh esp32 gcc
bash .github/scripts/build-embedded.sh esp32 clang
bash .github/scripts/build-embedded.sh esp32c3 gcc
bash .github/scripts/build-embedded.sh esp32c3 clang
```

The build script accepts `classic`, `fd`, `small`, or `all` as an optional
third argument, and `cmake`, `make`, or `all` as an optional fourth argument.
Both default to `all`. Each build system also cross-compiles the four
integration examples with their own feature definitions.

To reproduce a single CI job or compare a failing GCC configuration:

```bash
# Exactly the two cortex-m4 / gcc CI jobs (all three profiles each).
bash .github/scripts/build-embedded.sh cortex-m4 gcc all cmake
bash .github/scripts/build-embedded.sh cortex-m4 gcc all make

# Narrow the comparison to the CAN FD / Debug profile.
bash .github/scripts/build-embedded.sh cortex-m4 gcc fd cmake
bash .github/scripts/build-embedded.sh cortex-m4 gcc fd make
```

| Profile | Build type | Library options |
| --- | --- | --- |
| `classic` | Release | 8-byte Classical CAN, padding, default diagnostics |
| `fd` | Debug | 64-byte CAN FD, TX_DL 32, CAN flags/BRS, CAN user argument, completion callbacks, streaming, assertions |
| `small` | Release | Classical CAN without padding or formatted diagnostics, assertions disabled by `NDEBUG` |

Toolchains are installed under `build-embedded/toolchains`, and results go to
`build-embedded/<target>/<compiler>/<build-system>/<profile>/`. Both are ignored
by Git, and the build systems use separate output directories.
`EMBEDDED_TOOLCHAIN_ROOT` changes the installation/search location;
`EMBEDDED_BUILD_ROOT` changes the output location. `CLANG` selects the upstream
Clang executable, `CMAKE` selects the CMake executable, and `MAKE` selects GNU
Make. Espressif builds always use the installed Espressif Clang when `clang`
is requested.

The installer verifies the archive's SHA-256 before extraction and reuses an
installation with the same checksum marker. To upgrade a pin, update its URL
and checksum together and use a fresh installation directory. CI caches each
package with a key derived from the installer script, OS, and host architecture.
Compiler versions, target triples and sysroots are recorded in each profile's
`build.log`, along with verbose build commands and compiler errors. The pins are:

- Arm GCC: [xPack 14.2.1-1.1](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/tag/v14.2.1-1.1).
- Generic RISC-V GCC: [xPack 14.2.0-3](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/tag/v14.2.0-3).
- Espressif Xtensa GCC: `esp-14.2.0_20241119`; Espressif Clang:
  `esp-19.1.2_20250312`. These URLs and checksums come from the
  [ESP-IDF v5.5.1 tool manifest](https://github.com/espressif/esp-idf/blob/v5.5.1/tools/tools.json).
- Espressif RISC-V GCC:
  [esp-14.2.0_20260121](https://github.com/espressif/crosstool-NG/releases/tag/esp-14.2.0_20260121),
  matching the reported `-Wstringop-overflow` failure. Its SHA-256 comes from
  the release's published checksums.

## What CI checks

Both build systems enforce C99 without GNU language extensions, use the same
CPU/ABI and feature flags, treat warnings as errors, and build non-PIC static
libraries and example objects. Release profiles use `-O2 -DNDEBUG`; the Debug
profile uses `-O0 -g` with assertions enabled. The native build invokes
`native-all` from the root Makefile, supplemented by compile-only example rules
in [embedded.mk](../.github/make/embedded.mk).

The CMake toolchain makes compiler probes
static libraries too, so configuration does not need startup code, system
calls, or a board linker script. Clang uses the target libc headers from the
matching GCC package, following its explicit target triple and CPU/ABI flags;
host libc headers are not substituted. Xtensa uses Espressif's Clang backend.

After each build, the script checks the library's ELF machine and 32/64-bit
class with the target `readelf` and reports object sizes. The workflow uploads
archives, ELF headers, and full build logs, plus CMake compilation databases
and configuration diagnostics where applicable, for seven days, even when a
build fails. Artifact names include the target, compiler and build system.
To investigate a GCC failure, compare the commands and diagnostics in the two
build systems' `build.log` files for the same target and profile.
These archives use different compile-time configurations; consumers
must use the corresponding definitions, particularly those that affect the
public structure layout or platform hook signatures.

### GCC diagnostics

The normal cross-builds use `-ffreestanding`, which disables GCC's built-in
libc handling and can hide warnings about calls such as `memcpy`. The separate
diagnostics jobs compile `isotp.c` directly with the same target compilers and
CPU/ABI flags, using `-fhosted` to enable built-in checks. They need no firmware
link step and still use the target libc headers.

Each of the ten targets checks all 72 combinations of:

- C99 and GNU17.
- `-O0`, `-Og`, `-O1`, `-O2`, `-O3`, and `-Os`.
- Classical CAN, CAN FD with optional features, and small/no formatted errors.
- Assertions enabled (`-UNDEBUG`) and disabled (`-DNDEBUG`).

Warnings are errors, including explicit string-operation overflow, array-bounds
and format diagnostics. The Classical CAN profile uses the library defaults
with no padding, matching the reported failure. In particular, a debug build
at `-O0` cannot stand in for `-Og`: compiler diagnostics can differ by
optimization level. The diagnostics script runs every combination and exits
with failure if any compile fails.

To run the ESP32-C3 diagnostics locally on Linux x86_64:

```bash
bash .github/scripts/install-embedded-toolchain.sh esp-riscv
bash .github/scripts/check-gcc-diagnostics.sh \
  "$PWD/build-embedded/toolchains/esp-riscv/bin/riscv32-esp-elf-gcc" \
  build-embedded/diagnostics/esp32c3 \
  -march=rv32imc_zicsr_zifencei -mabi=ilp32
```

If an older compiler is already installed in that directory, use a fresh
`EMBEDDED_TOOLCHAIN_ROOT` and pass its compiler path to the diagnostics script.
The same script can check a host GCC by passing its executable and omitting
the CPU/ABI flags.

Artifacts named `gcc-diagnostics-<target>` retain compiler versions, exact
commands and errors, a `results.tsv` summary, and preprocessed `.i` files for
failed compilations for seven days. These checks supplement the independent
CMake and native Makefile jobs; they do not replace either build system.

### Scope

These checks cover compilation and archiving of the portable library and
integration examples. They do not link or flash firmware, build ESP-IDF, run
on an emulator, or test CAN hardware. Host unit tests and fuzzing remain in the
main CI workflow. CAN FD build coverage does not imply that every listed MCU
has a CAN FD controller.

To add a target, extend the CPU/ABI mapping in
[the CMake toolchain](../.github/cmake/embedded.cmake), the matching native Make
CPU/ABI flags and ELF expectations in
[the build script](../.github/scripts/build-embedded.sh), and
both workflow matrices (including the diagnostics CPU/ABI flags). Add an
installer package only if a new compiler distribution is needed.
