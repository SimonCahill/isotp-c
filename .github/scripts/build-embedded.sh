#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: bash .github/scripts/build-embedded.sh <target> <gcc|clang> [classic|fd|small|all]
Targets: cortex-m0 cortex-m4 cortex-m7 riscv32 riscv64 esp32 esp32s2 esp32s3 esp32c3 esp32c6
Default profile: all (Classical CAN, CAN FD/all features, small/no formatted errors)
Environment: EMBEDDED_TOOLCHAIN_ROOT, EMBEDDED_BUILD_ROOT, CLANG, CMAKE
EOF
}
if [[ ${1:-} == --help ]]; then
    usage
    exit 0
fi
if (( $# < 2 || $# > 3 )); then
    usage >&2
    exit 2
fi
target=$1
compiler=$2
profile=${3:-all}
case "$target" in
    cortex-m0|cortex-m4|cortex-m7) family=arm; triple=arm-none-eabi; machine=ARM; elf_class=ELF32 ;;
    riscv32|riscv64)
        family=riscv; triple=riscv-none-elf; machine=RISC-V
        elf_class=ELF${target#riscv}
        ;;
    esp32|esp32s2|esp32s3) family=esp-xtensa; triple=xtensa-$target-elf; machine='Tensilica Xtensa Processor'; elf_class=ELF32 ;;
    esp32c3|esp32c6) family=esp-riscv; triple=riscv32-esp-elf; machine=RISC-V; elf_class=ELF32 ;;
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
command -v "$cmake" >/dev/null || { echo "CMake is required." >&2; exit 127; }
# Resolve relative installation paths before passing them into CMake's probes.
toolchain_root=$(cd -- "$toolchain_root" && pwd)
bin_prefix=$toolchain_root/$family/bin/$triple

for profile in "${profiles[@]}"; do
    build_dir=$build_root/$target/$compiler/$profile
    build_type=Release
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
    case "$profile" in
        fd)
            build_type=Debug # Also compile assertions on every architecture.
            options+=(
                -Disotpc_MAX_CAN_FRAME_SIZE=64
                -Disotpc_DEFAULT_TX_DL=32
                -Disotpc_ENABLE_CAN_SEND_ARG=ON
                -Disotpc_ENABLE_CAN_SEND_FLAGS=ON
                -Disotpc_ENABLE_CAN_FD_BRS=ON
                -Disotpc_ENABLE_TRANSCEIVE_EVENTS=ON
                -Disotpc_ENABLE_STREAMING=ON
            )
            ;;
        small) options+=(-Disotpc_PAD_CAN_FRAMES=OFF -Disotpc_NO_FORMATTED_ERRORS=ON) ;;
    esac
    "$cmake" -S "$repo_root" -B "$build_dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$repo_root/.github/cmake/embedded.cmake" \
        -DISOTPC_EMBEDDED_TARGET="$target" \
        -DISOTPC_EMBEDDED_COMPILER="$compiler" \
        -DISOTPC_TOOLCHAIN_ROOT="$toolchain_root" \
        -DISOTPC_CLANG="${CLANG:-clang}" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCMAKE_C_STANDARD=99 -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_C_EXTENSIONS=OFF \
        -Disotpc_STATIC_LIBRARY=ON -Disotpc_STATIC_LIBRARY_PIC=OFF \
        -Disotpc_ENABLE_TESTING=OFF -Disotpc_ENABLE_FUZZING=OFF \
        -Disotpc_BUILD_EXAMPLES=ON "${options[@]}"
    "$cmake" --build "$build_dir" --target isotp isotpc_examples --parallel

    # Fail if a misconfigured toolchain ever silently produces host objects.
    LC_ALL=C "$bin_prefix-readelf" -h "$build_dir/libisotp.a" > "$build_dir/elf-header.txt"
    if ! grep -Eq "Machine:[[:space:]]+$machine$" "$build_dir/elf-header.txt" ||
        ! grep -Eq "Class:[[:space:]]+$elf_class$" "$build_dir/elf-header.txt"; then
        cat "$build_dir/elf-header.txt" >&2
        echo "Unexpected object architecture for $target/$compiler/$profile" >&2
        exit 1
    fi
    "$bin_prefix-size" "$build_dir/libisotp.a"
    echo "Built $target / $compiler / $profile"
done
