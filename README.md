# kipp

Kind-first Inter-Process Protocol. A text line protocol for local IPC over a
unix socket, plus a C implementation in one file.

See [SPEC.md](SPEC.md).

## The use case

A desktop split into separate programs. A window manager knows about windows. A
shell holds the desktop state and normalizes every foreign source. A bar draws.
Each one is a different repo, and one of them is replaceable without touching
the others.

The shell publishes on a socket. The bar and the popups connect and read. They
send commands back on the same connection. When the window manager changes, one
adapter file in the shell changes and no consumer notices.

kipp is the wire between them.

## Why lines

**One stream carries state and events.** A consumer connects, reads the full
current state, then reads changes in the order they happen. A late reader is
never wrong, and there is no second channel to order against the first.

**Any language joins.** `socat - UNIX-CONNECT:/run/user/1000/tildesh/shell` is
a working client. A shell script reads it with `while read`. This is what makes
a rewrite of one consumer a non-event.

**A new fact costs one line.** The first field names the kind, and a reader
skips a kind it does not know. Adding a fact changes no consumer.

**The protocol is the contract, not a library.** Each program implements the
format in the language it already uses. Two implementations that drift still
interoperate, which an ABI does not.

## Why not D-Bus

D-Bus is the standard for local IPC and it is the right tool for name
ownership, activation and typed method calls. Its unit of extension is an
interface, so a new fact means an interface revision or a property bag that
defeats the typing. kipp trades the type system for the property that a new
fact is one more line.

## Files

| | |
| --- | --- |
| `SPEC.md` | the protocol |
| `kipp.c`, `kipp.h` | the implementation. C99, libc only |
| `t_kipp.c` | tests |

## Build

```sh
make          # libkipp.a
make check    # tests
```

Or vendor `kipp.c` and `kipp.h` into your own tree.
