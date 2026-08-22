CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

check: t_kipp
	./t_kipp

t_kipp: t_kipp.c kipp.c kipp.h
	$(CC) $(CFLAGS) -o $@ t_kipp.c kipp.c

clean:
	rm -f t_kipp

.PHONY: check clean
