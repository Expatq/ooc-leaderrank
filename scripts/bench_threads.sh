#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build/native}"
BUDGET="${BUDGET:-128M}"
THREADS="${THREADS:-1 2 4 8}"
REPEATS="${REPEATS:-3}"
OUT_CSV="${OUT_CSV:-data/bench/results.csv}"
PREPARE="$BUILD_DIR/src/prepare/lr-prepare"
RANK="$BUILD_DIR/src/rank/lr-rank"

usage() {
	cat <<'EOF'
usage: scripts/bench_threads.sh <name=edges.csv> [<name=edges.csv> ...]

Environment:
  BUILD_DIR  Build directory (default: build/native)
  BUDGET     Memory budget passed to both tools (default: 128M)
  THREADS    Space-separated thread counts (default: "1 2 4 8")
  REPEATS    Runs per stage and thread count (default: 3)
  OUT_CSV    Result file; new rows are appended (default: data/bench/results.csv)
EOF
}

case "${1:-}" in
-h|--help)
	usage
	exit 0
	;;
esac

[ $# -ge 1 ] || { usage >&2; exit 1; }
[ -x "$PREPARE" ] && [ -x "$RANK" ] || { echo "binaries are not built in $BUILD_DIR" >&2; exit 1; }

for tool in python3 awk cmp; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "bench: required command not found: $tool" >&2
		exit 1
	fi
done

case "$REPEATS" in
''|*[!0-9]*|0)
	echo "bench: REPEATS must be a positive integer" >&2
	exit 1
	;;
esac

read -r -a thread_values <<< "$THREADS"
if [ "${#thread_values[@]}" -eq 0 ]; then
	echo "bench: THREADS must contain at least one positive integer" >&2
	exit 1
fi

tmax=1
for t in "${thread_values[@]}"; do
	case "$t" in
	''|*[!0-9]*|0)
		echo "bench: THREADS must contain positive integers" >&2
		exit 1
		;;
	esac
	[ "$t" -gt "$tmax" ] && tmax=$t
done

for spec in "$@"; do
	case "$spec" in
	*=*) ;;
	*)
		echo "bench: expected name=edges.csv, got: $spec" >&2
		exit 1
		;;
	esac
	name="${spec%%=*}"
	csv="${spec#*=}"
	case "$name" in
	''|*[!A-Za-z0-9._-]*|.|..)
		echo "bench: dataset name may contain only letters, digits, '.', '_' and '-': $name" >&2
		exit 1
		;;
	esac
	if [ ! -f "$csv" ]; then
		echo "bench: input file not found: $csv" >&2
		exit 1
	fi
done

mkdir -p data/bench "$(dirname "$OUT_CSV")"
[ -f "$OUT_CSV" ] || echo "dataset,tool,threads,run,wall_s,iterations,peak_rss_kib" > "$OUT_CSV"

now() {
	python3 -c 'import time; print(f"{time.time():.3f}")'
}

elapsed() {
	python3 -c 'import sys; print(f"{float(sys.argv[2]) - float(sys.argv[1]):.3f}")' "$1" "$2"
}

for spec in "$@"; do
	name="${spec%%=*}"
	csv="${spec#*=}"
	for t in "${thread_values[@]}"; do
		wd="data/bench/wd_${name}_t${t}"
		for run in $(seq 1 "$REPEATS"); do
			t0=$(now)
			out=$("$PREPARE" "$csv" "$wd" --budget "$BUDGET" --threads "$t")
			t1=$(now)
			wall=$(elapsed "$t0" "$t1")
			rss=$(printf '%s\n' "$out" | awk -F'peak_rss_kib=' '/done:/{print $2}')
			echo "$name,prepare,$t,$run,$wall,,$rss" >> "$OUT_CSV"
			echo "bench: $name prepare T=$t run=$run wall=${wall}s rss=${rss}KiB"
		done
		for run in $(seq 1 "$REPEATS"); do
			ranks="data/bench/ranks_${name}_t${t}.csv"
			t0=$(now)
			out=$("$RANK" "$wd" --budget "$BUDGET" --out "$ranks" --threads "$t")
			t1=$(now)
			wall=$(elapsed "$t0" "$t1")
			iters=$(printf '%s\n' "$out" | awk -F'iterations=' '/done:/{print $2}' | awk '{print $1}')
			converged=$(printf '%s\n' "$out" | awk -F'converged=' '/done:/{print $2}' | awk '{print $1}')
			rss=$(printf '%s\n' "$out" | awk -F'peak_rss_kib=' '/done:/{print $2}')
			if [ "$converged" != "true" ]; then
				echo "bench: lr-rank did not converge for $name with T=$t (run $run)" >&2
				exit 1
			fi
			echo "$name,rank,$t,$run,$wall,$iters,$rss" >> "$OUT_CSV"
			echo "bench: $name rank T=$t run=$run wall=${wall}s iters=$iters rss=${rss}KiB"
		done
	done
	check="data/bench/ranks_${name}_check_t1.csv"
	"$RANK" "data/bench/wd_${name}_t${tmax}" --budget "$BUDGET" --out "$check" --threads 1 > /dev/null
	cmp "$check" "data/bench/ranks_${name}_t${tmax}.csv" || { echo "FAIL: $name rank T=1 vs T=$tmax differ on the same workdir" >&2; exit 1; }
	echo "bench: $name rank output is byte-identical for T=1 and T=$tmax on the same workdir"
done
echo "BENCH DONE -> $OUT_CSV"
