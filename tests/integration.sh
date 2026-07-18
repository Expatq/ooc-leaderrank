#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build/native}"
BUDGET="${BUDGET:-64M}"
PREPARE="$BUILD_DIR/src/prepare/lr-prepare"
RANK="$BUILD_DIR/src/rank/lr-rank"
TOLERANCE=1e-9

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

[ -x "$PREPARE" ] && [ -x "$RANK" ] || fail "binaries are not built in $BUILD_DIR"

compare_csv() {
	python3 - "$1" "$2" "$TOLERANCE" <<'EOF'
import sys

def load(path):
    with open(path) as file:
        lines = [line.strip() for line in file if line.strip()]
    assert lines[0] == "vertex,rank", f"{path}: missing header"
    rows = [line.split(",") for line in lines[1:]]
    vertices = [int(v) for v, _ in rows]
    assert vertices == sorted(vertices), f"{path}: ids are not ascending"
    return [(int(v), float(r)) for v, r in rows]

actual, expected = load(sys.argv[1]), load(sys.argv[2])
tolerance = float(sys.argv[3])
assert len(actual) == len(expected), f"{len(actual)} rows, expected {len(expected)}"
for (av, ar), (ev, er) in zip(actual, expected):
    assert av == ev, f"vertex {av}, expected {ev}"
    assert abs(ar - er) <= tolerance, f"vertex {av}: {ar} vs {er}"
EOF
}

value_cases="task_example cycle star dangling no_incoming selfloop multiedge
	single_edge crlf header_variants blank_lines hypernode_small"
for case_name in $value_cases; do
	workdir=$(mktemp -d)
	"$PREPARE" "tests/golden/$case_name/edges.csv" "$workdir/wd" --budget "$BUDGET" \
		>/dev/null || fail "lr-prepare failed on $case_name"
	"$RANK" "$workdir/wd" --budget "$BUDGET" --out "$workdir/ranks.csv" \
		>/dev/null || fail "lr-rank failed on $case_name"
	compare_csv "$workdir/ranks.csv" "tests/golden/$case_name/expected.csv" \
		|| fail "$case_name: mismatch with the reference"
	"$RANK" "$workdir/wd" --budget "$BUDGET" --out "$workdir/ranks2.csv" >/dev/null
	cmp -s "$workdir/ranks.csv" "$workdir/ranks2.csv" \
		|| fail "$case_name: second run is not byte-identical"
	rm -rf "$workdir"
	echo "ok: $case_name"
done

error_cases="all_selfloops empty_file id_overflow"
for case_name in $error_cases; do
	workdir=$(mktemp -d)
	set +e
	message=$("$PREPARE" "tests/golden/$case_name/edges.csv" "$workdir/wd" \
		--budget "$BUDGET" 2>&1 >/dev/null)
	code=$?
	set -e
	[ "$code" -eq 1 ] || fail "$case_name: expected exit 1, got $code"
	[ -n "$message" ] || fail "$case_name: empty error message"
	rm -rf "$workdir"
	echo "ok: $case_name (error: $message)"
done

echo "INTEGRATION PASSED"
