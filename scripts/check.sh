#!/bin/sh
# scripts/check.sh <QtDir> — POSIX sh mirror of scripts/check.ps1, for Git Bash with MinGW
# only (SPEC-build-packaging.md, "Everything, one command"). Same five stages, same stage
# names, same "== [n/5] <name>" / "FAILED: <name>" messages and exit-code behaviour as
# check.ps1 — see that script's header comment for what each stage does and why.
set -u

if [ "$#" -lt 1 ]; then
    echo "usage: scripts/check.sh <QtDir>" >&2
    exit 1
fi

QT_DIR=${1%/}
QT_DIR=${QT_DIR%\\}
MINGW_DIR=/c/Qt/Tools/mingw1310_64/bin

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd) || exit 1
cd "$REPO_ROOT" || exit 1

# CMake, Ninja and the MinGW compiler all need to be on PATH; none of this is assumed to be
# set up already (unlike check.ps1's MSVC path, there is no vsdev.ps1 equivalent here).
export PATH="$MINGW_DIR:/c/Qt/Tools/CMake_64/bin:/c/Qt/Tools/Ninja:$PATH"
# Never inherited from env for any stage below: every CMake configure passes
# -DCMAKE_PREFIX_PATH explicitly (or omits it, for the cmake-core stage) instead.
unset CMAKE_PREFIX_PATH

TOTAL_STAGES=5
QMAKE_EXE="$QT_DIR/bin/qmake.exe"

print_stage() {
    echo "== [$1/$TOTAL_STAGES] $2"
}

# Prints the required "FAILED: <stage>" line (plus optional diagnostic detail) and stops the
# whole script with exit code 1; later stages never run.
fail_stage() {
    echo "FAILED: $1"
    if [ -n "${2:-}" ]; then
        echo "$2"
    fi
    exit 1
}

# run <stage-name> <command...> — runs a command from the repo root, stops the stage on a
# non-zero exit.
run() {
    name=$1
    shift
    "$@"
    st=$?
    if [ "$st" -ne 0 ]; then
        fail_stage "$name" "$* exited with code $st"
    fi
}

# run_in <stage-name> <dir> <command...> — same, but with <dir> as the working directory
# (qmake and its make tool must run inside their own build folder).
run_in() {
    name=$1
    dir=$2
    shift 2
    ( cd "$dir" && "$@" )
    st=$?
    if [ "$st" -ne 0 ]; then
        fail_stage "$name" "(cd $dir && $*) exited with code $st"
    fi
}

# Best-effort Windows -> POSIX path conversion, used only to compare -QtDir (given in Windows
# form, e.g. C:/Qt/6.11.1/mingw_64) against Git Bash's own PATH entries in stage 2: prefers
# cygpath (bundled with Git for Windows) and falls back to a manual conversion.
to_posix_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -u "$1"
        return
    fi
    p=$(printf '%s' "$1" | tr '\\' '/')
    drive=$(printf '%s' "$p" | cut -c1 | tr 'A-Z' 'a-z')
    rest=$(printf '%s' "$p" | cut -c3-)
    printf '/%s%s' "$drive" "$rest"
}

# ---- stage 1: cmake-full (BLD-01, BLD-09, BLD-08 script half) ----
print_stage 1 cmake-full
dir=build/check-cmake-full
rm -rf "$dir"
run cmake-full cmake -S . -B "$dir" -G Ninja -DCMAKE_PREFIX_PATH="$QT_DIR" -DCMAKE_BUILD_TYPE=Debug
run cmake-full cmake --build "$dir"
run cmake-full ctest --test-dir "$dir" --output-on-failure

header_version=$(grep '#define MC_VERSION_STRING' include/mc/version.h | sed -E 's/.*"([^"]+)".*/\1/')
cache_version=$(grep '^CMAKE_PROJECT_VERSION:STATIC=' "$dir/CMakeCache.txt" | cut -d= -f2)
if [ "$header_version" != "$cache_version" ]; then
    fail_stage cmake-full "CMAKE_PROJECT_VERSION ($cache_version) != MC_VERSION_STRING ($header_version)"
