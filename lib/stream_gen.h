#ifndef __stream_gen_h__
#define __stream_gen_h__

#include <stdint.h>

struct stream_gen {
	uint32_t state;
	union {
		uint32_t last;
		unsigned char last_bytes[4];
	};
	unsigned long position;
};

void stream_gen_init(struct stream_gen *gen, uint32_t seed);

void stream_gen(struct stream_gen *sg, void *p, unsigned int len);
int stream_gen_test(struct stream_gen *sg, void *p, unsigned int len);

#endif
