#!/bin/sh
# kipp from a shell. No library, no parser, no dependency but socat.
SOCK=${1:-$XDG_RUNTIME_DIR/kippsrv.sock}

{ printf 'connect\tread_sh\nsubscribe\t*\n'; cat; } |
socat - UNIX-CONNECT:"$SOCK" | while IFS='	' read -r verb topic payload; do
	case $verb in
	version) echo "connected, version $topic" ;;
	sync)    echo "--- $topic complete ---" ;;
	retain | publish)
		case ${topic%%/*} in
		tag) echo "tag ${topic#tag/} $payload" ;;
		net) echo "net ${topic#net/} $payload" ;;
		*)   ;;   # skip a kind we do not know. This is the whole rule.
		esac ;;
	esac
done
