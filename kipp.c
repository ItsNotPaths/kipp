/* kipp — Kind-first Inter-Process Protocol. See SPEC.md. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "kipp.h"

#define MAX_CONN 32

/* ------------------------------------------------------------------ util */

/* Tabs and newlines frame the protocol, so no value may hold one. Every
 * control character goes the same way. */
static void sanitize(char *dst, size_t cap, const char *src)
{
	size_t n = 0;

	while (*src && n + 1 < cap) {
		unsigned char c = (unsigned char)*src++;
		if (c >= 0x20 && c != 0x7f)
			dst[n++] = (char)c;
	}
	dst[n] = 0;
}

static int set_flags(int fd)
{
	int fl = fcntl(fd, F_GETFL);

	if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0)
		return -1;
	return fcntl(fd, F_SETFD, FD_CLOEXEC);
}

static int addr_of(struct sockaddr_un *a, const char *path)
{
	if (strlen(path) >= sizeof(a->sun_path))
		return -1;
	memset(a, 0, sizeof(*a));
	a->sun_family = AF_UNIX;
	strcpy(a->sun_path, path);
	return 0;
}

/* One line plus its newline. Fails the client rather than blocking it: on a
 * local socket a short write means the reader has stopped reading. */
static int wr(int fd, const char *line)
{
	char buf[KIPP_MAX_LINE + 1];
	int  n = snprintf(buf, sizeof(buf), "%s\n", line);
	ssize_t w;

	if (n <= 0 || n >= (int)sizeof(buf))
		return -1;
	w = send(fd, buf, (size_t)n, MSG_NOSIGNAL);
	return w == n ? 0 : -1;
}

/* -------------------------------------------------------------- read buf */

struct rdbuf {
	char b[KIPP_MAX_LINE + 1];
	int  len;
	int  skip;      /* discarding the tail of an overlong line */
};

/* 1 got bytes, 0 nothing waiting, -1 the peer is gone. */
static int rd_pull(int fd, struct rdbuf *r)
{
	ssize_t n;

	if (r->len == (int)sizeof(r->b))
		return 1;   /* full. rd_take decides what that means */
	n = recv(fd, r->b + r->len, sizeof(r->b) - (size_t)r->len, 0);
	if (n > 0) {
		r->len += (int)n;
		return 1;
	}
	if (n == 0)
		return -1;
	return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}

/* 1 a line is in out, 0 need more bytes, -1 the line was too long. */
static int rd_take(struct rdbuf *r, char *out)
{
	for (;;) {
		char *nl = memchr(r->b, '\n', (size_t)r->len);
		int   got, used;

		if (!nl) {
			if (r->len < (int)sizeof(r->b))
				return 0;
			r->len = 0;
			r->skip = 1;
			return -1;
		}
		*nl = 0;
		got = !r->skip;          /* a skipped line ends at this newline */
		if (got)
			memcpy(out, r->b, (size_t)(nl - r->b) + 1);
		r->skip = 0;

		used = (int)(nl - r->b) + 1;
		r->len -= used;
		memmove(r->b, nl + 1, (size_t)r->len);
		if (got)
			return 1;
	}
}

/* --------------------------------------------------------------- parsing */

int kipp_parse(struct kipp_msg *m, const char *line)
{
	char *p, *end;
	int   first = 1;

	if (!line || strlen(line) >= sizeof(m->buf))
		return -1;
	strcpy(m->buf, line);

	m->kind = NULL;
	m->nsubj = 0;
	m->nattr = 0;

	p = m->buf;
	end = p + strlen(p);
	if (end > p && end[-1] == '\r')
		*--end = 0;

	while (p < end) {
		char *tab = memchr(p, '\t', (size_t)(end - p));
		char *f = p;
		char *eq;

		if (tab) {
			*tab = 0;
			p = tab + 1;
		} else {
			p = end;
		}
		if (!*f)
			continue;

		if (first) {
			if (*f == '@')      /* reserved. The kind follows it. */
				continue;
			first = 0;
			m->kind = f;
			continue;
		}
		if (!m->kind)
			return -1;

		eq = strchr(f, '=');
		if (eq && eq != f) {
			if (m->nattr == KIPP_MAX_FIELDS)
				continue;
			*eq = 0;
			m->attr[m->nattr].k = f;
			m->attr[m->nattr].v = eq + 1;
			m->nattr++;
		} else if (m->nsubj < KIPP_MAX_FIELDS) {
			m->subj[m->nsubj++] = f;
		}
	}
	return m->kind ? 0 : -1;
}

