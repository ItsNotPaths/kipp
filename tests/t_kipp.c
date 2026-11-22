/* kipp tests. `make check` */
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../kipp.h"

#define SOCKET_PATH "/tmp/kipp-test.sock"

static int failures;

static void ok(int condition, const char *what)
{
	if (!condition) {
		failures++;
		printf("FAIL %s\n", what);
	}
}

/* ------------------------------------------------------------- parsing */

static void test_parse(void)
{
	struct kipp_message message;

	ok(kipp_parse(&message, "retain\tbattery/BAT0\tlevel=80\tstate=charging") == 0, "parse");
	ok(strcmp(message.verb, "retain") == 0, "verb");
	ok(strcmp(message.topic, "battery/BAT0") == 0, "topic");
	ok(message.field_count == 2, "field count");
	ok(strcmp(kipp_value(&message, "state"), "charging") == 0, "value");
	ok(kipp_value(&message, "nope") == NULL, "missing key");

	/* a value holds a space and an = with no quoting */
	ok(kipp_parse(&message, "retain\tnet/wifi\tssid=hotel guest\turl=a=b") == 0, "parse odd");
	ok(strcmp(kipp_value(&message, "ssid"), "hotel guest") == 0, "space in value");
	ok(strcmp(kipp_value(&message, "url"), "a=b") == 0, "= in value");

	/* a field after the topic that is not key=value is skipped */
	ok(kipp_parse(&message, "publish\tx/1\tstray\t=v\tkey=value") == 0, "parse stray");
	ok(message.field_count == 1, "stray fields skipped");

	ok(kipp_parse(&message, "retain\ttray/:1.456") == 0, "empty payload");
	ok(message.field_count == 0, "no fields");

	char many[KIPP_MAX_LINE] = "retain\tx/1";
	int  index;

	for (index = 0; index < KIPP_MAX_FIELDS + 8; index++)
		strcat(many, "\tk=v");
	ok(kipp_parse(&message, many) == 0 && message.field_count == KIPP_MAX_FIELDS,
	   "fields past the limit are dropped");

	ok(kipp_parse(&message, "retain") == -1, "no topic rejected");
	ok(kipp_parse(&message, "") == -1, "empty line rejected");

	kipp_parse(&message, "retain\tbattery/BAT0");
	ok(kipp_kind_is(&message, "battery"), "kind matches");
	ok(!kipp_kind_is(&message, "batt"), "kind prefix does not match");
	kipp_parse(&message, "retain\tbattery_extra/1");
	ok(!kipp_kind_is(&message, "battery"), "longer kind does not match");
	kipp_parse(&message, "retain\tbattery");
	ok(kipp_kind_is(&message, "battery"), "bare kind matches");
}

/* ------------------------------------------------------------ building */

static int refused(const char *verb, const char *topic)
{
	struct kipp_record record;

	kipp_begin(&record, verb, topic);
	return kipp_text(&record) == NULL;
}

static void test_build(void)
{
	struct kipp_record record;
	int index;

	kipp_begin(&record, "retain", "battery/BAT0");
	kipp_add(&record, "level", "%d", 80);
	kipp_add(&record, "state", "%s", "charging");
	ok(strcmp(kipp_text(&record), "retain\tbattery/BAT0\tlevel=80\tstate=charging") == 0,
	   "build");

	kipp_begin(&record, "retain", "tray/:1.456");
	ok(strcmp(kipp_text(&record), "retain\ttray/:1.456") == 0, "delete has no payload");

	/* a tab or a newline in a value cannot forge a line */
	kipp_begin(&record, "retain", "title/0x1");
	kipp_add(&record, "text", "%s", "a\tb\nretain\tfocus/X");
	ok(strcmp(kipp_text(&record), "retain\ttitle/0x1\ttext=abretainfocus/X") == 0,
	   "sanitize");

	/* over the limit refuses instead of truncating */
	kipp_begin(&record, "retain", "big/1");
	for (index = 0; index < 200; index++)
		kipp_add(&record, "key", "%s", "0123456789");
	ok(kipp_text(&record) == NULL, "over limit");

	kipp_begin(&record, "retain", "big/1");
	kipp_add(&record, "key", "%2000d", 1);
	ok(kipp_text(&record) == NULL, "long value refused");

	ok(refused("retain", "Tag/1"), "uppercase kind");
	ok(refused("retain", "42/x"), "number kind");
	ok(refused("retain", "/x"), "empty kind");
	ok(refused("retain", "tag/*"), "wildcard in topic");
	ok(refused("retain", "tag/a\tb"), "tab in topic");
	ok(refused("Retain", "tag/1"), "uppercase verb");
	ok(!refused("retain", "tag/eDP-1/2"), "topic segments hold any other byte");

	kipp_begin(&record, "retain", "tag/1");
	kipp_add(&record, "Level", "%d", 1);
	ok(kipp_text(&record) == NULL, "uppercase key");
}

