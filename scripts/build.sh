#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

usage() {
	cat <<'EOF'
usage: scripts/build.sh [target ...]

Generate build.ninja with CMake, then build with Ninja.
An existing build directory is never deleted.

Environment:
  BUILD_DIR   CMake build directory (default: build/native)
  BUILD_TYPE  CMake build type (default: Release)
  JOBS        Parallel Ninja jobs (default: chosen by Ninja)
  CXX         C++ compiler (default on Linux: clang++-18)

Examples:
  scripts/build.sh
  scripts/build.sh lr-prepare lr-rank
  BUILD_TYPE=Debug scripts/build.sh lr-tests
EOF
}

case "${1:-}" in
-h|--help)
	usage
	exit 0
	;;
esac

BUILD_DIR="${BUILD_DIR:-build/native}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

for tool in cmake ninja; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "build: required command not found: $tool" >&2
		echo "build: on Ubuntu 22.04, run scripts/setup_ubuntu.sh first" >&2
		exit 1
	fi
done

if [ -n "${JOBS:-}" ]; then
	case "$JOBS" in
	''|*[!0-9]*|0)
		echo "build: JOBS must be a positive integer" >&2
		exit 1
		;;
	esac
fi

if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
	configured_generator=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")
	if [ "$configured_generator" != "Ninja" ]; then
		echo "build: $BUILD_DIR uses '$configured_generator', not Ninja" >&2
		echo "build: choose a new directory, for example BUILD_DIR=build/ninja scripts/build.sh" >&2
		exit 1
	fi
else
	if [ "$(uname -s)" = "Darwin" ]; then
		CXX="${CXX:-clang++}"
	else
		CXX="${CXX:-clang++-18}"
	fi
	if ! command -v "$CXX" >/dev/null 2>&1; then
		echo "build: required command not found: $CXX" >&2
		echo "build: on Ubuntu 22.04, run scripts/setup_ubuntu.sh first" >&2
		exit 1
	fi
fi

configure_args=(-S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE")
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
	configure_args+=(-DCMAKE_CXX_COMPILER="$CXX")
fi
cmake "${configure_args[@]}"

ninja_args=(-C "$BUILD_DIR")
if [ -n "${JOBS:-}" ]; then
	ninja_args+=(-j "$JOBS")
fi
if [ "$#" -gt 0 ]; then
	ninja_args+=("$@")
fi
ninja "${ninja_args[@]}"

echo "build: done ($BUILD_DIR)"
for binary in \
	"$BUILD_DIR/src/prepare/lr-prepare" \
	"$BUILD_DIR/src/rank/lr-rank"; do
	if [ -x "$binary" ]; then
		echo "  $binary"
	fi
done
