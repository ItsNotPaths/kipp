/* kipp tests. `make check` */
#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kipp.h"

#define SOCK     "/tmp/kipp-test.sock"
#define MAX_POLL 40

static int fails;

static void ok(int cond, const char *what)
{
	if (!cond) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

/* ------------------------------------------------------------- parsing */

static void test_parse(void)
{
	struct kipp_msg m;

	ok(kipp_parse(&m, "tag\teDP-1\t2\tstate=focused,occupied") == 0, "parse ok");
	ok(strcmp(m.kind, "tag") == 0, "kind");
	ok(m.nsubj == 2, "nsubj");
	ok(strcmp(m.subj[0], "eDP-1") == 0, "subj0");
	ok(strcmp(m.subj[1], "2") == 0, "subj1");
	ok(m.nattr == 1, "nattr");
	ok(strcmp(kipp_attr(&m, "state"), "focused,occupied") == 0, "attr");
	ok(kipp_attr(&m, "nope") == NULL, "missing attr");
	ok(!kipp_is_cmd(&m), "fact is not a command");

	ok(kipp_parse(&m, "TAG\t4") == 0, "parse command");
	ok(kipp_is_cmd(&m), "uppercase is a command");

	/* a value holds a space and an = with no quoting */
	ok(kipp_parse(&m, "net\twifi\tssid=hotel guest\turl=a=b") == 0, "parse odd");
	ok(strcmp(kipp_attr(&m, "ssid"), "hotel guest") == 0, "space in value");
	ok(strcmp(kipp_attr(&m, "url"), "a=b") == 0, "= in value");

	/* reserved metadata field is skipped */
	ok(kipp_parse(&m, "@t=1\tfocus\teDP-1") == 0, "parse reserved");
	ok(strcmp(m.kind, "focus") == 0, "kind after reserved");
	ok(m.nsubj == 1, "subj after reserved");

	/* a leading = is a subject field, not an attribute */
	ok(kipp_parse(&m, "x\t=v") == 0, "parse leading eq");
	ok(m.nsubj == 1 && m.nattr == 0, "leading = stays positional");

	ok(kipp_parse(&m, "") == -1, "empty line rejected");
	ok(kipp_parse(&m, "\t\t") == -1, "tabs only rejected");
}

/* ------------------------------------------------------------ building */

static void test_build(void)
{
	struct kipp_out o;
	int i;

	kipp_begin(&o, "mon");
	kipp_add(&o, "%s", "eDP-1");
	kipp_add(&o, "w=%d", 2256);
	kipp_add(&o, "scale=%.1f", 1.5);
	ok(strcmp(kipp_str(&o), "mon\teDP-1\tw=2256\tscale=1.5") == 0, "build");

	/* a tab or a newline in a value cannot forge a line */
	kipp_begin(&o, "title");
	kipp_add(&o, "0x1");
	kipp_add(&o, "t=a\tb\nfocus\tX");
	ok(strcmp(kipp_str(&o), "title\t0x1\tt=abfocusX") == 0, "sanitize");

	/* over the limit reports instead of truncating */
	kipp_begin(&o, "big");
	for (i = 0; i < 200; i++)
		kipp_add(&o, "%s", "0123456789");
	ok(kipp_str(&o) == NULL, "over limit");
}

/* -------------------------------------------------------------- socket */

static int saw_tag;

static void dump(struct kipp_srv *s, int fd, void *user)
{
	(void)user;
	kipp_sendto(s, fd, "mon\teDP-1\tw=2256");
	kipp_sendto(s, fd, "focus\teDP-1");
}

static void oncmd(struct kipp_srv *s, const struct kipp_msg *m, int fd,
                  void *user)
{
	(void)user;
	if (strcmp(m->kind, "TAG") == 0 && m->nsubj == 1) {
		saw_tag = atoi(m->subj[0]);
		kipp_cast(s, "tag\teDP-1\t4\tstate=focused");
	} else {
		kipp_error(s, fd, "badcmd", m->kind, "unknown command");
	}
}

static void pump(struct kipp_srv *s)
{
	struct pollfd p[MAX_POLL];
	int fds[MAX_POLL];
	int n, i;

	n = kipp_nfds(s);
	if (n > MAX_POLL)
		n = MAX_POLL;
	for (i = 0; i < n; i++) {
		fds[i] = kipp_fd(s, i);
		p[i].fd = fds[i];
		p[i].events = POLLIN;
		p[i].revents = 0;
	}
	if (poll(p, (nfds_t)n, 20) <= 0)
		return;
	for (i = 0; i < n; i++)
		if (p[i].revents)
			kipp_ready(s, fds[i]);
}

/* Pump the server until the client has a line, or give up. */
static int expect(struct kipp_srv *s, struct kipp_cli *c, struct kipp_msg *m)
{
	int tries;

	for (tries = 0; tries < 100; tries++) {
		int r = kipp_recv(c, m);

		if (r == 1)
			return 1;
		if (r < 0)
			return 0;
		pump(s);
	}
	return 0;
}

static void test_socket(void)
{
	struct kipp_srv *s;
	struct kipp_cli *c;
	struct kipp_msg m;

	unlink(SOCK);
	s = kipp_serve(SOCK, "version\t1\ttest\tproto=1", dump, oncmd, NULL);
	ok(s != NULL, "serve");
	if (!s)
		return;

	ok(kipp_serve(SOCK, NULL, NULL, NULL, NULL) == NULL, "second bind refused");

	c = kipp_open(SOCK);
	ok(c != NULL, "connect");
	if (!c) {
		kipp_stop(s);
		return;
	}
	pump(s);

	ok(expect(s, c, &m) && strcmp(m.kind, "version") == 0, "greeting");
	ok(expect(s, c, &m) && strcmp(m.kind, "mon") == 0, "dump line 1");
	ok(expect(s, c, &m) && strcmp(m.kind, "focus") == 0, "dump line 2");
	ok(expect(s, c, &m) && strcmp(m.kind, "sync") == 0, "sync ends the dump");

	kipp_send(c, "TAG\t4");
	ok(expect(s, c, &m) && strcmp(m.kind, "tag") == 0, "command took effect");
	ok(saw_tag == 4, "command argument");

	kipp_send(c, "TGA");
	ok(expect(s, c, &m) && strcmp(m.kind, "error") == 0, "bad command errors");
	ok(m.nsubj == 1 && strcmp(m.subj[0], "badcmd") == 0, "error code");
	ok(strcmp(kipp_attr(&m, "cmd"), "TGA") == 0, "error names the command");

	kipp_stop(s);
	ok(kipp_recv(c, &m) == -1, "consumer sees EOF");
	kipp_close(c);
}

int main(void)
{
	test_parse();
	test_build();
	test_socket();
	printf(fails ? "%d failed\n" : "all passed\n", fails);
	return fails != 0;
}
