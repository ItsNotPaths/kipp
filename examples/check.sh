#!/bin/sh
# Smoke test for the readers. Replays one fixed session to each one and checks
# that it printed what it should. A missing toolchain is a skip, not a failure.
set -u
cd "$(dirname "$0")"

SOCK=/tmp/kipp-check.$$.sock
SESSION=/tmp/kipp-check.$$.session
FAILS=0

cleanup() { kill "$REPLAY" 2>/dev/null; rm -f "$SOCK" "$SESSION" ./read-odin; }
trap cleanup EXIT

# What a broker would send after `connect` and `subscribe *`.
printf '%s\n' \
	'version	2	broker=replay' \
	'retain	tag/eDP-1/2	state=focused,occupied' \
	'retain	net/wifi	ssid=hotel guest	signal=62	state=up' \
	'retain	status/nm	state=offline' \
	'retain	mystery/1	stray	x=1' \
	'sync	*' \
	'publish	key_press	key=super+3' \
	'delete	tag/eDP-1/2' >"$SESSION"

socat UNIX-LISTEN:"$SOCK",fork SYSTEM:"cat $SESSION; sleep 1" 2>/dev/null &
REPLAY=$!
sleep 0.3

run() {
	name=$1 want=$2
	shift 2
	out=$(timeout 3 "$@" </dev/null 2>/dev/null)
	if printf '%s' "$out" | grep -qF -e "$want"; then
		echo "ok    $name"
	else
		echo "FAIL  $name (no match for '$want')"
		FAILS=$((FAILS + 1))
	fi
}

skip() { echo "skip  $1 ($2 not installed)"; }

run sh '--- * complete ---' ./read.sh "$SOCK"

if command -v python3 >/dev/null; then
	run python '4 values, 1 offline' ./read.py "$SOCK"
else
	skip python python3
fi

if command -v lua >/dev/null; then
	run lua 'tag eDP-1/2 is gone' sh -c \
		"printf 'connect\tread_lua\nsubscribe\t*\n' | socat - UNIX-CONNECT:'$SOCK' | lua read.lua"
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
