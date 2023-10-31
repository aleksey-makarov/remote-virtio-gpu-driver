#include "stream_gen.h"

#include "trace.h"

#include <stdlib.h>

struct stream_gen_state {
	uint32_t state;
	union {
		uint32_t last;
		unsigned char last_bytes[4];
	};
	unsigned long position;
};

struct stream_gen {
	struct stream_gen_state gen;
	struct stream_gen_state test;
};

/* https://en.wikipedia.org/wiki/Xorshift */

/* The state must be initialized to non-zero */
static uint32_t xorshift32(uint32_t *state)
{
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *state = x;
}

struct stream_gen *stream_gen_init(uint32_t seed)
{
	(void)seed;
	struct stream_gen *sg = malloc(sizeof(struct stream_gen));
	if (!sg) {
		trace_err("malloc()");
		return 0;
	}

	sg->gen.state = sg->test.state = seed;
	sg->gen.position = sg->test.position = 0;

	return sg;
}

void stream_gen_done(struct stream_gen *sg)
{
	free(sg);
}

void stream_gen(struct stream_gen *sg, void *vp, unsigned int len)
{
	struct stream_gen_state *st = &sg->gen;
	unsigned char *p = vp;

	while (len) {

		if (st->position % 4 == 0)
			st->last = xorshift32(&st->state);

		*p = st->last_bytes[st->position % 4];

		st->position++;
		p++;
		len--;
	}
}

int stream_gen_test(struct stream_gen *sg, void *vp, unsigned int len)
{
	struct stream_gen_state *st = &sg->gen;
	unsigned char *p = vp;

	while (len) {

		if (st->position % 4 == 0)
			st->last = xorshift32(&st->state);

		if (*p != st->last_bytes[st->position % 4]) {
			trace_err("error @%lu: 0x%02x should be 0x%02x", st->position, (unsigned int)*p, (unsigned int)st->last_bytes[st->position % 4]);
			return -1;
		}

		st->position++;
		p++;
		len--;
	}

	return 0;
}
