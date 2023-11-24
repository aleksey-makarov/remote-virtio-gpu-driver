#include <asm-generic/bug.h>

#include "mtrace.h"
#include "stream_gen.h"

/* https://en.wikipedia.org/wiki/Xorshift */
static uint32_t xorshift32(uint32_t *state)
{
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *state = x;
}

void stream_gen_init(struct stream_gen *gen, uint32_t seed)
{
	BUG_ON(seed == 0);

	gen->state = seed;
	gen->position = 0;
}

void stream_gen(struct stream_gen *st, void *vp, unsigned int len)
{
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

int stream_gen_test(struct stream_gen *st, void *vp, unsigned int len)
{
	unsigned char *p = vp;

	while (len) {

		if (st->position % 4 == 0)
			st->last = xorshift32(&st->state);

		if (*p != st->last_bytes[st->position % 4]) {
			MTRACE("* @%lu: 0x%02x should be 0x%02x", st->position, (unsigned int)*p, (unsigned int)st->last_bytes[st->position % 4]);
			return -1;
		}

		st->position++;
		p++;
		len--;
	}

	return 0;
}
