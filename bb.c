#include <string.h>
#include "bb.h"

void bb_init(struct bounded_buffer * bb){

  memset(bb->queue, -1, sizeof(int)*USER_LIMIT);
  bb->head = bb->tail = 0;
  bb->count = 0;
}

int bb_push(struct bounded_buffer * bb, const int p){
  if(bb->count >= USER_LIMIT){
    return -1;
  }

  bb->queue[bb->tail++] = p;
  bb->tail %= USER_LIMIT;
  bb->count++;
  return 0;
}

int bb_pop(struct bounded_buffer * bb){
  const unsigned int pi = bb->queue[bb->head++];
  bb->count--;
  bb->head %= USER_LIMIT;

  return pi;
}

// find user we can unblocked
int bb_ready(struct bounded_buffer * bb, const struct timespec * clock, const struct user * users){
  int i;
  for(i=0; i < bb->count; i++){
    const int pi = bb_pop(bb);
    if(timercmp(clock, &users[pi].times[TIMER_IOBLOCK])){	//if our event time is reached
      return pi;
    }
    bb_push(bb, pi);
  }
  return -1;
}

int bb_top(struct bounded_buffer * bb){
  return bb->queue[bb->head];
}

int bb_size(struct bounded_buffer * bb){
  return bb->count;
}
