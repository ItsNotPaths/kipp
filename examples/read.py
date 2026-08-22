#!/usr/bin/env python3
"""kipp from Python. Connects, reads the dump, sends one command."""
import socket
import sys

SOCK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/kipp-demo.sock"


def parse(line):
    """-> (kind, subject list, attribute dict). None when there is no kind."""
    kind, subj, attr = None, [], {}
    for f in line.split("\t"):
        if not f:
            continue
        if kind is None:
            if f.startswith("@"):      # reserved metadata
                continue
            kind = f
        elif "=" in f[1:]:
            k, _, v = f.partition("=")
            attr[k] = v
        else:
            subj.append(f)
    return (kind, subj, attr) if kind else None


def lines(sock):
    buf = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            return
        buf += chunk
        while b"\n" in buf:
            line, _, buf = buf.partition(b"\n")
            yield line.decode("utf-8", "replace")


def main():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)

    synced = False
    for line in lines(s):
        m = parse(line)
        if m is None:
            continue                      # unparsable, skip
        kind, subj, attr = m
        print(f"{kind:12} {subj} {attr}", flush=True)

        if kind == "sync" and not synced:
            synced = True
            print("--- state complete, sending TAG 4 ---", flush=True)
            s.sendall(b"TAG\t4\n")


if __name__ == "__main__":
    main()
