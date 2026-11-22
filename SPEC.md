# kipp

Kind-first Inter-Process Protocol. Version 2.

A text line protocol for local IPC over a unix socket. One broker holds the
socket. Clients connect to it, publish to topics and subscribe to topics. The
rules are MQTT 3.1.1's. The wire is text, and MQTT's binary wire is never used.

## Framing

One message for each line. Fields are tab separated. The line ends with `\n`.

```
verb	topic	key=value	key=value
```

- Field 1 is the **verb**.
- Field 2 is the **topic**.
- Every field after the topic is **`key=value`** and unordered. Together they
  are the **payload**. A payload may be empty.

A **key** is a word of lowercase letters, digits and underscores.

A **value** holds any byte except a tab, a newline and a control character.
Only the first `=` in a field separates the key from the value, so a value may
hold more of them: `name=home=wifi` is one field.

A field after the topic with no `=` in it is not valid. A writer must refuse
it, and a reader skips it.

A binary payload goes in a file. The line carries the path.

## Topics

A topic is segments joined by `/`: `tag/eDP-1/2`, `net/c8119aa9`, `notif/42`.

**The first segment is the kind.** A kind is a word of lowercase letters,
digits and underscores, and it is never a number. The kind says what the
message is about, so a reader can skip a kind it does not know.

The other segments hold any byte except a tab, a newline, a control character,
`*`, `?` and `[`. Choose a segment that cannot change: a UUID, a MAC, a bus
name, a device path. A name a person typed belongs in the payload.

## Verbs

| Verb | Sent by | Means |
| --- | --- | --- |
| `version` | broker | the greeting, first line of a session |
| `connect` | client | names the client. First line a client sends |
| `will` | client | a message the broker sends for the client when its connection ends |
| `subscribe` | client | send me what matches this pattern |
| `unsubscribe` | client | stop sending what matches this pattern |
| `retain` | both | a value. The broker keeps it and forwards it |
| `publish` | both | an event. The broker forwards it and keeps nothing |
| `sync` | broker | the retained lines for one `subscribe` are all sent |
| `error` | broker | a line from the client failed |

A reader skips a line whose verb it does not know.

## Session

A client connects to the socket. The broker sends one `version` line:

```
version	2	broker=kippsrv
```

The client sends `connect` with its name. The name follows the rule for a kind.
If a second client connects with the same name, the broker closes the first
one. The broker answers any other line sent before `connect` with
`error noconnect`.

After `connect`, the client sends any of `will`, `subscribe`, `unsubscribe`,
`retain` and `publish`, in any order and at any time. The broker sends forwarded
lines, `sync` and `error` on the same connection, in the order they happen.

```
version	2	broker=kippsrv
connect	bar
will	retain	status/bar	state=offline
retain	status/bar	state=online
subscribe	battery/*
retain	battery/BAT0	level=80	state=charging
sync	battery/*
retain	battery/BAT0	level=79	state=charging
publish	tag/request	tag=4
```

## Retain and publish

**`retain`** is a value. The broker stores the line under its topic, replacing
the one stored before, and forwards it to every client whose pattern matches.
It forwards it as `retain`, so a reader knows that it is a value.

**`retain` with no payload** deletes the value. The broker forgets the topic and
forwards the line. A reader forgets the topic too.

```
retain	tray/:1.456	state=registered
retain	tray/:1.456
```

**`publish`** is an event. The broker forwards it to every client whose pattern
matches and stores nothing. A client that is not connected never sees it.

```
publish	key_press	key=super+3
publish	screenshot
```

A client also gets its own lines back when one of its patterns matches them.

A request is a `publish` to a topic that its handler subscribes to. It gets no
reply when it succeeds. The `retain` lines that follow show the result. If no
client subscribes, the broker drops it, as MQTT does.

## Subscribe

```
subscribe	battery/*
```

The pattern is matched against the topic with `fnmatch(3)`, without
`FNM_PATHNAME`. So `*` matches across `/`: `battery/*` is everything under
battery, and `*` is everything.

This is one of kipp's two changes to MQTT's rules. MQTT's `+` and `#` are not used.

The broker answers a `subscribe` with every retained line that the pattern
matches, then one `sync` line that repeats the pattern:

```
sync	battery/*
```

After `sync`, new lines that match arrive as they happen. A client that sends
several subscribes knows from the pattern which set has ended.

Subscribing to a pattern the client already holds sends the retained lines and
the `sync` again. `unsubscribe` with the same pattern removes it.

## Will

```
will	retain	status/bar	state=offline
```

The fields after `will` are one `retain` or `publish` line. When the client's
connection ends, for any reason, the broker acts on that line as if the client
had sent it. A later `will` replaces the earlier one.

## Status

A client says that it is up by retaining `status/<name>`, with its will setting
the same topic to offline:

```
will	retain	status/bar	state=offline
retain	status/bar	state=online
```

When a program stops answering, its last values stay retained but are old.
A reader that subscribes to `status/*` learns this and can show them as old.
Which topics belong to which status is agreed between those programs, like any
other kind.

## Errors

A failed line gets one `error` line. The second field is a code, not a topic.

```
error	badverb	verb=pubish	msg=unknown verb
error	badtopic	topic=Tag/1	msg=kind must be lowercase
error	noconnect	msg=send connect first
error	toolong	msg=line over 1024 bytes
```

| Code | Meaning |
| --- | --- |
| `badverb` | the verb is unknown |
| `badtopic` | the topic or pattern breaks the topic rules |
| `badarg` | the verb is known and another field is wrong |
| `noconnect` | the line came before `connect` |
| `toolong` | the line passed the byte limit |

## Limits

| | |
| --- | --- |
| line length | 1024 bytes, newline included |
| fields for each line | 32 |

## Rules

| Condition | Behavior |
| --- | --- |
| unknown verb, at a client | skip the line |
| unknown verb, at the broker | send `error badverb` |
| unknown kind | skip the line |
| unknown key | skip the field |
| expected kind never arrives | the reader reports it |
| line over the limit | reject it, and send `error toolong` |
| control character in a value | the writer removes it |
| broker exits | every client reads EOF |

A reader never rejects a line because the kind is unfamiliar. The check is on
absence, not on presence.

## Optional: the state projection

A broker can also write its retained lines to a file, in the same format. It
writes a temporary file and renames it over the target, so a reader never sees
half a file. This file serves readers that will not hold a connection.

A reader that watches this file must watch the **directory** with
`IN_CLOSE_WRITE | IN_MOVED_TO`. A rename makes a new inode, and a plain file
watch goes deaf.

## Reference implementation

`kipp.c` and `kipp.h` are a client. C99, libc only. Vendor the two files. The
broker is kippsrv. The wire is the contract, so two implementations that drift
still interoperate.
