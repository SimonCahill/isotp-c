#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: bash $0 <gcc> <output-dir> [target flags...]"
    echo "Checks C99/GNU17, six optimization levels, three feature profiles and both assertion modes."
}
if [[ ${1:-} == --help ]]; then
    usage
    exit 0
fi
if (( $# < 2 )); then
    usage >&2
    exit 2
fi
compiler=$1
output=$2
shift 2
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$output"
output=$(cd -- "$output" && pwd)

"$compiler" -v > "$output/toolchain.log" 2>&1
"$compiler" -dumpmachine >> "$output/toolchain.log"
"$compiler" -print-sysroot >> "$output/toolchain.log"
cat "$output/toolchain.log"
printf 'configuration\texit_status\n' > "$output/results.tsv"
result=0

for standard in c99 gnu17; do
    for profile in classic fd small; do
        definitions=(-DISO_TP_MAX_CAN_FRAME_SIZE=8 -DISO_TP_DEFAULT_TX_DL=8)
        case "$profile" in
            fd)
                definitions=(
                    -DISO_TP_MAX_CAN_FRAME_SIZE=64 -DISO_TP_DEFAULT_TX_DL=32
                    -DISO_TP_FRAME_PADDING -DISO_TP_FRAME_PADDING_VALUE=0xAA
                    -DISO_TP_USER_SEND_CAN_ARG -DISO_TP_USER_SEND_CAN_FLAGS
                    -DISO_TP_CAN_FD_USE_BRS -DISO_TP_TRANSMIT_COMPLETE_CALLBACK
                    -DISO_TP_RECEIVE_COMPLETE_CALLBACK -DISO_TP_ENABLE_STREAMING
                )
                ;;
            small) definitions+=(-DISO_TP_NO_FORMATTED_ERRORS) ;;
        esac
        for optimization in O0 Og O1 O2 O3 Os; do
            for assertions in enabled disabled; do
                assertion_flag=-UNDEBUG
                if [[ $assertions == disabled ]]; then assertion_flag=-DNDEBUG; fi
                name=$standard-$profile-$optimization-assertions-$assertions
                # Freestanding compilation disables built-in libc diagnostics.
                # These compile-only checks intentionally enable them, without
                # needing firmware startup code, a linker script or host libc.
                flags=(
                    "$@" -I"$repo_root" -fhosted "-std=$standard" "-$optimization"
                    "$assertion_flag" "${definitions[@]}"
                    -Wall -Wextra -Wpedantic -Werror -Wno-unknown-pragmas
                    -Wstringop-overflow=2 -Warray-bounds=2 -Wformat=2
                    -Wformat-overflow=2 -Wformat-truncation=2
                )
                command=("$compiler" "${flags[@]}" -c "$repo_root/isotp.c" -o "$output/$name.o")
                printf '%q ' "${command[@]}" > "$output/$name.log"
                printf '\n' >> "$output/$name.log"
                if "${command[@]}" >> "$output/$name.log" 2>&1; then
                    status=0
                    echo "PASS $name"
                else
                    status=$?
                    result=1
                    echo "FAIL $name (exit $status)" >&2
                    cat "$output/$name.log" >&2
                    # Retain the failing translation unit for local replay.
                    "$compiler" "${flags[@]}" -E "$repo_root/isotp.c" -o "$output/$name.i" \
                        >> "$output/$name.log" 2>&1 || true
                fi
                printf '%s\t%s\n' "$name" "$status" >> "$output/results.tsv"
            done
        done
    done
done

# Report all configurations before failing the gate; one warning must never
# be hidden by successful builds later in the loop.
exit "$result"
