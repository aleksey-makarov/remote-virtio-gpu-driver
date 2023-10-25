#ifndef __epoll_scheduler_h__
#define __epoll_scheduler_h__

#define ES_DONE  (0)
#define ES_EXIT  (1)
#define ES_READY (2)
#define ES_WAIT  (3)

#include <stdint.h>
#include <stdbool.h>
#include <sys/epoll.h>

struct es_thread {

	const char *name;
	void *ctxt;
	int fd;
	uint32_t events;

	/*
	 * `test` should be non-null
	 * Returns:
	 * - ES_DONE to exit all threads and quit `es_schedule()`
	 * - ES_EXIT to gracefully exit this thread
	 * - ES_READY ready to `go()`
	 *     don't wait on the `fd` for this thread, mark it as ready
	 * - ES_WAIT wait for the events specified in the `events` field then `go()`
	 *     setting `events` to 0 would exclude this thread from running
	 *     in the current iteration
	 * - any negative vaule to signal error
	 */
	int (*test)(void *ctxt);

	/*
	 * Returns
	 *     - any non-negative value to proceed
	 *     - any negative vaule to signal error
	 */
	int (*go)(uint32_t events, void *ctxt);

	/*
	 * Called if any of the threads signalls error
	 * or if this thread exits.
	 */
	void (*done)(void *ctxt);

	/*
	 * Private to epoll_scheduler, don't touch
	 */
	struct epoll_event private;
	bool ready;
};

int es_add(struct es_thread *thread);
int es_schedule(void);

#endif
