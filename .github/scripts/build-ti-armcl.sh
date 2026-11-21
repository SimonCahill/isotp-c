#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: bash .github/scripts/build-ti-armcl.sh <armcl-root> <target> [armcl-option...]
Targets:
  tms570     Cortex-R4F, big-endian (TMS570 Hercules)
  rm4x       Cortex-R4F, little-endian (RM4x Hercules)
  cortex-m4  Cortex-M4F, little-endian (Tiva C)
Compiles isotp.c with warnings as errors, and checks the frame layout armcl
produces against the bytes ISO 15765-2 puts on the bus. Any further arguments,
such as --define=ISO_TP_MAX_CAN_FRAME_SIZE=64, are passed to armcl.
Environment: TI_ARMCL_BUILD_DIR (default: build-ti-armcl/<target>)
USAGE
}
if (( $# < 2 )); then
    usage >&2
    exit 2
fi
armcl_root=$1
target=$2
shift 2
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=${TI_ARMCL_BUILD_DIR:-$repo_root/build-ti-armcl/$target}

case "$target" in
    # armcl compiles big-endian unless told otherwise; it has no flag to ask for it.
    tms570)    cpu_flags=(-mv7R4 --code_state=32 --float_support=VFPv3D16) ;;
    rm4x)      cpu_flags=(-mv7R4 --code_state=32 --float_support=VFPv3D16 --little_endian) ;;
    cortex-m4) cpu_flags=(-mv7M4 --code_state=16 --float_support=FPv4SPD16 --little_endian) ;;
    *) usage >&2; exit 2 ;;
esac

flags=(
    "${cpu_flags[@]}" --abi=eabi --c99 --opt_level=2
    --emit_warnings_as_errors --display_error_number --diag_wrap=off
    --include_path="$repo_root" --include_path="$armcl_root/include"
    "$@"
)
mkdir -p "$out"
for source in isotp.c .github/ti/layout_probe.c; do
    echo "armcl ${flags[*]} $source"
    "$armcl_root/bin/armcl" "${flags[@]}" --obj_directory="$out" \
        --output_file="$out/$(basename "$source" .c).obj" --compile_only "$repo_root/$source"
done

# One frame of each type, as layout_probe.c builds them. The probe asserts that
# each message is ISO_TP_MAX_CAN_FRAME_SIZE bytes, so the frame size follows
# from the length of what armcl emitted.
prefixes=(07 003e 1234 1000 25 310a14)
actual=$(python3 "$repo_root/.github/scripts/elf-section.py" "$out/layout_probe.obj" isotp_layout_probe)
frame_digits=$(( ${#actual} / ${#prefixes[@]} ))
expected=
for prefix in "${prefixes[@]}"; do
    expected+=$prefix$(printf '%0*d' $(( frame_digits - ${#prefix} )) 0)
done
if [[ $actual != "$expected" ]]; then
    echo "::error::$target: armcl's frame layout does not match the bus"
    echo "expected: $expected"
    echo "actual:   $actual"
    exit 1
fi
echo "Frame layout matches the bus ($(( frame_digits / 2 ))-byte frames)."
