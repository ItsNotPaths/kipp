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

/* ------------------------------------------------------------------ util */

static int is_control(unsigned char character)
{
	return character < 0x20 || character == 0x7f;
}

/* Tabs and newlines frame the protocol, so no value may hold one. Every
 * control character goes the same way. */
static void sanitize(char *target, size_t capacity, const char *source)
{
	size_t length = 0;

	while (*source && length + 1 < capacity) {
		unsigned char character = (unsigned char)*source++;
		if (!is_control(character))
			target[length++] = (char)character;
	}
	target[length] = 0;
}

static int has_control(const char *text)
{
	while (*text)
		if (is_control((unsigned char)*text++))
			return 1;
	return 0;
}

#define DIGITS "0123456789"

/* start..end lies inside a terminated string, so strspn may read past end. */
static int is_word(const char *start, const char *end)
{
	size_t length = (size_t)(end - start);

	return length > 0 && strspn(start, "abcdefghijklmnopqrstuvwxyz_" DIGITS) >= length;
}

static int is_kind(const char *start, const char *end)
{
	return is_word(start, end) && strspn(start, DIGITS) < (size_t)(end - start);
}

static int is_topic(const char *topic)
{
	const char *slash = strchr(topic, '/');
	const char *kind_end = slash ? slash : topic + strlen(topic);

	return is_kind(topic, kind_end) && !has_control(kind_end) &&
	       !strpbrk(kind_end, "*?[");
}

static int address_of(struct sockaddr_un *address, const char *path)
{
	if (strlen(path) >= sizeof(address->sun_path))
		return -1;
	memset(address, 0, sizeof(*address));
	address->sun_family = AF_UNIX;
	strcpy(address->sun_path, path);
	return 0;
}

/* One line plus its newline. Fails rather than blocking: on a local socket a
 * short write means the reader has stopped reading. */
static int write_line(int descriptor, const char *line)
{
	char buffer[KIPP_MAX_LINE + 1];
	int  length = snprintf(buffer, sizeof(buffer), "%s\n", line);

	if (length <= 0 || length >= (int)sizeof(buffer))
		return -1;
	return send(descriptor, buffer, (size_t)length, MSG_NOSIGNAL) == length ? 0 : -1;
}

/* ----------------------------------------------------------------- input */

struct input {
	char bytes[KIPP_MAX_LINE + 1];
	int  length;
	int  skipping;  /* discarding the tail of an overlong line */
};

/* 1 got bytes, 0 nothing waiting, -1 the peer is gone. */
static int input_fill(int descriptor, struct input *input)
{
	ssize_t count;

	if (input->length == (int)sizeof(input->bytes))
		return 1;   /* full. input_take decides what that means */
	count = recv(descriptor, input->bytes + input->length,
	             sizeof(input->bytes) - (size_t)input->length, 0);
	if (count > 0) {
		input->length += (int)count;
		return 1;
	}
	if (count == 0)
		return -1;
	return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}

/* 1 a line is in line, 0 need more bytes, -1 the line was too long. */
static int input_take(struct input *input, char *line)
{
	for (;;) {
		char *newline = memchr(input->bytes, '\n', (size_t)input->length);
		int   wanted, used;

		if (!newline) {
			if (input->length < (int)sizeof(input->bytes))
				return 0;
			input->length = 0;
			input->skipping = 1;
			return -1;
		}
		*newline = 0;
		wanted = !input->skipping;   /* a skipped line ends at this newline */
		if (wanted)
			memcpy(line, input->bytes, (size_t)(newline - input->bytes) + 1);
		input->skipping = 0;

		used = (int)(newline - input->bytes) + 1;
		input->length -= used;
		memmove(input->bytes, newline + 1, (size_t)input->length);
		if (wanted)
			return 1;
	}
}

/* --------------------------------------------------------------- parsing */

static char *next_field(char **cursor, char *end)
{
	char *field = *cursor;
	char *tab = memchr(field, '\t', (size_t)(end - field));

	if (tab)
		*tab = 0;
	*cursor = tab ? tab + 1 : end;
	return field;
}

static void add_field(struct kipp_message *message, char *field)
{
	char *equals = strchr(field, '=');

	if (!equals || equals == field || message->field_count == KIPP_MAX_FIELDS)
		return;   /* not key=value. Skip it */
	*equals = 0;
	message->field[message->field_count].key = field;
	message->field[message->field_count].value = equals + 1;
	message->field_count++;
}

int kipp_parse(struct kipp_message *message, const char *line)
{
	char  *cursor, *end;
	size_t length;

	if (!line || (length = strlen(line)) >= sizeof(message->buffer))
		return -1;
	memcpy(message->buffer, line, length + 1);

	message->verb = NULL;
	message->topic = NULL;
	message->field_count = 0;

	cursor = message->buffer;
	end = cursor + length;
	if (end > cursor && end[-1] == '\r')
		*--end = 0;

	while (cursor < end) {
		char *field = next_field(&cursor, end);

		if (!*field)
			continue;
		if (!message->verb)
			message->verb = field;
		else if (!message->topic)
			message->topic = field;
		else
			add_field(message, field);
	}
	return message->topic ? 0 : -1;
}

