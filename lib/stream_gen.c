#include "stream_gen.h"

#include <stddef.h>

struct stream_gen {
};

struct stream_gen *stream_gen_init(uint32_t seed)
{
	(void)seed;
	return NULL;
}

void stream_gen_done(struct stream_gen *sg)
{
	(void)sg;
}

int stream_gen_get(struct stream_gen *sg, void *p, unsigned int len)
{
	(void)sg;
	(void)p;
	(void)len;
	return 0;
}

int stream_gen_test(struct stream_gen *sg, void *p, unsigned int len)
{
	(void)sg;
	(void)p;
	(void)len;
	return 0;
}