const char *kipp_attr(const struct kipp_msg *m, const char *key)
{
	int i;

	for (i = 0; i < m->nattr; i++)
		if (strcmp(m->attr[i].k, key) == 0)
			return m->attr[i].v;
	return NULL;
}

int kipp_is_cmd(const struct kipp_msg *m)
{
	return m->kind && m->kind[0] >= 'A' && m->kind[0] <= 'Z';
}

/* -------------------------------------------------------------- building */

void kipp_begin(struct kipp_out *o, const char *kind)
{
	o->over = 0;
	sanitize(o->buf, sizeof(o->buf), kind);
	o->len = (int)strlen(o->buf);
}

void kipp_add(struct kipp_out *o, const char *fmt, ...)
{
	char raw[KIPP_MAX_LINE];
	char clean[KIPP_MAX_LINE];
	va_list ap;
	int n;

	if (o->over)
		return;

	va_start(ap, fmt);
	n = vsnprintf(raw, sizeof(raw), fmt, ap);
	va_end(ap);
	if (n < 0) {
		o->over = 1;
		return;
	}
	sanitize(clean, sizeof(clean), raw);

	n = (int)strlen(clean);
	if (o->len + 1 + n + 1 > (int)sizeof(o->buf)) {
		o->over = 1;
		return;
	}
	o->buf[o->len++] = '\t';
	memcpy(o->buf + o->len, clean, (size_t)n + 1);
	o->len += n;
}

const char *kipp_str(struct kipp_out *o)
{
	return o->over ? NULL : o->buf;
}

/* ---------------------------------------------------------------- server */

struct conn {
	int          fd;
	struct rdbuf r;
};

struct kipp_srv {
	int          fd;                /* listening */
	char         path[108];
	struct conn  c[MAX_CONN];
	int          nc;
	kipp_dump_fn dump;
	kipp_cmd_fn  cmd;
	void        *user;
	char         greet[KIPP_MAX_LINE];
};

static void drop(struct kipp_srv *s, int i)
{
	close(s->c[i].fd);
	s->c[i] = s->c[--s->nc];
}

/* A live owner answers a connect. A stale socket does not. */
static int in_use(const char *path)
{
	struct sockaddr_un a;
	int fd, ok;

	if (addr_of(&a, path) < 0)
		return 1;
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return 0;
	ok = connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0;
	close(fd);
	return ok;
}

struct kipp_srv *kipp_serve(const char *path, const char *greet,
                            kipp_dump_fn dump, kipp_cmd_fn cmd, void *user)
{
	struct sockaddr_un a;
	struct kipp_srv *s;

	if (addr_of(&a, path) < 0 || in_use(path)) {
		errno = EADDRINUSE;
		return NULL;
	}
	s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->fd = -1;

	s->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (s->fd < 0)
		goto fail;

	unlink(path);
	if (bind(s->fd, (struct sockaddr *)&a, sizeof(a)) < 0)
		goto fail;
	if (listen(s->fd, 8) < 0)
		goto fail;

	snprintf(s->path, sizeof(s->path), "%s", path);
	snprintf(s->greet, sizeof(s->greet), "%s", greet ? greet : "version\t1");
	s->dump = dump;
	s->cmd = cmd;
	s->user = user;
	return s;

fail:
	if (s->fd >= 0)
		close(s->fd);
	free(s);
	return NULL;
}

void kipp_stop(struct kipp_srv *s)
{
	int i;

	if (!s)
		return;
	for (i = 0; i < s->nc; i++)
		close(s->c[i].fd);
	close(s->fd);
	unlink(s->path);
	free(s);
}

int kipp_nfds(struct kipp_srv *s) { return 1 + s->nc; }

int kipp_fd(struct kipp_srv *s, int i)
{
	return i == 0 ? s->fd : s->c[i - 1].fd;
}

static void accept_one(struct kipp_srv *s)
{
	int fd = accept(s->fd, NULL, NULL);

	if (fd < 0)
		return;
	if (set_flags(fd) < 0 || s->nc == MAX_CONN) {
		close(fd);
		return;
	}
	if (wr(fd, s->greet) < 0) {
		close(fd);
		return;
	}
	s->c[s->nc].fd = fd;
	s->c[s->nc].r.len = 0;
	s->c[s->nc].r.skip = 0;
	s->nc++;

	if (s->dump)
		s->dump(s, fd, s->user);
	kipp_sendto(s, fd, "sync\tstate");
}

