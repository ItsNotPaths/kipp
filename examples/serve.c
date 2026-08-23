/* A kipp publisher for the examples to talk to. `make demo` */
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "../kipp.h"

#define SOCK "/tmp/kipp-demo.sock"


static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }

static void dump(struct kipp_srv *s, int fd, void *user)
{
	(void)user;
	kipp_sendto(s, fd, "mon\teDP-1\tw=2256\th=1504\tscale=1.5");
	kipp_sendto(s, fd, "focus\teDP-1");
	kipp_sendto(s, fd, "tag\teDP-1\t2\tstate=focused,occupied");
	kipp_sendto(s, fd, "net\twifi\tssid=hotel guest\tsignal=62\tstate=up");
	kipp_sendto(s, fd, "mode\tnormal");
}

static void cmd(struct kipp_srv *s, const struct kipp_msg *m, int fd, void *user)
{
	struct kipp_out o;

	(void)user;
	if (strcmp(m->kind, "TAG") != 0 || m->nsubj != 1) {
		kipp_error(s, fd, "badcmd", m->kind, "unknown command");
		return;
	}
	kipp_begin(&o, "tag");
	kipp_add(&o, "eDP-1");
	kipp_add(&o, "%s", m->subj[0]);
	kipp_add(&o, "state=focused,occupied");
	kipp_cast(s, kipp_str(&o));
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : SOCK;
	struct pollfd p[34];
	int fds[34], n, i, tick = 0;
	struct kipp_srv *s;

	signal(SIGINT, stop);
	signal(SIGTERM, stop);

	s = kipp_serve(path, "version\t1\tdemo\tproto=1", dump, cmd, NULL);
	if (!s) {
		perror("kipp_serve");
		return 1;
	}
	fprintf(stderr, "serving %s\n", path);

	while (running) {
		n = kipp_nfds(s);
		for (i = 0; i < n; i++) {
			fds[i] = kipp_fd(s, i);
			p[i].fd = fds[i];
			p[i].events = POLLIN;
			p[i].revents = 0;
		}
		if (poll(p, (nfds_t)n, 500) > 0)
			for (i = 0; i < n; i++)
				if (p[i].revents)
					kipp_ready(s, fds[i]);
		if (++tick % 4 == 0)
			kipp_cast(s, "key_press\tsuper+3");
		if (tick == 6)
			kipp_cast(s, "stale\tnet\twifi");        /* still there, unseen */
		if (tick == 10)
			kipp_cast(s, "drop\ttag\teDP-1\t2");    /* gone */
	}
	kipp_stop(s);
	return 0;
}