const char *kipp_value(const struct kipp_message *message, const char *key)
{
	int index;

	for (index = 0; index < message->field_count; index++)
		if (strcmp(message->field[index].key, key) == 0)
			return message->field[index].value;
	return NULL;
}

int kipp_kind_is(const struct kipp_message *message, const char *kind)
{
	size_t length = strlen(kind);

	return strncmp(message->topic, kind, length) == 0 &&
	       (message->topic[length] == '/' || message->topic[length] == 0);
}

/* -------------------------------------------------------------- building */

void kipp_begin(struct kipp_record *record, const char *verb, const char *topic)
{
	record->length = snprintf(record->buffer, sizeof(record->buffer), "%s\t%s",
	                          verb, topic);
	record->refused = !is_word(verb, verb + strlen(verb)) || !is_topic(topic) ||
	                  record->length >= (int)sizeof(record->buffer);
}

void kipp_add(struct kipp_record *record, const char *key, const char *format, ...)
{
	char raw[KIPP_MAX_LINE];
	char clean[KIPP_MAX_LINE];
	int  room = (int)sizeof(record->buffer) - record->length;
	int  length;
	va_list arguments;

	if (record->refused)
		return;
	va_start(arguments, format);
	length = vsnprintf(raw, sizeof(raw), format, arguments);
	va_end(arguments);
	if (!is_word(key, key + strlen(key)) || length < 0 || length >= (int)sizeof(raw)) {
		record->refused = 1;
		return;
	}
	sanitize(clean, sizeof(clean), raw);

	length = snprintf(record->buffer + record->length, (size_t)room,
	                  "\t%s=%s", key, clean);
	if (length >= room) {
		record->refused = 1;
		return;
	}
	record->length += length;
}

const char *kipp_text(const struct kipp_record *record)
{
	return record->refused ? NULL : record->buffer;
}

/* ---------------------------------------------------------------- client */

struct kipp_client {
	int          descriptor;
	struct input input;
};

static int send_joined(struct kipp_client *client, const char *verb, const char *rest)
{
	char line[KIPP_MAX_LINE];
	int  length;

	if (!rest)
		return -1;
	length = snprintf(line, sizeof(line), "%s\t%s", verb, rest);
	if (length < 0 || length >= (int)sizeof(line))
		return -1;
	return write_line(client->descriptor, line);
}

static int send_pattern(struct kipp_client *client, const char *verb, const char *pattern)
{
	if (!pattern || has_control(pattern))
		return -1;
	return send_joined(client, verb, pattern);
}

struct kipp_client *kipp_open(const char *path, const char *name)
{
	struct sockaddr_un  address;
	struct kipp_client *client;
	int descriptor, flags;

	if (!is_kind(name, name + strlen(name)) || address_of(&address, path) < 0) {
		errno = EINVAL;
		return NULL;
	}
	descriptor = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (descriptor < 0)
		return NULL;
	if (connect(descriptor, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    (flags = fcntl(descriptor, F_GETFL)) < 0 ||
	    fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
		close(descriptor);
		return NULL;
	}
	client = calloc(1, sizeof(*client));
	if (!client) {
		close(descriptor);
		return NULL;
	}
	client->descriptor = descriptor;
	if (send_joined(client, "connect", name) < 0) {
		kipp_close(client);
		return NULL;
	}
	return client;
}

void kipp_close(struct kipp_client *client)
{
	if (!client)
		return;
	close(client->descriptor);
	free(client);
}

int kipp_descriptor(struct kipp_client *client) { return client->descriptor; }

int kipp_receive(struct kipp_client *client, struct kipp_message *message)
{
	char line[KIPP_MAX_LINE];

	for (;;) {
		int result = input_take(&client->input, line);

		if (result == -1)
			continue;               /* an overlong line. Skip it */
		if (result == 1) {
			if (kipp_parse(message, line) == 0)
				return 1;
			continue;               /* unparsable. Skip it */
		}
		result = input_fill(client->descriptor, &client->input);
		if (result <= 0)
			return result;
	}
}

int kipp_send(struct kipp_client *client, const char *line)
{
	return line ? write_line(client->descriptor, line) : -1;
}

int kipp_will(struct kipp_client *client, const char *line)
{
	return send_joined(client, "will", line);
}

int kipp_subscribe(struct kipp_client *client, const char *pattern)
{
	return send_pattern(client, "subscribe", pattern);
}

int kipp_unsubscribe(struct kipp_client *client, const char *pattern)
{
	return send_pattern(client, "unsubscribe", pattern);
}
