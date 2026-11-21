#!/usr/bin/env bash
set -euo pipefail

# Installs TI's ARM compiler (armcl) for Linux x86_64 from TI's public download
# server. The installer is pinned by version and SHA-256. It is downloaded on
# each run rather than cached or redistributed: the compiler is under TI's own
# licence, and the download is 46 MB and installs in a few seconds.
version=20.2.7.LTS
url=https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-sDOoXkUcde/$version/ti_cgt_tms470_${version}_linux-x64_installer.bin
sha256=697bc381ed00c52d7be19d67f1cdf651a3068ec7c38404edd696ee7f801fb3e8

if (( $# != 1 )); then
    echo "Usage: bash $0 <install-root>" >&2
    echo "Installs armcl $version and prints the path of its root directory." >&2
    exit 2
fi
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo "TI's armcl installer requires Linux x86_64." >&2
    exit 2
fi

install_root=$1
toolchain=$install_root/ti-cgt-arm_$version
if [[ ! -x $toolchain/bin/armcl ]]; then
    mkdir -p "$install_root"
    staging=$(mktemp -d)
    trap 'rm -rf -- "$staging"' EXIT
    curl --fail --location --silent --show-error --retry 3 --connect-timeout 30 \
        "$url" --output "$staging/installer.bin"
    printf '%s  %s\n' "$sha256" "$staging/installer.bin" | sha256sum --check - >&2
    chmod +x "$staging/installer.bin"
    "$staging/installer.bin" --mode unattended --prefix "$install_root" >&2
fi
banner=$("$toolchain/bin/armcl" --help)
echo "${banner%%$'\n'*}" >&2
cd -- "$toolchain" && pwd
