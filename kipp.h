/* kipp — Kind-first Inter-Process Protocol. See SPEC.md.
 * C99, libc only. Vendor this file and kipp.c. */
#ifndef KIPP_H
#define KIPP_H

#include <stddef.h>

#define KIPP_MAX_LINE   1024
#define KIPP_MAX_FIELDS 32

/* -------------------------------------------------------------- parsing */

/* A parsed line. Points into its own buf, so it outlives the source. */
struct kipp_msg {
	const char *kind;
	const char *subj[KIPP_MAX_FIELDS];
	int         nsubj;
	struct { const char *k, *v; } attr[KIPP_MAX_FIELDS];
	int         nattr;
	char        buf[KIPP_MAX_LINE];
};

int         kipp_parse(struct kipp_msg *m, const char *line);   /* 0, or -1 */
const char *kipp_attr(const struct kipp_msg *m, const char *key);
int         kipp_is_cmd(const struct kipp_msg *m);   /* kind is UPPERCASE */

/* -------------------------------------------------------------- building */

struct kipp_out {
	char buf[KIPP_MAX_LINE];
	int  len;
	int  over;      /* the line passed the limit. kipp_str returns NULL */
};

void        kipp_begin(struct kipp_out *o, const char *kind);
void        kipp_add(struct kipp_out *o, const char *fmt, ...);   /* one field */
const char *kipp_str(struct kipp_out *o);   /* no newline. NULL when over */

/* A field is a field. `=` in it makes it an attribute:
 *   kipp_begin(&o, "mon"); kipp_add(&o, "%s", name); kipp_add(&o, "w=%d", w); */

/* --------------------------------------------------------------- server */

struct kipp_srv;

/* Send the current state to one consumer. kipp.c appends `sync state`. */
typedef void (*kipp_dump_fn)(struct kipp_srv *s, int fd, void *user);
typedef void (*kipp_cmd_fn)(struct kipp_srv *s, const struct kipp_msg *m,
                            int fd, void *user);

struct kipp_srv *kipp_serve(const char *path, const char *greet,
                            kipp_dump_fn dump, kipp_cmd_fn cmd, void *user);
void kipp_stop(struct kipp_srv *s);

/* Descriptors, for a foreign event loop. The set changes as consumers come
 * and go, so re-read it each time round the loop. kipp_ready takes the fd
 * and not the index, so a drop during the pass is harmless. */
int  kipp_nfds(struct kipp_srv *s);
int  kipp_fd(struct kipp_srv *s, int i);
void kipp_ready(struct kipp_srv *s, int fd);

void kipp_sendto(struct kipp_srv *s, int fd, const char *line);
void kipp_cast(struct kipp_srv *s, const char *line);
void kipp_error(struct kipp_srv *s, int fd, const char *code,
                const char *cmd, const char *msg);

/* --------------------------------------------------------------- client */

struct kipp_cli;

struct kipp_cli *kipp_open(const char *path);
void             kipp_close(struct kipp_cli *c);
int              kipp_cli_fd(struct kipp_cli *c);

/* 1 a line is in m, 0 nothing buffered, -1 the publisher is gone. */
int kipp_recv(struct kipp_cli *c, struct kipp_msg *m);
int kipp_send(struct kipp_cli *c, const char *line);

/* --------------------------------------------------- state projection */

/* Write text to path through a temporary file and a rename. */
int kipp_state_write(const char *path, const char *text, size_t len);

#endif
