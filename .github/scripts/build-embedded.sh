#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: bash .github/scripts/build-embedded.sh <target> <gcc|clang> [classic|fd|small|all] [cmake|make|all]
Targets: cortex-m0 cortex-m4 cortex-m7 riscv32 riscv64 esp32 esp32s2 esp32s3 esp32c3 esp32c6
Default profile: all (Classical CAN, CAN FD/all features, small/no formatted errors)
Default build system: all (CMake/Ninja and the native Makefile)
Environment: EMBEDDED_TOOLCHAIN_ROOT, EMBEDDED_BUILD_ROOT, CLANG, CMAKE, MAKE
EOF
}
if [[ ${1:-} == --help ]]; then
    usage
    exit 0
fi
if (( $# < 2 || $# > 4 )); then
    usage >&2
    exit 2
fi
target=$1
compiler=$2
profile=${3:-all}
build_system=${4:-all}
# Keep the native Make CPU/ABI flags in sync with .github/cmake/embedded.cmake.
case "$target" in
    cortex-m0|cortex-m4|cortex-m7)
        family=arm; triple=arm-none-eabi; clang_triple=arm-none-eabi; machine=ARM; elf_class=ELF32
        cpu_flags="-mcpu=$target -mthumb"
        case "$target" in
            cortex-m0) cpu_flags+=" -mfloat-abi=soft" ;;
            cortex-m4) cpu_flags+=" -mfpu=fpv4-sp-d16 -mfloat-abi=hard" ;;
            cortex-m7) cpu_flags+=" -mfpu=fpv5-d16 -mfloat-abi=hard" ;;
        esac
        ;;
    riscv32|riscv64)
        family=riscv; triple=riscv-none-elf; clang_triple=$target-unknown-elf; machine=RISC-V
        elf_class=ELF${target#riscv}
        if [[ $target == riscv32 ]]; then
            cpu_flags="-march=rv32imac -mabi=ilp32"
        else
            cpu_flags="-march=rv64imac -mabi=lp64"
        fi
        ;;
    esp32|esp32s2|esp32s3)
        family=esp-xtensa; triple=xtensa-$target-elf; clang_triple=xtensa-esp-elf
        machine='Tensilica Xtensa Processor'; elf_class=ELF32
        if [[ $compiler == clang ]]; then cpu_flags="-mcpu=$target"; else cpu_flags=-mlongcalls; fi
        ;;
    esp32c3|esp32c6)
        family=esp-riscv; triple=riscv32-esp-elf; clang_triple=riscv32-esp-elf; machine=RISC-V; elf_class=ELF32
        if [[ $target == esp32c3 ]]; then
            cpu_flags="-march=rv32imc_zicsr_zifencei -mabi=ilp32"
        else
            cpu_flags="-march=rv32imac_zicsr_zifencei -mabi=ilp32"
        fi
        ;;
    *) usage >&2; exit 2 ;;
esac
case "$build_system" in
    cmake|make) build_systems=("$build_system") ;;
    all) build_systems=(cmake make) ;;
    *) usage >&2; exit 2 ;;
esac
case "$compiler" in gcc|clang) ;; *) usage >&2; exit 2 ;; esac
case "$profile" in
    classic|fd|small) profiles=("$profile") ;;
    all) profiles=(classic fd small) ;;
    *) usage >&2; exit 2 ;;
esac

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
toolchain_root=${EMBEDDED_TOOLCHAIN_ROOT:-$repo_root/build-embedded/toolchains}
build_root=${EMBEDDED_BUILD_ROOT:-$repo_root/build-embedded}
cmake=${CMAKE:-cmake}
make=${MAKE:-make}
# Resolve relative installation paths before passing them into CMake's probes.
toolchain_root=$(cd -- "$toolchain_root" && pwd)
mkdir -p "$build_root"
# Make runs from the repository root, regardless of the caller's directory.
build_root=$(cd -- "$build_root" && pwd)
bin_prefix=$toolchain_root/$family/bin/$triple
cc=$bin_prefix-gcc
if [[ $compiler == clang ]]; then
    if [[ $target == esp32* ]]; then
        cc=$toolchain_root/esp-clang/bin/clang
    else
        cc=${CLANG:-clang}
    fi
fi

