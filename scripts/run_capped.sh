#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat <<'EOF'
usage: scripts/run_capped.sh <budget: 128m|64m|...> <command...>

  macOS: runs the command in the ooc-leaderrank-build Podman image.
         Repository paths are available under /work.
  Linux: runs the command in a transient user scope through systemd-run.
EOF
}

case "${1:-}" in
-h|--help)
	usage
	exit 0
	;;
esac

[ $# -ge 2 ] || { usage >&2; exit 1; }
BUDGET="$1"
shift

report() {
	local code=$1
	local peak=$2
	local oom=$3
	echo "run_capped: exit=$code oom_killed=$oom memory_peak=$peak budget=$BUDGET"
	return "$code"
}

if [ "$(uname)" = "Darwin" ]; then
	if ! command -v podman >/dev/null 2>&1; then
		echo "run_capped: podman is required on macOS" >&2
		exit 1
	fi
	IMAGE=ooc-leaderrank-build
	if ! podman image exists "$IMAGE"; then
		echo "run_capped: image '$IMAGE' is missing; build containers/Containerfile first" >&2
		exit 1
	fi
	NAME="lrcap_$$"
	set +e
	podman run --name "$NAME" --memory="$BUDGET" --memory-swap="$BUDGET" \
		-v "$PWD:/work" "$IMAGE" bash -c \
		'"$@"; code=$?; cat /sys/fs/cgroup/memory.peak > /tmp/peak 2>/dev/null
		 echo "cgroup_memory_peak=$(cat /tmp/peak 2>/dev/null || echo n/a)"
		 exit $code' _ "$@"
	CODE=$?
	set -e
	OOM=$(podman inspect --format '{{.State.OOMKilled}}' "$NAME" 2>/dev/null || echo n/a)
	podman rm "$NAME" >/dev/null 2>&1 || true
	report "$CODE" "see cgroup_memory_peak above" "$OOM"
elif [ "$(uname)" = "Linux" ]; then
	if ! command -v systemd-run >/dev/null 2>&1; then
		echo "run_capped: systemd-run is required on Linux" >&2
		exit 1
	fi
	set +e
	systemd-run --user --scope --quiet -p MemoryMax="$BUDGET" -p MemorySwapMax=0 \
		bash -c '"$@"; code=$?
			cg=$(cut -d: -f3 /proc/self/cgroup)
			echo "cgroup_memory_peak=$(cat /sys/fs/cgroup${cg}/memory.peak 2>/dev/null || echo n/a)"
			exit $code' _ "$@"
	CODE=$?
	set -e
	OOM=n/a
	if [ "$CODE" -eq 137 ]; then
		OOM=possible
	fi
	report "$CODE" "see cgroup_memory_peak above" "$OOM"
else
	echo "run_capped: unsupported operating system: $(uname)" >&2
	exit 1
fi
