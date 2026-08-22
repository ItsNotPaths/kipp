#!/bin/sh
# kipp from a shell. No library, no parser, no dependency but socat.
SOCK=${1:-/tmp/kipp-demo.sock}

socat -u UNIX-CONNECT:"$SOCK" - | while IFS='	' read -r kind rest; do
	case $kind in
	version) echo "connected to $rest" ;;
	sync)    echo "--- state complete ---" ;;
	tag)     echo "tag $rest" ;;
	net)     echo "net $rest" ;;
	*)       ;;   # skip a kind we do not know. This is the whole rule.
	esac
done
