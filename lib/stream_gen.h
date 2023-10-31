#ifndef __stream_gen_h__
#define __stream_gen_h__

#include <stdint.h>

struct stream_gen;

struct stream_gen *stream_gen_init(uint32_t seed);
void stream_gen_done(struct stream_gen *sg);

int stream_gen_get(struct stream_gen *sg, void *p, unsigned int len);
int stream_gen_test(struct stream_gen *sg, void *p, unsigned int len);

#endif
