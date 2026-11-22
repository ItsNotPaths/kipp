# examples

Each file reads kipp in one language and does nothing else. They show the
framing and the session. They are not meant to be vendored.

`kipp.c` and `kipp.h` in the parent directory are the C client. The broker is
kippsrv. These are readers, 20 to 100 lines each.

| File | Shows |
| --- | --- |
| `read.sh` | the wire is plain text. socat, `while read`, and a `case` |
| `read.lua` | the parse rule alone. Reads lines on stdin |
| `read.py` | the whole session. Holds values, so a `retain` with no payload and `status/*` do something |
| `read.odin` | the same session with typed parsing and an arena |
| `check.sh` | replays one fixed session to every reader and checks the output |

## Run

With kippsrv running, each reader connects to `$XDG_RUNTIME_DIR/kippsrv.sock`:

```sh
./read.sh
./read.py
printf 'connect\tread_lua\nsubscribe\t*\n' |
	socat - UNIX-CONNECT:"$XDG_RUNTIME_DIR/kippsrv.sock" | lua read.lua
odin run read.odin -file
```

`./check.sh`, or `make examples` in the parent directory, needs no broker.

## What these are not

**They are not a shared implementation.** A program that speaks kipp writes its
own, in whatever language it already uses. `SPEC.md` is the contract. Two
implementations that drift still interoperate, and that is the whole reason the
contract is a wire format rather than a library.

**They are not copies of production code.** A program that serves kipp has a
store, an event loop and a socket server. A reader here must never become a
copy of one. Code copied into a repo with no build and no tests around it is
the definition of drift, and a stale example is worse than none: a stranger
writes against it and finds out later.

An example that shows only framing barely drifts. Version 2 framing does not
move.

## Adding a language

Write a reader that connects, subscribes, reads to `sync`, prints what it
understands, and skips what it does not. Under 150 lines. If it needs more than that, it is
becoming an implementation and belongs in its own repo.
