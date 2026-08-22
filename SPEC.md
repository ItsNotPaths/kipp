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
a number. A key follows the same rule. A value holds any byte except a tab, a
newline and a control character.

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
version	1	tildesh	proto=1
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

## The kind vocabulary

kipp defines no kinds. A kind list is an agreement between the programs that
share a socket, and it lives with those programs. A new kind costs one line and
no consumer changes.

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
