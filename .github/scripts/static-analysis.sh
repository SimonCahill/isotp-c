#!/usr/bin/env sh
set -eu

CPPCHECK="${CPPCHECK:-cppcheck}"

if ! command -v "$CPPCHECK" >/dev/null 2>&1; then
    echo "cppcheck is required for static analysis." >&2
    exit 127
fi

"$CPPCHECK" \
    --enable=warning,style,performance,portability \
    --error-exitcode=1 \
    --inline-suppr \
    --std=c99 \
    --suppress=missingIncludeSystem \
    isotp.c \
    isotp.h \
    isotp_config.h \
    isotp_defines.h \
    isotp_user.h
