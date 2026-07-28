#!/usr/bin/env bash
set -euo pipefail

# Install the build toolchain and Python dependencies on a fresh Ubuntu 22.04 (x86-64) machine.
# Mirrors containers/Containerfile so a native build matches the container build.

cd "$(dirname "$0")/.."

usage() {
	cat <<'EOF'
usage: scripts/setup_ubuntu.sh

Install Clang 18, libc++, CMake, Ninja and the Python helper environment on
Ubuntu 22.04. This command uses apt and may ask for sudo.
EOF
}

case "${1:-}" in
-h|--help)
	usage
	exit 0
	;;
esac
if [ "$#" -ne 0 ]; then
	usage >&2
	exit 1
fi

if [ ! -r /etc/os-release ]; then
	echo "setup: /etc/os-release is missing; Ubuntu 22.04 is required" >&2
	exit 1
fi

# shellcheck source=/dev/null
. /etc/os-release
if [ "${ID:-}" != "ubuntu" ] || [ "${VERSION_ID:-}" != "22.04" ]; then
	echo "setup: Ubuntu 22.04 is required (found ${PRETTY_NAME:-unknown system})" >&2
	exit 1
fi

sudo_cmd=()
if [ "$(id -u)" -ne 0 ]; then
	if ! command -v sudo >/dev/null 2>&1; then
		echo "setup: sudo is required when running as a non-root user" >&2
		exit 1
	fi
	sudo_cmd=(sudo)
fi

export DEBIAN_FRONTEND=noninteractive

"${sudo_cmd[@]}" apt-get update
"${sudo_cmd[@]}" apt-get install -y ca-certificates gnupg wget

# LLVM 18 (clang + libc++): Ubuntu 22.04 ships an older clang without full C++23.
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key |
	"${sudo_cmd[@]}" gpg --batch --yes --dearmor -o /usr/share/keyrings/llvm.gpg
echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] https://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" |
	"${sudo_cmd[@]}" tee /etc/apt/sources.list.d/llvm.list >/dev/null

# Kitware CMake, same source as the container image.
wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc |
	"${sudo_cmd[@]}" gpg --batch --yes --dearmor -o /usr/share/keyrings/kitware.gpg
echo "deb [signed-by=/usr/share/keyrings/kitware.gpg] https://apt.kitware.com/ubuntu/ jammy main" |
	"${sudo_cmd[@]}" tee /etc/apt/sources.list.d/kitware.list >/dev/null

"${sudo_cmd[@]}" apt-get update
"${sudo_cmd[@]}" apt-get install -y \
	clang-18 libc++-18-dev libc++abi-18-dev lld-18 \
	cmake ninja-build git \
	python3 python3-venv python3-pip

# Python virtualenv for the independent reference (reference_rank.py) and benchmark plots.
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install numpy scipy pandas networkx matplotlib

echo
echo "toolchain ready. next:"
echo "  scripts/build.sh"
