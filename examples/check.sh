#!/bin/sh
# Smoke test for the readers. Starts a publisher, runs each one, checks that
# it printed what it should. A missing toolchain is a skip, not a failure.
set -u
cd "$(dirname "$0")"

SOCK=/tmp/kipp-check.$$.sock
FAILS=0

cleanup() { kill "$SRV" 2>/dev/null; rm -f "$SOCK" ./kipp-check ./read-odin; }
trap cleanup EXIT

cc -std=c99 -O2 -Wall -D_POSIX_C_SOURCE=200809L -o ./kipp-check serve.c ../kipp.c || exit 1
./kipp-check "$SOCK" 2>/dev/null &
SRV=$!
sleep 0.3

run() {
	name=$1 want=$2
	shift 2
	out=$(timeout 2 "$@" 2>/dev/null)
	if printf '%s' "$out" | grep -q "$want"; then
		echo "ok    $name"
	else
		echo "FAIL  $name (no match for '$want')"
		FAILS=$((FAILS + 1))
	fi
}

skip() { echo "skip  $1 ($2 not installed)"; }

run sh 'state complete' ./read.sh "$SOCK"

if command -v python3 >/dev/null; then
	run python 'sending TAG 4' ./read.py "$SOCK"
else
	skip python python3
fi

if command -v lua >/dev/null; then
	out=$(timeout 2 socat -u UNIX-CONNECT:"$SOCK" - 2>/dev/null | timeout 2 lua read.lua)
	case $out in
	*"tag 2 on eDP-1"*) echo "ok    lua" ;;
	*) echo "FAIL  lua"; FAILS=$((FAILS + 1)) ;;
	esac
else
	skip lua lua
fi

if command -v odin >/dev/null; then
	odin build read.odin -file -out:./read-odin >/dev/null 2>&1 &&
		run odin 'key_press' ./read-odin "$SOCK" ||
		{ echo "FAIL  odin (build)"; FAILS=$((FAILS + 1)); }
else
	skip odin odin
fi

[ "$FAILS" -eq 0 ] && echo "examples ok" || echo "$FAILS failed"
exit "$FAILS"