static void read_conn(struct kipp_srv *s, int i)
{
	char line[KIPP_MAX_LINE];
	struct kipp_msg m;

	for (;;) {
		int t = rd_take(&s->c[i].r, line);

		if (t == -1) {
			kipp_error(s, s->c[i].fd, "toolong", NULL,
			           "line over 1024 bytes");
			continue;
		}
		if (t == 0) {
			int p = rd_pull(s->c[i].fd, &s->c[i].r);

			if (p == 0)
				return;
			if (p < 0) {
				drop(s, i);
				return;
			}
			continue;
		}
		if (kipp_parse(&m, line) < 0) {
			kipp_error(s, s->c[i].fd, "badcmd", NULL, "unparsable line");
			continue;
		}
		if (s->cmd)
			s->cmd(s, &m, s->c[i].fd, s->user);
	}
}

/* By fd, not by index: this call can drop a consumer, and a caller part way
 * through its poll set must not have the rest shift under it. */
void kipp_ready(struct kipp_srv *s, int fd)
{
	int i;

	if (fd == s->fd) {
		accept_one(s);
		return;
	}
	for (i = 0; i < s->nc; i++)
		if (s->c[i].fd == fd) {
			read_conn(s, i);
			return;
		}
}

void kipp_sendto(struct kipp_srv *s, int fd, const char *line)
{
	int i;

	if (!line)
		return;
	if (wr(fd, line) == 0)
		return;
	for (i = 0; i < s->nc; i++)
		if (s->c[i].fd == fd) {
			drop(s, i);
			return;
		}
}

void kipp_cast(struct kipp_srv *s, const char *line)
{
	int i = 0;

	if (!line)
		return;
	while (i < s->nc) {
		if (wr(s->c[i].fd, line) < 0)
			drop(s, i);   /* drop() moves the last conn into i */
		else
			i++;
	}
}

void kipp_error(struct kipp_srv *s, int fd, const char *code,
                const char *cmd, const char *msg)
{
	struct kipp_out o;

	kipp_begin(&o, "error");
	kipp_add(&o, "%s", code);
	if (cmd)
		kipp_add(&o, "cmd=%s", cmd);
	if (msg)
		kipp_add(&o, "msg=%s", msg);
	kipp_sendto(s, fd, kipp_str(&o));
}

/* ---------------------------------------------------------------- client */

struct kipp_cli {
	int          fd;
	struct rdbuf r;
};

struct kipp_cli *kipp_open(const char *path)
{
	struct sockaddr_un a;
	struct kipp_cli *c;
	int fd;

	if (addr_of(&a, path) < 0)
		return NULL;
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return NULL;
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0 || set_flags(fd) < 0) {
		close(fd);
		return NULL;
	}
	c = calloc(1, sizeof(*c));
	if (!c) {
		close(fd);
		return NULL;
	}
	c->fd = fd;
	return c;
}

void kipp_close(struct kipp_cli *c)
{
	if (!c)
		return;
	close(c->fd);
	free(c);
}

int kipp_cli_fd(struct kipp_cli *c) { return c->fd; }

int kipp_recv(struct kipp_cli *c, struct kipp_msg *m)
{
	char line[KIPP_MAX_LINE];

	for (;;) {
		int t = rd_take(&c->r, line);

		if (t == -1)
			continue;               /* an overlong line. Skip it. */
		if (t == 1) {
			if (kipp_parse(m, line) == 0)
				return 1;
			continue;               /* unparsable. Skip it. */
		}
		t = rd_pull(c->fd, &c->r);
		if (t == 0)
			return 0;
		if (t < 0)
			return -1;
	}
}

int kipp_send(struct kipp_cli *c, const char *line)
{
	return line ? wr(c->fd, line) : -1;
}

/* --------------------------------------------------- state projection */

int kipp_state_write(const char *path, const char *text, size_t len)
{
	char tmp[256];
	int  fd;
	ssize_t w;

	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -1;
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	w = write(fd, text, len);
	close(fd);
	if (w != (ssize_t)len || rename(tmp, path) < 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}
