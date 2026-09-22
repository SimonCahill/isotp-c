#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: bash $0 <arm|riscv|esp-xtensa|esp-riscv|esp-clang> [install-root]"
    echo "Installs a pinned, SHA-256-verified toolchain for Linux x86_64."
}

if [[ ${1:-} == --help ]]; then
    usage
    exit 0
fi
if (( $# < 1 || $# > 2 )); then
    usage >&2
    exit 2
fi
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo "The CI toolchain installer requires Linux x86_64." >&2
    exit 2
fi

# xPack checksums come from each release's <archive>.sha asset. Xtensa GCC and
# Espressif Clang match esp-idf v5.5.1 tools/tools.json. RISC-V GCC uses the
# esp-14.2.0_20260121 release and its published SHA-256 to cover the reported
# stringop-overflow diagnostic. Keep pins and hashes together; the workflow
# cache key includes this entire script.
case "$1" in
    arm)
        url=https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v14.2.1-1.1/xpack-arm-none-eabi-gcc-14.2.1-1.1-linux-x64.tar.gz
        sha256=ed8c7d207a85d00da22b90cf80ab3b0b2c7600509afadf6b7149644e9d4790a6
        ;;
    riscv)
        url=https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v14.2.0-3/xpack-riscv-none-elf-gcc-14.2.0-3-linux-x64.tar.gz
        sha256=f574415b63f12b09bdd3475223ab492a465d23810646c90c13a4c3b676c83503
        ;;
    esp-xtensa)
        url=https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20241119/xtensa-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.xz
        sha256=e3e6dcf3d275c3c9ab0e4c8a9d93fd10e7efc035d435460576c9d95b4140c676
        ;;
    esp-riscv)
        url=https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20260121/riscv32-esp-elf-14.2.0_20260121-x86_64-linux-gnu.tar.xz
        sha256=b3fce4b04dd15a9f0ed1c209b5a2f389c04eff92ed85e4cc0ebc275b7d95e95a
        ;;
    esp-clang)
        url=https://github.com/espressif/llvm-project/releases/download/esp-19.1.2_20250312/clang-esp-19.1.2_20250312-x86_64-linux-gnu.tar.xz
        sha256=8546cd8ac0596835fbe3970d7c8ed6a842713ab948b759525756867f3bc0a5ef
        ;;
    *) usage >&2; exit 2 ;;
esac

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
install_root=${2:-${EMBEDDED_TOOLCHAIN_ROOT:-$repo_root/build-embedded/toolchains}}
destination=$install_root/$1
if [[ -f $destination/.archive-sha256 ]] && [[ $(cat "$destination/.archive-sha256") == "$sha256" ]]; then
    echo "Using cached toolchain: $destination"
    exit 0
fi
if [[ -e $destination ]]; then
    echo "Unrecognised toolchain at $destination; choose a fresh install-root." >&2
    exit 1
fi

for tool in curl sha256sum tar; do
    command -v "$tool" >/dev/null || { echo "Required tool not found: $tool" >&2; exit 127; }
done

mkdir -p "$install_root"
staging=$(mktemp -d "$install_root/.install-$1.XXXXXX")
trap 'rm -rf -- "$staging"' EXIT
curl --fail --location --silent --show-error --retry 3 --connect-timeout 30 \
    "$url" --output "$staging/archive"
printf '%s  %s\n' "$sha256" "$staging/archive" | sha256sum --check -
mkdir "$staging/toolchain"
tar --extract --file "$staging/archive" --directory "$staging/toolchain" --strip-components=1
printf '%s\n' "$sha256" > "$staging/toolchain/.archive-sha256"
mv -- "$staging/toolchain" "$destination"
echo "Installed toolchain: $destination"
