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

[ $# -ge 1 ] || { echo "usage: bench_threads.sh <name=edges.csv> [<name=edges.csv> ...]" >&2; exit 1; }
[ -x "$PREPARE" ] && [ -x "$RANK" ] || { echo "binaries are not built in $BUILD_DIR" >&2; exit 1; }

mkdir -p data/bench
[ -f "$OUT_CSV" ] || echo "dataset,tool,threads,run,wall_s,iterations,peak_rss_kib" > "$OUT_CSV"

now() {
	python3 -c 'import time; print(f"{time.time():.3f}")'
}

elapsed() {
	python3 -c "print(f'{$2 - $1:.3f}')"
}

tmax=1
for t in $THREADS; do
	[ "$t" -gt "$tmax" ] && tmax=$t
done

for spec in "$@"; do
	name="${spec%%=*}"
	csv="${spec#*=}"
	for t in $THREADS; do
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
			rss=$(printf '%s\n' "$out" | awk -F'peak_rss_kib=' '/done:/{print $2}')
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
