/* kipp — Kind-first Inter-Process Protocol, version 2. See SPEC.md.
 * A client. C99, libc only. Vendor this file and kipp.c. */
#ifndef KIPP_H
#define KIPP_H

#define KIPP_MAX_LINE   1024
#define KIPP_MAX_FIELDS 32

/* -------------------------------------------------------------- parsing */

/* A parsed line. Points into its own buffer, so it outlives the source.
 * topic is field 2: a pattern after sync, a code after error. */
struct kipp_message {
	const char *verb;
	const char *topic;
	struct { const char *key, *value; } field[KIPP_MAX_FIELDS];
	int         field_count;
	char        buffer[KIPP_MAX_LINE];
};

int         kipp_parse(struct kipp_message *message, const char *line);   /* 0, or -1 */
const char *kipp_value(const struct kipp_message *message, const char *key);
int         kipp_kind_is(const struct kipp_message *message, const char *kind);

/* -------------------------------------------------------------- building */

struct kipp_record {
	char buffer[KIPP_MAX_LINE];
	int  length;
	int  refused;   /* over the limit, or a topic or key breaks the rules */
};

void        kipp_begin(struct kipp_record *record, const char *verb, const char *topic);
void        kipp_add(struct kipp_record *record, const char *key, const char *format, ...);
const char *kipp_text(const struct kipp_record *record);   /* no newline. NULL when refused */

/*   kipp_begin(&record, "retain", "battery/BAT0");
 *   kipp_add(&record, "level", "%d", level);         */

/* --------------------------------------------------------------- client */

struct kipp_client;

/* Connects and sends `connect name`. The broker's `version` line is the
 * first one kipp_receive returns. */
struct kipp_client *kipp_open(const char *path, const char *name);
void                kipp_close(struct kipp_client *client);
int                 kipp_descriptor(struct kipp_client *client);

/* 1 a line is in message, 0 nothing buffered, -1 the broker is gone. */
int kipp_receive(struct kipp_client *client, struct kipp_message *message);

/* Each returns 0, or -1. line is a kipp_text result. */
int kipp_send(struct kipp_client *client, const char *line);
int kipp_will(struct kipp_client *client, const char *line);
int kipp_subscribe(struct kipp_client *client, const char *pattern);
int kipp_unsubscribe(struct kipp_client *client, const char *pattern);

#endif
