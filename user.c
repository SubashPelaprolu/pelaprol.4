#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "common.h"

int main(void){

	OSS *ossaddr = oss_init(0);
	if(ossaddr == NULL){
		return -1;
	}

	int term = 0;
  while(term == 0){

		struct msgbuf msg;
		if(msg_rcv(&msg) == -1){
			break;
		}

		const int quantum = msg.val;
		if(quantum < 0){
			break;
		}

		msg.mtype = getppid();
		msg.val = USER_READY;

		int action;
		term = ((rand() % 100) < 15);
		if(term){
			msg.val = USER_EXITED;
			action = 2;	//to use part of quantum
		}else{
			action = rand() % 3;
		}

		switch(action){
			case 0:	//use entire quantum
				msg.burst.tv_sec = 0;
				msg.burst.tv_nsec = quantum;
				break;

			case 1:	//block for input/output
				msg.val = USER_BLOCKED;
				msg.burst.tv_sec  = rand() % 5;
				msg.burst.tv_nsec = rand() % 1000;
				break;

			case 2:	//use [1;99] of quantum
				msg.burst.tv_sec = 0;
				msg.burst.tv_nsec = (unsigned int)((float) quantum / (100.0f / (1.0f + (rand() % 99))));
				break;
		}

		if(msg_snd(&msg) == -1){
			break;
		}
  }

	oss_deinit(0);
  return 0;
}
