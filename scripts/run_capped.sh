#!/usr/bin/env bash
set -euo pipefail

usage() {
	echo "usage: scripts/run_capped.sh <budget: 128m|64m|...> <command...>" >&2
	echo "  mac: command and paths are INSIDE the container (repo is mounted at /work)" >&2
	echo "  linux: plain command, capped via systemd-run" >&2
	exit 1
}

[ $# -ge 2 ] || usage
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
	IMAGE=ooc-leaderrank-build
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
else
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
		OOM=true
	fi
	report "$CODE" "see cgroup_memory_peak above" "$OOM"
fi
