#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

usage() {
	echo "usage: scripts/download_dataset.sh {lj|twitter|wikitalk|pokec}" >&2
	exit 1
}

case "${1:-}" in
lj)
	URL=https://snap.stanford.edu/data/soc-LiveJournal1.txt.gz
	GZ_SIZE=259619239
	OUT=soc-LiveJournal1.txt
	;;
twitter)
	URL=https://snap.stanford.edu/data/twitter-2010.txt.gz
	GZ_SIZE=5501785223
	OUT=twitter-2010.txt
	;;
wikitalk)
	URL=https://snap.stanford.edu/data/wiki-Talk.txt.gz
	GZ_SIZE=16947922
	OUT=wiki-Talk.txt
	;;
pokec)
	URL=https://snap.stanford.edu/data/soc-pokec-relationships.txt.gz
	GZ_SIZE=132454730
	OUT=pokec.txt
	;;
*)
	usage
	;;
esac

mkdir -p data/raw
GZ="data/raw/$(basename "$URL")"

if [ ! -f "data/raw/$OUT" ]; then
	wget -c --tries=10 --waitretry=15 --timeout=60 -O "$GZ" "$URL"
	ACTUAL_SIZE=$(stat -f%z "$GZ" 2>/dev/null || stat -c%s "$GZ")
	if [ "$ACTUAL_SIZE" -ne "$GZ_SIZE" ]; then
		echo "size is $ACTUAL_SIZE bytes, expected $GZ_SIZE — corrupted file, delete and retry" >&2
		exit 1
	fi
	gunzip -t "$GZ"
	gunzip -c "$GZ" > "data/raw/$OUT"
	rm "$GZ"
fi

echo "done: data/raw/$OUT"
