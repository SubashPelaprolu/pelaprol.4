#ifndef USER_H
#define USER_H

#include "timer.h"

#define USER_LIMIT 20

enum user_state { USER_READY=1, USER_BLOCKED, USER_EXITED };

struct user {
	pid_t	pid;
	int ID;
	int class;

	enum user_state state;
	struct timespec	times[TIMER_COUNT];
};

#endif
