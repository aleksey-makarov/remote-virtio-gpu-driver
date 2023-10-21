#ifndef __epoll_scheduler_h__
#define __epoll_scheduler_h__

#define ES_EXIT (-1122)

struct es_thread {
	void *ctx;
	int (*init)(void *ctx);
	int (*pre)(void *ctx);
	int (*post)(void *ctx);
	void (*done)(void *ctx);
};

int es_schedule(struct es_thread *threads, unsigned int len);

#endif