# Run in a subshell so command tracing is confined to the per-profile log.
build_profile() (
    set -x
    echo "Building $target / $compiler / $build_system / $profile"
    uname -a
    "$cc" -v
    "$bin_prefix-gcc" -dumpmachine
    "$bin_prefix-gcc" -print-sysroot
    "$bin_prefix-ar" --version
    build_type=Release
    optimization_flags="-O2 -DNDEBUG"
    options=(
        -Disotpc_PAD_CAN_FRAMES=ON
        -Disotpc_MAX_CAN_FRAME_SIZE=8
        -Disotpc_DEFAULT_TX_DL=8
        -Disotpc_ENABLE_CAN_SEND_ARG=OFF
        -Disotpc_ENABLE_CAN_SEND_FLAGS=OFF
        -Disotpc_ENABLE_CAN_FD_BRS=OFF
        -Disotpc_ENABLE_TRANSCEIVE_EVENTS=OFF
        -Disotpc_ENABLE_STREAMING=OFF
        -Disotpc_NO_FORMATTED_ERRORS=OFF
    )
    make_options=(
        CMAKE_BUILD_BY_DEFAULT=OFF
        USE_STATIC_LIBRARY=ON
        ENABLE_STATIC_LIBRARY_PIC=OFF
        USE_INCLUDE_DIR=OFF
        ENABLE_FRAME_PADDING=ON
        CAN_FRAME_PAD_VALUE=0xAA
        MAX_CAN_FRAME_SIZE=8
        DEFAULT_TX_DL=8
        ENABLE_CAN_SEND_ARG=OFF
        ENABLE_CAN_SEND_FLAGS=OFF
        ENABLE_CAN_FD_BRS=OFF
        ENABLE_TRANSCEIVE_EVENTS=OFF
        ENABLE_TRANSMIT_COMPLETE_CALLBACK=ON
        ENABLE_RECEIVE_COMPLETE_CALLBACK=ON
        ENABLE_STREAMING=OFF
        NO_FORMATTED_ERRORS=OFF
    )
    case "$profile" in
        fd)
            build_type=Debug # Also compile assertions on every architecture.
            optimization_flags="-O0 -g"
            options+=(
                -Disotpc_MAX_CAN_FRAME_SIZE=64
                -Disotpc_DEFAULT_TX_DL=32
                -Disotpc_ENABLE_CAN_SEND_ARG=ON
                -Disotpc_ENABLE_CAN_SEND_FLAGS=ON
                -Disotpc_ENABLE_CAN_FD_BRS=ON
                -Disotpc_ENABLE_TRANSCEIVE_EVENTS=ON
                -Disotpc_ENABLE_STREAMING=ON
            )
            make_options+=(
                MAX_CAN_FRAME_SIZE=64
                DEFAULT_TX_DL=32
                ENABLE_CAN_SEND_ARG=ON
                ENABLE_CAN_SEND_FLAGS=ON
                ENABLE_CAN_FD_BRS=ON
                ENABLE_TRANSCEIVE_EVENTS=ON
                ENABLE_STREAMING=ON
            )
            ;;
        small)
            options+=(-Disotpc_PAD_CAN_FRAMES=OFF -Disotpc_NO_FORMATTED_ERRORS=ON)
            make_options+=(ENABLE_FRAME_PADDING=OFF NO_FORMATTED_ERRORS=ON)
            ;;
    esac
    if [[ $build_system == cmake ]]; then
        "$cmake" --version
        "$cmake" -S "$repo_root" -B "$build_dir" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$repo_root/.github/cmake/embedded.cmake" \
            -DISOTPC_EMBEDDED_TARGET="$target" \
            -DISOTPC_EMBEDDED_COMPILER="$compiler" \
            -DISOTPC_TOOLCHAIN_ROOT="$toolchain_root" \
            -DISOTPC_CLANG="${CLANG:-clang}" \
            -DCMAKE_BUILD_TYPE="$build_type" \
            "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG" "-DCMAKE_C_FLAGS_DEBUG=-O0 -g" \
            -DCMAKE_C_STANDARD=99 -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_C_EXTENSIONS=OFF \
            -Disotpc_STATIC_LIBRARY=ON -Disotpc_STATIC_LIBRARY_PIC=OFF \
            -Disotpc_ENABLE_TESTING=OFF -Disotpc_ENABLE_FUZZING=OFF \
            -Disotpc_BUILD_EXAMPLES=ON "${options[@]}"
        "$cmake" --build "$build_dir" --target isotp isotpc_examples --parallel --verbose
    else
        "$make" --version
        cflags="$cpu_flags -ffreestanding -ffunction-sections -fdata-sections"
        cflags+=" -std=c99 -Wall -Wextra -Wpedantic -Werror -Wno-unknown-pragmas $optimization_flags"
        if [[ $compiler == clang ]]; then
            target_sysroot=$("$bin_prefix-gcc" -print-sysroot)
            if [[ ! -f $target_sysroot/include/stdio.h ]]; then
                echo "Cannot locate the target libc headers using $bin_prefix-gcc -print-sysroot" >&2
                exit 1
            fi
            cflags+=" --target=$clang_triple --sysroot=\"$target_sysroot\" -isystem \"$target_sysroot/include\""
        fi
        "$make" -C "$repo_root" -f .github/make/embedded.mk \
            "COMP=\"$cc\"" "AR=\"$bin_prefix-ar\"" "BIN=$build_dir" \
            "CFLAGS=$cflags" CPPFLAGS= "${make_options[@]}" native-all embedded-examples
    fi

    # Fail if a misconfigured toolchain ever silently produces host objects.
    LC_ALL=C "$bin_prefix-readelf" -h "$build_dir/libisotp.a" > "$build_dir/elf-header.txt"
    if ! grep -Eq "Machine:[[:space:]]+$machine$" "$build_dir/elf-header.txt" ||
        ! grep -Eq "Class:[[:space:]]+$elf_class$" "$build_dir/elf-header.txt"; then
        cat "$build_dir/elf-header.txt" >&2
        echo "Unexpected object architecture for $target/$compiler/$build_system/$profile" >&2
        exit 1
    fi
    "$bin_prefix-size" "$build_dir/libisotp.a"
    echo "Built $target / $compiler / $build_system / $profile"
)

for build_system in "${build_systems[@]}"; do
    for profile in "${profiles[@]}"; do
        build_dir=$build_root/$target/$compiler/$build_system/$profile
        mkdir -p "$build_dir"
        # pipefail preserves compiler/configuration failures while tee retains
        # exact commands, tool versions and stderr for the CI artifact upload.
        build_profile 2>&1 | tee "$build_dir/build.log"
    done
done
