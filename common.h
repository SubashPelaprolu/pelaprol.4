#ifndef COMMON_H
#define COMMON_H

#include "user.h"

// quantum ( 10 ms )
#define QUANTUM 10000000

typedef struct {
	pid_t oss_pid;									/* PID of master process */
	struct timespec clock;
	struct user users[USER_LIMIT];
} OSS;

struct msgbuf {
	long mtype;
	int val;
  struct timespec burst;
};

OSS * oss_init(const int flags);
int oss_deinit(const int clear);

int msg_snd(const struct msgbuf *mb);
int msg_rcv(struct msgbuf *mb);

#endif
