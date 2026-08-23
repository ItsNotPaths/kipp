# kipp

Kind-first Inter-Process Protocol. Version 1.

A text line protocol for local IPC over a unix socket. One publisher sends
facts. Any number of consumers read them and send commands back.

## Framing

One fact for each line. Fields are tab separated. The line ends with `\n`.

```
kind	<positional subject>	key=value	key=value
```

- Field 1 is the **kind**.
- The fields after it are **positional subject**, up to the first field that
  contains `=`.
- Every field from there is **`key=value`** and unordered.

A kind is a word of lowercase letters, digits and underscores. A kind is never
a number. A key follows the same rule.

An **attribute value** holds any byte except a tab, a newline and a control
character. Only the first `=` in a field separates the key from the value, so
a value may hold more of them: `name=home=wifi` is one attribute.

A **fact needs at least one subject field**, because the subject is what
identifies it. A subject field may not hold `=`: the framing rule would read it
as the first attribute, the fact would keep no subject, and its key would
become the bare kind — colliding with every other fact of that kind. A writer
must refuse such a line.

A fact of which there can only ever be one still carries a subject. Write
`uptime host secs=1234`, not `uptime secs=1234`.

An **event** carries no current value, so it is never stored and needs no
subject. `emit screenshot` and `key_press super+3` are complete.

Choose a subject that cannot hold one: a UUID, a MAC, a bus name, a device
path. A name a person typed belongs in an attribute.

## Case marks direction

| Case | Meaning | Direction |
| --- | --- | --- |
| lowercase | a fact | publisher to consumer |
| UPPERCASE | a command | consumer to publisher |

```
tag	eDP-1	2	state=focused
TAG	4
```

## Session

A consumer connects to the socket. The publisher sends:

1. One `version` line.
2. The full current state, one line for each fact.
3. One `sync	state` line.

After `sync	state`, the publisher sends deltas, events and errors on the same
connection, in the order they happen. The consumer writes commands on the same
connection at any time.

```
version	1	shell	proto=1
mon	eDP-1	w=2256	h=1504	scale=1.5
focus	eDP-1
tag	eDP-1	2	state=focused,occupied
mode	normal
sync	state
tag	eDP-1	3	state=focused,occupied
key_press	super+3
```

A command gets no reply when it succeeds. The state lines that follow show the
result.

## Retraction, and doubt

A fact can end in two ways, and they are not the same.

**It is gone.** A `drop` line names the kind and subject of the fact. The
consumer forgets it.

```
tray	:1.456/StatusNotifierItem	state=registered
drop	tray	:1.456/StatusNotifierItem
```

**It is no longer visible.** The thing probably still exists, but whatever
reported it stopped answering. A `stale` line names the same fields. The
consumer keeps the fact and shows that it is old.

```
bt	00:11	name=Buds	state=connected
stale	bt	00:11
```

Any later fact for that subject clears the mark. A publisher repeats a stale
fact and its mark in the dump, so a consumer that connects afterward learns
both.

Sending the fact back with no attributes would not do for either. A consumer
cannot tell that from a fact that has no attributes, and they mean opposite
things.

## Errors

A failed command gets one `error` line. The line names the command that failed.

```
error	badcmd	cmd=TGA	msg=unknown command
error	badarg	cmd=TAG	msg=tag out of range
error	toolong	msg=line over 1024 bytes
```

| Code | Meaning |
| --- | --- |
| `badcmd` | the verb is unknown |
| `badarg` | the verb is known and an argument is wrong |
| `toolong` | the line passed the byte limit |
| `nosrc` | the command needs a source that is not connected |

## Limits

| | |
| --- | --- |
| line length | 1024 bytes, newline included |
| fields for each line | 32 |

## Rules

| Condition | Behavior |
| --- | --- |
| unknown kind | skip the line |
| unknown key | skip the field |
| expected kind never arrives | the consumer reports it |
| line over the limit | reject it, and send `error toolong` |
| control character in a value | the writer removes it |
| publisher exits | every consumer reads EOF |

A reader never rejects a line because the kind is unfamiliar. The check is on
absence, not on presence.

## The kinds kipp defines

kipp defines four kinds, all about the protocol itself. It defines no kind
about anything else.

| Kind | Direction | Means |
| --- | --- | --- |
| `version` | publisher | the greeting, first line of a session |
| `sync` | publisher | `sync	state` ends the dump |
| `error` | publisher | a command failed |
| `drop` | publisher | a fact is gone. Its fields are the kind and subject |
| `stale` | publisher | a fact is last-known, not current. Same fields |

Every other kind is an agreement between the programs that share a socket, and
lives with those programs. A new one costs one line and no consumer changes.

## Optional: the state projection

A publisher can also write the state to a file, in the same format. It writes a
temporary file and renames it over the target, so a reader never sees half a
file. This file serves readers that will not hold a connection.

A reader that watches this file must watch the **directory** with
`IN_CLOSE_WRITE | IN_MOVED_TO`. A rename makes a new inode, and a plain file
watch goes deaf.

## Reserved

A field that starts with `@`, before the kind, is reserved for message metadata.
Version 1 never sends one. A reader skips it.

## Reference implementation

`kipp.c` and `kipp.h`. C99, libc only. Vendor the two files. The wire is the
contract, so two implementations that drift still interoperate.
