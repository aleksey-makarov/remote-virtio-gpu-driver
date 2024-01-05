/*******************************************************************************
 * Copyright (c) 2022 OpenSynergy GmbH.
 *
 * This software may not be used in any way or distributed without
 * permission. All rights reserved.
 ******************************************************************************/

#ifndef __es2gears_h__
#define __es2gears_h__

#ifdef __cplusplus
extern "C" {
#endif

struct es2gears_state;

void es2gears_idle(struct es2gears_state * state, unsigned long int elapsed_time_ms);
void es2gears_reshape(struct es2gears_state * state, int width, int height);
void es2gears_draw(struct es2gears_state *state);
enum special { SPECIAL_LEFT, SPECIAL_RIGHT, SPECIAL_UP, SPECIAL_DOWN, };
void es2gears_special(struct es2gears_state * state, enum special special);
struct es2gears_state *es2gears_init(void);
void es2gears_done(struct es2gears_state *state);

#ifdef __cplusplus
}
#endif

#endif
