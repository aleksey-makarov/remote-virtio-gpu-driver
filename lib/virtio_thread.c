#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#include "error.h"

static pthread_t thread;
static volatile bool done = false;

static void *virtio_thread(void *ptr)
{
	(void)ptr;

	trace("enter");
	while(!done) {
		trace("");
		sleep(3);
	}
	trace("exit");

	return NULL;
}

int virtio_thread_start(void)
{
	trace("");
	int err = pthread_create(&thread, NULL, virtio_thread, NULL);
	if (err) {
		error("pthread_create(): %s", strerror(err));
		return -1;
	}
	return 0;
}

void virtio_thread_stop(void)
{
	trace("");
	done = true;
	pthread_join(thread, NULL);
}