/* -------------------------------------------------------------- client */

static int listen_on(const char *path)
{
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);

	strcpy(address.sun_path, path);
	unlink(path);
	if (bind(descriptor, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    listen(descriptor, 1) < 0) {
		close(descriptor);
		return -1;
	}
	return descriptor;
}

static int broker_reads(FILE *broker, const char *want)
{
	char line[KIPP_MAX_LINE + 1];

	if (!fgets(line, sizeof(line), broker))
		return 0;
	line[strcspn(line, "\n")] = 0;
	return strcmp(line, want) == 0;
}

static int receive_one(struct kipp_client *client, struct kipp_message *message)
{
	struct pollfd watch = { kipp_descriptor(client), POLLIN, 0 };
	int result;

	while ((result = kipp_receive(client, message)) == 0)
		if (poll(&watch, 1, 1000) <= 0)
			return 0;
	return result;
}

static void test_client(void)
{
	struct kipp_client *client;
	struct kipp_message message;
	struct kipp_record  record;
	char  overlong[2000];
	int   listener, broker_descriptor;
	FILE *broker;

	ok(kipp_open(SOCKET_PATH, "Bar") == NULL, "name follows the kind rule");

	listener = listen_on(SOCKET_PATH);
	ok(listener >= 0, "listen");
	if (listener < 0)
		return;
	client = kipp_open(SOCKET_PATH, "bar");
	ok(client != NULL, "open");
	if (!client) {
		close(listener);
		return;
	}
	broker_descriptor = accept(listener, NULL, NULL);
	broker = fdopen(broker_descriptor, "r");

	ok(broker_reads(broker, "connect\tbar"), "connect comes first");

	kipp_begin(&record, "retain", "status/bar");
	kipp_add(&record, "state", "offline");
	kipp_will(client, kipp_text(&record));
	ok(broker_reads(broker, "will\tretain\tstatus/bar\tstate=offline"), "will");

	kipp_subscribe(client, "battery/*");
	ok(broker_reads(broker, "subscribe\tbattery/*"), "subscribe");
	kipp_unsubscribe(client, "battery/*");
	ok(broker_reads(broker, "unsubscribe\tbattery/*"), "unsubscribe");
	ok(kipp_subscribe(client, "a\tb") == -1, "tab in pattern refused");
	kipp_begin(&record, "retain", "Bad/1");
	ok(kipp_will(client, kipp_text(&record)) == -1, "refused record is never sent");

	memset(overlong, 'x', sizeof(overlong) - 1);
	overlong[sizeof(overlong) - 1] = '\n';
	dprintf(broker_descriptor, "version\t2\tbroker=test\n"
	        "retain\tbattery/BAT0\tlevel=80\n");
	write(broker_descriptor, overlong, sizeof(overlong));
	dprintf(broker_descriptor, "sync\tbattery/*\n");

	ok(receive_one(client, &message) == 1 && strcmp(message.verb, "version") == 0 &&
	   strcmp(message.topic, "2") == 0, "greeting");
	ok(receive_one(client, &message) == 1 && strcmp(message.verb, "retain") == 0 &&
	   strcmp(kipp_value(&message, "level"), "80") == 0, "retained value");
	ok(receive_one(client, &message) == 1 && strcmp(message.verb, "sync") == 0 &&
	   strcmp(message.topic, "battery/*") == 0, "overlong skipped, sync names the pattern");

	fclose(broker);
	ok(receive_one(client, &message) == -1, "client sees EOF");

	kipp_close(client);
	close(listener);
	unlink(SOCKET_PATH);
}

int main(void)
{
	test_parse();
	test_build();
	test_client();
	printf(failures ? "%d failed\n" : "all passed\n", failures);
	return failures != 0;
}
