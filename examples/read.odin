// kipp from Odin. Connects, reads the dump, prints each line parsed.
//   odin run read.odin -file
package main

import "core:c"
import "core:fmt"
import "core:os"
import "core:strings"
import "core:sys/posix"

MAX_LINE :: 1024

Msg :: struct {
	kind: string,
	subj: [dynamic]string,
	attr: map[string]string,
}

// The framing rule: field 1 is the kind, then positional subject up to the
// first field holding '=', then key=value.
parse :: proc(line: string, allocator := context.allocator) -> (m: Msg, ok: bool) {
	m.subj = make([dynamic]string, allocator)
	m.attr = make(map[string]string, allocator = allocator)

	rest := line
	for f in strings.split_iterator(&rest, "\t") {
		if f == "" do continue
		if m.kind == "" {
			if strings.has_prefix(f, "@") do continue // reserved
			m.kind = f
			continue
		}
		if i := strings.index_byte(f, '='); i > 0 {
			m.attr[f[:i]] = f[i + 1:]
		} else {
			append(&m.subj, f)
		}
	}
	return m, m.kind != ""
}

dial :: proc(path: string) -> (posix.FD, bool) {
	addr: posix.sockaddr_un
	addr.sun_family = .UNIX
	if len(path) >= len(addr.sun_path) do return -1, false
	copy(addr.sun_path[:], transmute([]c.char)path)

	fd := posix.socket(.UNIX, .STREAM)
	if fd < 0 do return -1, false
	if posix.connect(fd, (^posix.sockaddr)(&addr), size_of(addr)) != .OK {
		posix.close(fd)
		return -1, false
	}
	return fd, true
}

main :: proc() {
	path := len(os.args) > 1 ? os.args[1] : "/tmp/kipp-demo.sock"

	fd, ok := dial(path)
	if !ok {
		fmt.eprintfln("cannot connect to %s", path)
		os.exit(1)
	}
	defer posix.close(fd)

	buf: [4096]byte
	held: [dynamic]byte
	defer delete(held)

	for {
		n := posix.recv(fd, &buf, len(buf), {})
		if n <= 0 do break
		append(&held, ..buf[:n])

		for {
			i := strings.index_byte(string(held[:]), '\n')
			if i < 0 do break
			line := strings.clone(string(held[:i]), context.temp_allocator)
			remove_range(&held, 0, i + 1)

			if len(line) > MAX_LINE {
				fmt.eprintln("line over the limit, skipped")
				continue
			}
			m, good := parse(line, context.temp_allocator)
			if !good do continue

			fmt.printfln("%-12s %v %v", m.kind, m.subj[:], m.attr)
			if m.kind == "sync" {
				msg := "TAG\t4\n"
				posix.send(fd, raw_data(msg), len(msg), {})
			}
		}
		free_all(context.temp_allocator)
	}
}
