#!/usr/bin/env bash
# Keep the matrices in sync with .github/workflows/{ci,embedded}.yml.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: bash .github/scripts/run-ci.sh [options] [groups...]

With no groups, run all checks for the current platform. Checks continue after
failures; missing prerequisites count as failures. No system packages are installed.

Groups:
  all          All Linux checks, or version + windows under Git Bash on Windows
  host         version, docs, tests, make, analysis, coverage, fuzz (Linux)
  version      Existing version consistency/bump gate
  docs         Compile integration examples and generate warning-free Doxygen docs
  tests        All six Linux Release CMake/unit-test configurations
  make         Native Makefile builds with 8- and 64-byte CAN frames
  analysis     Existing cppcheck and Infer gate
  coverage     GCC Debug unit tests and the thresholds from gcovr.cfg
  fuzz         Fuzzer formatting and 5,000 seeded ASan/UBSan libFuzzer runs
  diagnostics  GCC diagnostics on all ten embedded targets (72 cases each)
  embedded     Ten targets x GCC/Clang x CMake/Make x three feature profiles
  windows      Four Visual Studio 2022 x64 Release configurations (Git Bash only)

Options:
  --jobs N                 Build/test parallelism (default: 2)
  --build-dir DIR          Parent for fresh run directories (default: build-ci-local)
  --install-toolchains     Install/reuse pinned embedded toolchains via CI's installer
  -h, --help               Show this help

Environment:
  GOOGLETEST_SOURCE_DIR     Existing GoogleTest source checkout for offline builds
  VERSION_GATE_BASE_REF    Base commit/ref for the version gate (default: origin/master)
  EMBEDDED_TOOLCHAIN_ROOT  Existing toolchains (default: build-embedded/toolchains)
  CC, CXX                  Linux host compilers (default: gcc, g++)
  GCC, GXX, GCOV           Coverage tools (default: gcc, g++, gcov)
  CMAKE, CTEST, MAKE, GCOVR, DOXYGEN, CPPCHECK, INFER
                           Override the corresponding executable
  FUZZ_CLANG, CLANG_FORMAT Fuzzing tools (default: clang, clang-format)
  CLANG                    Upstream embedded Clang (default: clang-18, as in CI)

Examples:
  bash .github/scripts/run-ci.sh --install-toolchains
  bash .github/scripts/run-ci.sh host
  bash .github/scripts/run-ci.sh --jobs 4 tests coverage
  bash .github/scripts/run-ci.sh diagnostics embedded

Linux cannot run the Windows/MSVC jobs; run the windows group under Git Bash
with Visual Studio 2022 installed. Embedded CI toolchains require Linux x86_64.
EOF
}

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
readonly REPO_ROOT
readonly HOST_GROUPS=(version docs tests make analysis coverage fuzz)
readonly EMBEDDED_TARGETS=(cortex-m0 cortex-m4 cortex-m7 riscv32 riscv64 esp32 esp32s2 esp32s3 esp32c3 esp32c6)
jobs=2
buildRoot=$REPO_ROOT/build-ci-local
installToolchains=false
requestedGroups=()
while (( $# )); do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --jobs|--build-dir)
            if (( $# < 2 )) || [[ -z $2 || $2 == --* ]]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            case "$1" in
                --jobs) jobs=$2 ;;
                --build-dir) buildRoot=$2 ;;
            esac
            shift 2
            ;;
        --install-toolchains) installToolchains=true; shift ;;
        all|host|version|docs|tests|make|analysis|coverage|fuzz|diagnostics|embedded|windows)
            requestedGroups+=("$1"); shift ;;
        *) echo "Unknown option or group: $1 (see --help)" >&2; exit 2 ;;
    esac
done
if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
    echo "--jobs must be a positive integer" >&2
    exit 2
fi
if (( ${#requestedGroups[@]} == 0 )); then requestedGroups=(all); fi

platform=$(uname -s)
groups=()
for group in "${requestedGroups[@]}"; do
    case "$group" in
        all)
            case "$platform" in
                Linux) groups+=("${HOST_GROUPS[@]}" diagnostics embedded) ;;
                MINGW*|MSYS*) groups+=(version windows) ;;
                *) echo "CI reproduction supports Linux or Windows with Git Bash." >&2; exit 2 ;;
            esac
            ;;
        host) groups+=("${HOST_GROUPS[@]}") ;;
        *) groups+=("$group") ;;
    esac
done

selected() {
    local group
    for group in "${groups[@]}"; do
        if [[ $group == "$1" ]]; then return 0; fi
    done
    return 1
}

