CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

all: libkipp.a

libkipp.a: kipp.o
	$(AR) rcs $@ $^

kipp.o: kipp.c kipp.h
	$(CC) $(CFLAGS) -c -o $@ $<

check: t_kipp
	./t_kipp

t_kipp: t_kipp.c kipp.c kipp.h
	$(CC) $(CFLAGS) -o $@ t_kipp.c kipp.c

clean:
	rm -f *.o *.a t_kipp

.PHONY: all check clean
