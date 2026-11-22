#!/usr/bin/env python3
"""kipp from Python. Subscribes to everything, holds values, sends one request."""
import os
import socket
import sys

SOCK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "kippsrv.sock")


def parse(line):
    """-> (verb, topic, fields). None when the line has no topic."""
    words = [word for word in line.split("\t") if word]
    if len(words) < 2:
        return None
    fields = {}
    for word in words[2:]:
        key, equals, value = word.partition("=")
        if equals and key:              # not key=value: skip it
            fields[key] = value
    return words[0], words[1], fields


def lines(connection):
    held = b""
    while True:
        chunk = connection.recv(4096)
        if not chunk:
            return
        held += chunk
        while b"\n" in held:
            line, _, held = held.partition(b"\n")
            yield line.decode("utf-8", "replace")


def main():
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.connect(SOCK)
    connection.sendall(b"connect\tread_py\nsubscribe\t*\n")

    # A real reader holds values, so a retain with no payload has something
    # to forget, and status/* can mark a source's values as old.
    values = {}
    synced = False

    for line in lines(connection):
        message = parse(line)
        if message is None:
            continue                    # unparsable, skip
        verb, topic, fields = message

        if verb == "retain":
            if fields:
                values[topic] = fields
            else:
                values.pop(topic, None)  # no payload: gone. Forget it

        print(f"{verb:8} {topic:16} {fields}", flush=True)

        if verb == "sync" and not synced:
            synced = True
            offline = [topic for topic, fields in values.items()
                       if topic.startswith("status/") and fields.get("state") == "offline"]
            print(f"--- {len(values)} values, {len(offline)} offline, "
                  "requesting tag 4 ---", flush=True)
            connection.sendall(b"publish\ttag/request\ttag=4\n")


if __name__ == "__main__":
    main()