# Resolve caller-provided paths before changing to the repository directory.
mkdir -p -- "$buildRoot"
buildRoot=$(cd -- "$buildRoot" && pwd)
if [[ -n ${GOOGLETEST_SOURCE_DIR:-} ]]; then
    if [[ ! -f $GOOGLETEST_SOURCE_DIR/CMakeLists.txt ]]; then
        echo "GOOGLETEST_SOURCE_DIR must contain a GoogleTest source checkout." >&2
        exit 2
    fi
    GOOGLETEST_SOURCE_DIR=$(cd -- "$GOOGLETEST_SOURCE_DIR" && pwd)
fi
toolchainRoot=${EMBEDDED_TOOLCHAIN_ROOT:-$REPO_ROOT/build-embedded/toolchains}
if [[ $toolchainRoot != /* ]]; then toolchainRoot=$PWD/$toolchainRoot; fi
export EMBEDDED_TOOLCHAIN_ROOT=$toolchainRoot
export CLANG=${CLANG:-clang-18}
export CMAKE=${CMAKE:-cmake}
export CMAKE_BUILD_PARALLEL_LEVEL=$jobs
cmakeCommand=$CMAKE
ctestCommand=${CTEST:-ctest}
makeCommand=${MAKE:-make}
runDir=$(mktemp -d "$buildRoot/run.XXXXXX")
export EMBEDDED_BUILD_ROOT=$runDir/embedded
mkdir -p "$runDir/logs"
printf 'check\texit_status\tlog\n' > "$runDir/results.tsv"
cd -- "$REPO_ROOT"

printf 'Local CI artifacts: %s\n' "$runDir"
case "$platform" in
    Linux) echo 'Windows/MSVC jobs require a separate run under Git Bash on Windows.' ;;
    MINGW*|MSYS*) echo 'Linux host and embedded jobs require a separate Linux run.' ;;
esac
if selected diagnostics || selected embedded; then
    echo "Embedded toolchains: $toolchainRoot (use --install-toolchains to download pinned versions)."
fi

failedChecks=0
passedChecks=0
runCheck() {
    local name=$1
    shift
    local logFile=$runDir/logs/$name.log
    local exitStatus
    printf '\nRUN  %s (log: %s)\n' "$name" "$logFile"
    # Do not put this subshell/function in an if/|| condition: Bash would then
    # disable errexit inside it and a later success could hide an earlier failure.
    set +e
    (set -e; set -x; "$@") > "$logFile" 2>&1
    exitStatus=$?
    set -e
    printf '%s\t%s\t%s\n' "$name" "$exitStatus" "$logFile" >> "$runDir/results.tsv"
    if (( exitStatus == 0 )); then
        passedChecks=$((passedChecks + 1))
        printf 'PASS %s\n' "$name"
    else
        failedChecks=$((failedChecks + 1))
        printf 'FAIL %s (exit %s)\n' "$name" "$exitStatus" >&2
        tail -n 40 "$logFile" >&2
    fi
}

requireTools() {
    local tool
    local missing=0
    for tool in "$@"; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "Required executable not found: $tool" >&2
            missing=1
        fi
    done
    if (( missing )); then return 127; fi
}

requireLinux() {
    if [[ $platform != Linux ]]; then
        echo 'This check requires Linux, matching the CI runner.' >&2
        return 2
    fi
}

testBuild() {
    local buildDir=$1
    local buildType=$2
    shift 2
    local sourceDir
    local fetchOptions=()
    requireTools "$cmakeCommand" "$ctestCommand"
    if [[ -n ${GOOGLETEST_SOURCE_DIR:-} ]]; then
        fetchOptions+=("-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$GOOGLETEST_SOURCE_DIR")
    else
        # Reuse sources fetched by an earlier job, but keep build products separate.
        for sourceDir in "$runDir"/{tests-standard,coverage,windows-standard}/_deps/googletest-src; do
            if [[ -f $sourceDir/CMakeLists.txt ]]; then
                fetchOptions+=("-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$sourceDir")
                break
            fi
        done
    fi
    "$cmakeCommand" -S "$REPO_ROOT" -B "$buildDir" \
        -DCMAKE_BUILD_TYPE="$buildType" -Disotpc_ENABLE_TESTING=ON \
        "${fetchOptions[@]}" "$@"
    "$cmakeCommand" --build "$buildDir" --config "$buildType" --parallel "$jobs"
    "$ctestCommand" --test-dir "$buildDir" -C "$buildType" \
        --output-on-failure --timeout 120 --parallel "$jobs" --no-tests=error
}

linuxTests() {
    requireLinux
    requireTools "${CC:-gcc}" "${CXX:-g++}"
    local name=$1
    shift
    testBuild "$runDir/tests-$name" Release \
        "-DCMAKE_C_COMPILER=${CC:-gcc}" "-DCMAKE_CXX_COMPILER=${CXX:-g++}" "$@"
}

windowsTests() {
    case "$platform" in
        MINGW*|MSYS*) ;;
        *) echo 'Windows checks require Git Bash and Visual Studio 2022 on Windows.' >&2; return 2 ;;
    esac
    local name=$1
    shift
    testBuild "$runDir/windows-$name" Release -G 'Visual Studio 17 2022' -A x64 "$@"
}

documentation() {
    requireLinux
    requireTools "$cmakeCommand" "${CC:-gcc}" "$makeCommand" "${DOXYGEN:-doxygen}" dot
    if [[ ! -f submodules/doxygen-awesome-css/doxygen-awesome.css ]]; then
        echo 'Missing stylesheet submodule; run git submodule update --init --recursive.' >&2
        return 1
    fi
    "$cmakeCommand" -S . -B "$runDir/examples" -DCMAKE_BUILD_TYPE=Release \
        "-DCMAKE_C_COMPILER=${CC:-gcc}" -Disotpc_BUILD_EXAMPLES=ON
    "$cmakeCommand" --build "$runDir/examples" --target isotpc_examples --parallel "$jobs"
    "$makeCommand" docs "DOXYGEN=${DOXYGEN:-doxygen}"
}

nativeMake() {
    requireLinux
    requireTools "$makeCommand" "${CC:-gcc}"
    local binDir=$runDir/make-$1
    "$makeCommand" "MAX_CAN_FRAME_SIZE=$1" "BIN=$binDir" \
        "COMP=${CC:-gcc}" CMAKE_BUILD_BY_DEFAULT=OFF all
    [[ -d $binDir && -L $binDir/libisotp.so && -f $binDir/libisotp.so ]]
}

analysis() {
    requireLinux
    requireTools "$makeCommand" "$cmakeCommand" "${CPPCHECK:-cppcheck}" "${INFER:-infer}"
    "$makeCommand" static-analysis
}

coverage() {
    requireLinux
    requireTools "${GCC:-gcc}" "${GXX:-g++}" "${GCOV:-gcov}" "${GCOVR:-gcovr}"
    testBuild "$runDir/coverage" Debug \
        "-DCMAKE_C_COMPILER=${GCC:-gcc}" "-DCMAKE_CXX_COMPILER=${GXX:-g++}" \
        -Disotpc_STATIC_LIBRARY=ON -Disotpc_ENABLE_COVERAGE=ON
    mkdir -p "$runDir/coverage-report"
    # An explicit search path avoids collecting stale data from other local builds.
    # The config still supplies CI's filters and coverage thresholds unchanged.
    "${GCOVR:-gcovr}" --config gcovr.cfg --gcov-executable "${GCOV:-gcov}" \
        --object-directory "$runDir/coverage" \
        --html-details "$runDir/coverage-report/index.html" \
        --cobertura "$runDir/coverage-report/coverage.xml" "$runDir/coverage"
}

fuzz() {
    requireLinux
    requireTools "$cmakeCommand" "${FUZZ_CLANG:-clang}" "${CLANG_FORMAT:-clang-format}" ninja timeout
    "${CLANG_FORMAT:-clang-format}" --dry-run --Werror fuzz/isotp_receive_fuzzer.c
    "$cmakeCommand" -S . -B "$runDir/fuzz" -G Ninja \
        "-DCMAKE_C_COMPILER=${FUZZ_CLANG:-clang}" -DCMAKE_BUILD_TYPE=Debug -Disotpc_ENABLE_FUZZING=ON
    "$cmakeCommand" --build "$runDir/fuzz" --target isotp_fuzz_receive --parallel "$jobs"
    mkdir -p "$runDir/fuzz/corpus-run" "$runDir/fuzz/artifacts"
    cp fuzz/corpus/isotp_receive/* "$runDir/fuzz/corpus-run/"
    ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        timeout 300 "$runDir/fuzz/fuzz/isotp_fuzz_receive" "$runDir/fuzz/corpus-run" \
        "-artifact_prefix=$runDir/fuzz/artifacts/" -runs=5000 -seed=1
}

# Diagnostic flags mirror embedded.yml; the build helper owns its own CPU mapping.
embeddedTarget() {
    target=$1
    case "$target" in
        cortex-m0) family=arm; prefix=arm-none-eabi; flags=(-mcpu=cortex-m0 -mthumb -mfloat-abi=soft) ;;
        cortex-m4) family=arm; prefix=arm-none-eabi; flags=(-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard) ;;
        cortex-m7) family=arm; prefix=arm-none-eabi; flags=(-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard) ;;
        riscv32) family=riscv; prefix=riscv-none-elf; flags=(-march=rv32imac -mabi=ilp32) ;;
        riscv64) family=riscv; prefix=riscv-none-elf; flags=(-march=rv64imac -mabi=lp64) ;;
        esp32|esp32s2|esp32s3) family=esp-xtensa; prefix=xtensa-$target-elf; flags=(-mlongcalls) ;;
        esp32c3) family=esp-riscv; prefix=riscv32-esp-elf; flags=(-march=rv32imc_zicsr_zifencei -mabi=ilp32) ;;
        esp32c6) family=esp-riscv; prefix=riscv32-esp-elf; flags=(-march=rv32imac_zicsr_zifencei -mabi=ilp32) ;;
    esac
}

diagnostics() {
    requireLinux
    embeddedTarget "$1"
    requireTools "$toolchainRoot/$family/bin/$prefix-gcc"
    bash .github/scripts/check-gcc-diagnostics.sh "$toolchainRoot/$family/bin/$prefix-gcc" \
        "$runDir/diagnostics/$target" "${flags[@]}"
}

embeddedBuild() {
    requireLinux
    embeddedTarget "$1"
    requireTools "$toolchainRoot/$family/bin/$prefix-gcc" "$makeCommand"
    if [[ $3 == cmake ]]; then requireTools "$cmakeCommand" ninja; fi
    if [[ $2 == clang ]]; then
        if [[ $1 == esp32* ]]; then
            requireTools "$toolchainRoot/esp-clang/bin/clang"
        else
            requireTools "$CLANG"
        fi
    fi
    bash .github/scripts/build-embedded.sh "$1" "$2" all "$3"
}

if selected version; then runCheck version "$makeCommand" version-gate; fi
if selected docs; then runCheck docs documentation; fi
if selected tests; then
    runCheck tests-standard linuxTests standard
    runCheck tests-send-arg linuxTests send-arg -Disotpc_ENABLE_CAN_SEND_ARG=ON
    runCheck tests-send-arg-events linuxTests send-arg-events -Disotpc_ENABLE_CAN_SEND_ARG=ON -Disotpc_ENABLE_TRANSCEIVE_EVENTS=ON
    runCheck tests-can-fd linuxTests can-fd -Disotpc_MAX_CAN_FRAME_SIZE=64
    runCheck tests-no-formatted-errors linuxTests no-formatted-errors -Disotpc_NO_FORMATTED_ERRORS=ON
    runCheck tests-all-features linuxTests all-features -Disotpc_MAX_CAN_FRAME_SIZE=64 \
        -Disotpc_DEFAULT_TX_DL=32 -Disotpc_ENABLE_CAN_SEND_FLAGS=ON -Disotpc_ENABLE_CAN_FD_BRS=ON \
        -Disotpc_ENABLE_CAN_SEND_ARG=ON -Disotpc_ENABLE_TRANSCEIVE_EVENTS=ON -Disotpc_ENABLE_STREAMING=ON
fi
if selected make; then
    runCheck make-8 nativeMake 8
    runCheck make-64 nativeMake 64
fi
if selected analysis; then runCheck analysis analysis; fi
if selected coverage; then runCheck coverage coverage; fi
if selected fuzz; then runCheck fuzz fuzz; fi
if selected diagnostics || selected embedded; then
    if $installToolchains; then
        for family in arm riscv esp-xtensa esp-riscv; do
            runCheck "install-$family" bash .github/scripts/install-embedded-toolchain.sh "$family" "$toolchainRoot"
        done
        if selected embedded; then
            runCheck install-esp-clang bash .github/scripts/install-embedded-toolchain.sh esp-clang "$toolchainRoot"
        fi
    fi
    for target in "${EMBEDDED_TARGETS[@]}"; do
        if selected diagnostics; then runCheck "diagnostics-$target" diagnostics "$target"; fi
        if selected embedded; then
            for compiler in gcc clang; do
                for buildSystem in cmake make; do
                    runCheck "embedded-$target-$compiler-$buildSystem" embeddedBuild "$target" "$compiler" "$buildSystem"
                done
            done
        fi
    done
fi
if selected windows; then
    runCheck windows-standard windowsTests standard
    runCheck windows-send-arg windowsTests send-arg -Disotpc_ENABLE_CAN_SEND_ARG=ON
    runCheck windows-events windowsTests events -Disotpc_ENABLE_TRANSCEIVE_EVENTS=ON
    runCheck windows-send-arg-events windowsTests send-arg-events -Disotpc_ENABLE_CAN_SEND_ARG=ON -Disotpc_ENABLE_TRANSCEIVE_EVENTS=ON
fi

printf '\nLocal CI: %s passed, %s failed.\nResults: %s/results.tsv\n' "$passedChecks" "$failedChecks" "$runDir"
if selected docs; then printf 'Doxygen output (when successful): %s/html/index.html\n' "$REPO_ROOT"; fi
if selected coverage; then printf 'Coverage output (when generated): %s/coverage-report/index.html\n' "$runDir"; fi
if (( failedChecks )); then exit 1; fi