fi

# ---- stage 2: cmake-core (BLD-02) ----
print_stage 2 cmake-core
dir=build/check-cmake-core
rm -rf "$dir"
mkdir -p "$dir"
trace_file="$dir/configure-trace.log"

# Strip only PATH entries under the Qt kit itself; see check.ps1's stage 2 for why the
# CMake/Ninja/MinGW directories (a different subtree, C:\Qt\Tools\...) must stay on PATH.
posix_qt_dir=$(to_posix_path "$QT_DIR" | tr 'A-Z' 'a-z')
old_path=$PATH
new_path=""
old_ifs=$IFS
IFS=:
for entry in $PATH; do
    entry_lc=$(printf '%s' "$entry" | tr 'A-Z' 'a-z')
    case "$entry_lc" in
        "$posix_qt_dir"*) ;;
        *) new_path="$new_path:$entry" ;;
    esac
done
IFS=$old_ifs
PATH=${new_path#:}
export PATH

run cmake-core cmake -S . -B "$dir" -G Ninja -DMC_BUILD_DEVICE=OFF -DCMAKE_BUILD_TYPE=Debug --trace-expand "--trace-redirect=$trace_file"

PATH=$old_path
export PATH

# Anchored on the trace's own "<file>(<line>):  <command>(" prefix so this matches only an
# actual find_package(Qt6 ...) call, not the substring appearing inside another command's
# argument text (e.g. the MC_BUILD_DEVICE option()'s own help string in CMakeLists.txt).
if grep -Eq '\):[[:space:]]+find_package\(Qt6' "$trace_file"; then
    fail_stage cmake-core "$trace_file contains a find_package(Qt6 call"
fi

run cmake-core cmake --build "$dir"
run cmake-core ctest --test-dir "$dir" --output-on-failure

# ---- stage 3: qmake (BLD-03) ----
print_stage 3 qmake
dir=build/check-qmake
rm -rf "$dir"
mkdir -p "$dir"
run_in qmake "$dir" "$QMAKE_EXE" ../../mc_protocol.pro CONFIG+=debug
run_in qmake "$dir" mingw32-make
run_in qmake "$dir" mingw32-make check

# ---- stage 4: consumer-cmake (BLD-06) ----
print_stage 4 consumer-cmake
dir=build/check-consumer-cmake
rm -rf "$dir"
run consumer-cmake cmake -S tests/consumer_cmake -B "$dir" -G Ninja -DCMAKE_PREFIX_PATH="$QT_DIR" -DCMAKE_BUILD_TYPE=Debug
run consumer-cmake cmake --build "$dir"

ctest_dry_run=$(ctest --test-dir "$dir" -N 2>&1)
ctest_dry_run_exit=$?
if [ "$ctest_dry_run_exit" -ne 0 ]; then
    fail_stage consumer-cmake "ctest -N exited with code $ctest_dry_run_exit"
fi
if ! printf '%s\n' "$ctest_dry_run" | grep -q 'Total Tests: 0'; then
    fail_stage consumer-cmake "its build tree registered a library test target: $ctest_dry_run"
fi

consumer_exe="$dir/consumer.exe"
if [ ! -f "$consumer_exe" ]; then
    fail_stage consumer-cmake "consumer.exe not found at $consumer_exe"
fi
run consumer-cmake "$consumer_exe"

# ---- stage 5: consumer-qmake (BLD-07) ----
print_stage 5 consumer-qmake
dir=build/check-consumer-qmake
rm -rf "$dir"
mkdir -p "$dir"
run_in consumer-qmake "$dir" "$QMAKE_EXE" ../../tests/consumer_qmake/app.pro CONFIG+=debug
run_in consumer-qmake "$dir" mingw32-make

app_exe=$(find "$dir" -iname 'app.exe' | head -n 1)
if [ -z "$app_exe" ]; then
    fail_stage consumer-qmake "app.exe not found under $dir"
fi
run consumer-qmake "$app_exe"

echo "== check: all $TOTAL_STAGES stages passed =="
exit 0
