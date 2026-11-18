#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: bash .github/scripts/build-ti-armcl.sh <armcl-root> <target> [build-dir]
Targets:
  tms570     Cortex-R4F, big-endian (TMS570 Hercules)
  rm4x       Cortex-R4F, little-endian (RM4x Hercules)
  cortex-m4  Cortex-M4F, little-endian (Tiva C)
Compiles isotp.c in the classic, fd and small profiles, and checks the frame
layout armcl produces against the bytes ISO 15765-2 puts on the bus.
USAGE
}
if (( $# < 2 || $# > 3 )); then
    usage >&2
    exit 2
fi
armcl_root=$1
target=$2
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=${3:-$repo_root/build-ti-armcl}/$target

case "$target" in
    # armcl compiles big-endian unless told otherwise; it has no flag to ask for it.
    tms570)    cpu_flags=(-mv7R4 --code_state=32 --float_support=VFPv3D16) ;;
    rm4x)      cpu_flags=(-mv7R4 --code_state=32 --float_support=VFPv3D16 --little_endian) ;;
    cortex-m4) cpu_flags=(-mv7M4 --code_state=16 --float_support=FPv4SPD16 --little_endian) ;;
    *) usage >&2; exit 2 ;;
esac

armcl=$armcl_root/bin/armcl
common_flags=(
    "${cpu_flags[@]}" --abi=eabi --c99 --opt_level=2
    --emit_warnings_as_errors --display_error_number --diag_wrap=off
    --include_path="$repo_root" --include_path="$armcl_root/include"
)

failed=0
for profile in classic fd small; do
    case "$profile" in
        classic) frame_size=8;  defines=(--define=ISO_TP_FRAME_PADDING) ;;
        fd)      frame_size=64; defines=(
                     --define=ISO_TP_MAX_CAN_FRAME_SIZE=64 --define=ISO_TP_DEFAULT_TX_DL=32
                     --define=ISO_TP_FRAME_PADDING
                     --define=ISO_TP_USER_SEND_CAN_ARG --define=ISO_TP_USER_SEND_CAN_FLAGS
                     --define=ISO_TP_CAN_FD_USE_BRS --define=ISO_TP_ENABLE_STREAMING
                     --define=ISO_TP_TRANSMIT_COMPLETE_CALLBACK
                     --define=ISO_TP_RECEIVE_COMPLETE_CALLBACK) ;;
        small)   frame_size=8;  defines=(--define=ISO_TP_NO_FORMATTED_ERRORS --define=NDEBUG) ;;
    esac
    out=$build_dir/$profile
    mkdir -p "$out"
    echo "::group::$target / $profile"
    for source in isotp.c .github/ti/layout_probe.c; do
        object=$out/$(basename "$source" .c).obj
        echo "armcl ${common_flags[*]} ${defines[*]} $source"
        if ! "$armcl" "${common_flags[@]}" "${defines[@]}" \
                --obj_directory="$out" --output_file="$object" --compile_only "$repo_root/$source"; then
            echo "::error::$target / $profile: armcl failed to compile $source"
            failed=1
        fi
    done
    if [[ -f $out/layout_probe.obj ]]; then
        expected=
        for prefix in 07 003e 1234 1000 25 310a14; do
            frame=$prefix$(printf '%0*d' $(( frame_size * 2 - ${#prefix} )) 0)
            expected+=$frame
        done
        actual=$(python3 "$repo_root/.github/scripts/elf-section.py" "$out/layout_probe.obj" isotp_layout_probe)
        if [[ $actual == "$expected" ]]; then
            echo "Frame layout matches the bus."
        else
            echo "::error::$target / $profile: armcl's frame layout does not match the bus"
            echo "expected: $expected"
            echo "actual:   $actual"
            failed=1
        fi
    fi
    echo "::endgroup::"
done
exit "$failed"
