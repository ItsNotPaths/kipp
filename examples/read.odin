// kipp from Odin. Subscribes to everything, prints each line parsed.
//   odin run read.odin -file
package main

import "core:c"
import "core:fmt"
import "core:os"
import "core:strings"
import "core:sys/posix"

MAX_LINE :: 1024

Message :: struct {
	verb:   string,
	topic:  string,
	fields: map[string]string,
}

// The framing rule: field 1 is the verb, field 2 the topic, then key=value.
parse :: proc(line: string, allocator := context.allocator) -> (message: Message, ok: bool) {
	message.fields = make(map[string]string, allocator = allocator)

	rest := line
	for field in strings.split_iterator(&rest, "\t") {
		if field == "" do continue
		if message.verb == "" {
			message.verb = field
		} else if message.topic == "" {
			message.topic = field
		} else if equals := strings.index_byte(field, '='); equals > 0 {
			message.fields[field[:equals]] = field[equals + 1:]
		} // not key=value: skip it
	}
	return message, message.topic != ""
}

dial :: proc(path: string) -> (posix.FD, bool) {
	address: posix.sockaddr_un
	address.sun_family = .UNIX
	if len(path) >= len(address.sun_path) do return -1, false
	copy(address.sun_path[:], transmute([]c.char)path)

	descriptor := posix.socket(.UNIX, .STREAM)
	if descriptor < 0 do return -1, false
	if posix.connect(descriptor, (^posix.sockaddr)(&address), size_of(address)) != .OK {
		posix.close(descriptor)
		return -1, false
	}
	return descriptor, true
}

main :: proc() {
	path := len(os.args) > 1 ? os.args[1] : fmt.tprintf("%s/kippsrv.sock", os.get_env("XDG_RUNTIME_DIR", context.temp_allocator))

	descriptor, ok := dial(path)
	if !ok {
		fmt.eprintfln("cannot connect to %s", path)
		os.exit(1)
	}
	defer posix.close(descriptor)

	hello := "connect\tread_odin\nsubscribe\t*\n"
	posix.send(descriptor, raw_data(hello), len(hello), {})

	chunk: [4096]byte
	held: [dynamic]byte
	defer delete(held)

	for {
		count := posix.recv(descriptor, &chunk, len(chunk), {})
		if count <= 0 do break
		append(&held, ..chunk[:count])

		for {
			newline := strings.index_byte(string(held[:]), '\n')
			if newline < 0 do break
			line := strings.clone(string(held[:newline]), context.temp_allocator)
			remove_range(&held, 0, newline + 1)

			if len(line) + 1 > MAX_LINE { // the limit counts the newline
				fmt.eprintln("line over the limit, skipped")
				continue
			}
			message, good := parse(line, context.temp_allocator)
			if !good do continue

			fmt.printfln("%-8s %-16s %v", message.verb, message.topic, message.fields)
			if message.verb == "sync" {
				request := "publish\ttag/request\ttag=4\n"
				posix.send(descriptor, raw_data(request), len(request), {})
			}
		}
		free_all(context.temp_allocator)
	}
}
