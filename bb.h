#ifndef BB_H
#define BB_H

#include "user.h"

struct bounded_buffer {
	int queue[USER_LIMIT];	/* value is ctrl_block->ID */
	int head, tail;
	int count;
};

void bb_init(struct bounded_buffer * bb);

int bb_push(struct bounded_buffer * bb, const int p);

int bb_pop(struct bounded_buffer * bb);
int bb_top(struct bounded_buffer * bb);

int bb_size(struct bounded_buffer * bb);

int bb_ready(struct bounded_buffer * bb, const struct timespec * clock, const struct user * users);

#endif
