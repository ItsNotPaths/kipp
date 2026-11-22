# kipp

Kind-first Inter-Process Protocol. A text line protocol for local IPC over a
unix socket, plus a C client in one file.

```
version	2	broker=kippsrv
connect	bar
subscribe	tag/*
retain	tag/eDP-1/2	state=focused,occupied
retain	tag/eDP-1/3	state=occupied
sync	tag/*
retain	tag/eDP-1/3	state=focused,occupied
publish	tag/request	tag=4
```

Tab separated, verb first, then a topic whose first segment is the kind.
Subscribe and you get every value true right now, then the changes as they
happen. Publish `tag/request` on the same connection to switch tags.

The rules are MQTT 3.1.1's: `retain`, `publish`, `subscribe`, `will`. The wire
is text you can read, not MQTT's binary.

The whole thing is one page: [SPEC.md](SPEC.md).

## The use case

A desktop split into separate programs. A window manager knows about windows. A
bar draws. Popups come and go. Each one is a different repo, and one of them is
replaceable without touching the others.

One broker, [kippsrv](../kippsrv), holds the socket and the current state, and
turns every foreign source into topics. The bar and the popups connect,
subscribe and read. They publish requests back on the same connection. When the
window manager changes, one adapter in kippsrv changes and no client notices.

kipp is the wire between them.

## Why lines

**One stream carries state and events.** Subscribe, read the current values,
then read changes in the order they happen. A late reader is never wrong, and
there is no second channel to order against the first.

**Any language joins.** `socat` is a working client, and `while read` is
enough. That is what makes rewriting a client a non-event.

```sh
{ printf 'connect\tsh\nsubscribe\t*\n'; cat; } |
	socat - UNIX-CONNECT:"$XDG_RUNTIME_DIR/kippsrv.sock"
```

**A new kind costs one line.** The first topic segment names the kind and a
reader skips one it does not know, so adding a kind changes no client.

**The protocol is the contract, not a library.** Each program implements the
format in whatever language it already uses. Two implementations that drift
still interoperate. An ABI does not.

## Why not MQTT

kipp has MQTT's meaning, and a bridge to a real MQTT broker is one kippsrv
adapter over `mosquitto_sub` and `mosquitto_pub`. MQTT's wire is binary, so a
shell script cannot speak it with `read` and `printf`. kipp keeps the rules
and makes every line one you can read and type.

## Why not D-Bus

D-Bus is the standard for local IPC and it is the right tool for name
ownership, activation and typed method calls. Its unit of extension is an
interface, so a new fact means an interface revision or a property bag that
defeats the typing. kipp trades the type system for the property that a new
kind is one more line.

## Files

| | |
| --- | --- |
| `SPEC.md` | the protocol |
| `kipp.c`, `kipp.h` | the C client. C99, libc only |
| `tests/` | tests. `make check`; `make examples` also runs the readers |
| `examples/` | readers in sh, Lua, Python and Odin |

## Build

Vendor `kipp.c` and `kipp.h` into your own tree. There is no library to link
and nothing to install.

```sh
make    # build and run the tests
```

## License

MIT. See [LICENSE](LICENSE).
