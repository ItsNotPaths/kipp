# examples

Each file reads kipp in one language and does nothing else. They show the
framing and the session. They are not meant to be vendored.

`kipp.c` and `kipp.h` in the parent directory are the reference
implementation: a server, a client, dump on accept, the state projection.
These are readers, 30 to 110 lines each.

| File | Shows |
| --- | --- |
| `read.sh` | the wire is plain text. socat, `while read`, and a `case` |
| `read.lua` | the parse rule alone. Reads lines on stdin |
| `read.py` | the whole session. Holds facts, so `drop` and `stale` do something |
| `read.odin` | the same session with typed parsing and an arena |
| `serve.c` | a publisher for the readers to talk to |
| `check.sh` | runs every reader against `serve.c` and checks the output |

## Run

```sh
make demo          # in the parent directory. Serves /tmp/kipp-demo.sock
./check.sh         # or run them all at once and check the output
```

Then, in another terminal:

```sh
./read.sh
./read.py
socat -u UNIX-CONNECT:/tmp/kipp-demo.sock - | lua read.lua
odin run read.odin -file
```

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

An example that shows only framing barely drifts. Version 1 framing does not
move.

## Adding a language

Write a reader that connects, reads to `sync`, prints what it understands, and
skips what it does not. Under 150 lines. If it needs more than that, it is
becoming an implementation and belongs in its own repo.
