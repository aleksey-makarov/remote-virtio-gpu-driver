#include "virtio_thread.h"

#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "error.h"
#include "gettid.h"

static pthread_t thread;
static volatile bool done = false;
static pthread_mutex_t req_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static STAILQ_HEAD(, virtio_thread_request) req_queue = STAILQ_HEAD_INITIALIZER(req_queue);

// called from the main thread
void virtio_thread_request_done(struct virtio_thread_request *req)
{
	int err;

	assert(req);
	trace("(4) %d on %d", req->serial, gettid());

	err = pthread_mutex_lock(&req_queue_mutex);
	if (err) {
		error("pthread_mutex_lock(): %s", strerror(err));
		exit(EXIT_FAILURE);
	}

	STAILQ_INSERT_TAIL(&req_queue, req, queue_entry);

	err = pthread_mutex_unlock(&req_queue_mutex);
	if (err) {
		error("pthread_mutex_lock(): %s", strerror(err));
		exit(EXIT_FAILURE);
	}
}


static void *virtio_thread(void *ptr)
{
	int err;
	struct virtio_thread_request *req;

	(void)ptr;

	trace("virtio_thread: %d", gettid());

	while(!done) {
		sleep(3);

		static unsigned int serial = 0;

		req = malloc(sizeof(*req));
		req->serial = serial++;
		trace("(1) %d new on %d", req->serial, gettid());
		virtio_thread_new_request(req);

		req = malloc(sizeof(*req));
		req->serial = serial++;
		trace("(1) %d new on %d", req->serial, gettid());
		virtio_thread_new_request(req);

		err = pthread_mutex_lock(&req_queue_mutex);
		if (err) {
			// FIXME
			error("pthread_mutex_lock(): %s", strerror(err));
			exit(EXIT_FAILURE);
		}

		while(!STAILQ_EMPTY(&req_queue)) {
			struct virtio_thread_request *req = STAILQ_FIRST(&req_queue);
			STAILQ_REMOVE_HEAD(&req_queue, queue_entry);
			trace("(5) %d free %d", req->serial, gettid());
			free(req);
		}

		err = pthread_mutex_unlock(&req_queue_mutex);
		if (err) {
			// FIXME
			error("pthread_mutex_lock(): %s", strerror(err));
			exit(EXIT_FAILURE);
		}
	}

	assert(STAILQ_EMPTY(&req_queue));

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
