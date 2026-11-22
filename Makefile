CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

check: tests/t_kipp
	./tests/t_kipp

tests/t_kipp: tests/t_kipp.c kipp.c kipp.h
	$(CC) $(CFLAGS) -o $@ tests/t_kipp.c kipp.c

examples: check
	./examples/check.sh

clean:
	rm -f tests/t_kipp

.PHONY: check examples clean
