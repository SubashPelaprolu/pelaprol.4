#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
//#include <errno.h>
#include <sys/ipc.h>
#include <sys/wait.h>

#include "common.h"
#include "user.h"
#include "bb.h"

// feedback queues /levels
#define FB_QUEUES 4

static 			 unsigned int num_users = 0;
static const unsigned int max_users = 100;

static int ext_procs = 0;  //ext_procs users

enum stat_times {IDLE_TIME, TURN_TIME, WAIT_TIME, SLEEP_TIME};
static struct timespec sim_times[4];

static OSS *ossaddr = NULL;
static const char * logfile = "output.txt";

static int sig_flag = 0;	//raise, if we were signalled
static unsigned int user_bitmap = 0;

//Queues for blocked and ready processes
static struct bounded_buffer io_bb;          	 // blocked
static struct bounded_buffer fb_bb[FB_QUEUES]; // ready
static unsigned int quants[FB_QUEUES];

//check which user entry is free, using bitmap
static int get_free_ui(){
	int i;
  for(i=0; i < USER_LIMIT; i++){
  	if(((user_bitmap & (1 << i)) >> i) == 0){	//if bit is 0, than means entry is free
			user_bitmap ^= (1 << i);	//set bit to 1, to mark it used
      return i;
    }
  }
  return -1;
}

//Clear a user entry
void ui_clear(struct user * users, const unsigned int i){
  user_bitmap ^= (1 << i); //set bit to 0, to mark it free
  memset(&users[i], 0, sizeof(struct user));
}

struct user * ui_new(struct user * users, const int ID){
	const int i = get_free_ui();
	if(i == -1){
		return NULL;
	}

  users[i].ID	= ID;
	//70% percent change, for a process class to be normal(0). Realtime is 1
  users[i].class = ((rand() % 100) > 70) ? 0 : 1;
  users[i].state = USER_READY;
	return &users[i];
}

static int user_fork(void){

  struct user *pe;
  if((pe = ui_new(ossaddr->users, num_users)) == NULL)
    return 0;

	const int ui = pe - ossaddr->users; //process index

	const pid_t pid = fork();
	switch(pid){
		case -1:
			ui_clear(ossaddr->users, ui);
			perror("fork");
			break;

		case 0:

			execl("./user", "./user", NULL);
			perror("execl");
			exit(EXIT_FAILURE);

		default:
			num_users++;
			pe->pid = pid;
	    pe->times[TIMER_STARTED] = ossaddr->clock;
	    pe->times[TIMER_READY] 	 = ossaddr->clock;
			break;
  }

  if(bb_push(&fb_bb[0], ui) < 0){
    fprintf(stderr, "[%li.%li] Error: Queueing process with PID %d failed\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->pid);
		return -1;
  }else{
    printf("[%li.%li] OSS: Generating process with PID %u in queue 0\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID);
		return 0;
  }
}

static void fb_leveling(struct user * pe, int q){

	struct timespec res;
  const int ui = bb_pop(&fb_bb[q]);

  if(pe->state == USER_EXITED){

    //sim_times[TURN_TIME] time = system time / num processes
    timeradd(&sim_times[TURN_TIME], &pe->times[TIMER_SYSTEM]);

    /* wait time = total_system time - total cpu time */
    timersub(&res, &pe->times[TIMER_SYSTEM], &pe->times[TIMER_CPU]);
    timeradd(&sim_times[WAIT_TIME], &res);

    printf("[%li.%li] OSS: Process with PID %u has terminated, cleared from queue %d\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID, q);
    ui_clear(ossaddr->users, ui);
  }else if(pe->state == USER_BLOCKED){

		printf("[%li.%li] OSS: Putting process with PID %u in blocked queue\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID);
    bb_push(&io_bb, ui);
  }else{

    if(pe->times[TIMER_BURST].tv_nsec == quants[q]){  //if entire quantum was used
      if(q < (FB_QUEUES - 1)){  //if next level
        q++;  //up to next queue level
      }
    }else{
      printf("[%li.%li] OSS: not using its entire time quantum\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec);
    }
    pe->times[TIMER_READY] = ossaddr->clock;

    printf("[%li.%li] OSS: Process with PID %u queued in level %d\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID, q);
		bb_push(&fb_bb[q], ui);
  }
}

static int fb_dispatch(const int q){

  const int ui = bb_top(&fb_bb[q]);
  struct user * pe = &ossaddr->users[ui];

  printf("[%li.%li] OSS: Dispatching process with PID %u from queue %i\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID, q);

  /* send message to process */
  struct msgbuf mb;
  mb.mtype = pe->pid;
  mb.val = quants[q];

  if( (msg_snd(&mb) == -1) || (msg_rcv(&mb) == -1) ){
    return -1;
  }

  pe->times[TIMER_BURST] = mb.burst;
  pe->state = mb.val;

	if(pe->state == USER_READY){
    printf("[%li.%li] OSS: Receiving that process with PID %u ran for %li nanoseconds\n",
      ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID, pe->times[TIMER_BURST].tv_nsec);

    timeradd(&pe->times[TIMER_CPU], &pe->times[TIMER_BURST]);
    timeradd(&ossaddr->clock, &pe->times[TIMER_BURST]);  //advance clock with burst time

	}else if(pe->state == USER_BLOCKED){

    printf("[%li.%li] OSS: Process with PID %u blocked until %li.%li\n",
        ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID, pe->times[TIMER_IOBLOCK].tv_sec, pe->times[TIMER_IOBLOCK].tv_nsec);
    /* add burst and current timer to make blocked timestamp */
		timeradd(&pe->times[TIMER_IOBLOCK], &ossaddr->clock);
  	timeradd(&pe->times[TIMER_IOBLOCK], &pe->times[TIMER_BURST]);

	}else if(pe->state == USER_EXITED){
    ext_procs++;
    timeradd(&pe->times[TIMER_CPU], &pe->times[TIMER_BURST]);
    timersub(&pe->times[TIMER_SYSTEM], &ossaddr->clock, &pe->times[TIMER_STARTED]);
    printf("[%li.%li] OSS: Process with PID %u ext_procs\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID);
  }
  fb_leveling(pe, q);

  //calculate dispatch time
  struct timespec temp;
  temp.tv_sec = 0;
  temp.tv_nsec = rand() % 100;
  printf("[%li.%li] OSS: Time this dispatch took %li nanoseconds\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, temp.tv_nsec);
  timeradd(&ossaddr->clock, &temp);

  return 0;
}

//Find which queue has a ready process
static int fb_ready(const struct user * users){
  int i;
  for(i=0; i < FB_QUEUES; i++){
    const int ui = bb_top(&fb_bb[i]);
    if(users[ui].state == USER_READY){
      return i;
    }
  }
  return -1;
}

static int io_ready(const struct user * users){

  const int ui = bb_ready(&io_bb, &ossaddr->clock, users);
  if(ui == -1){
    return -1;
  }
  struct user * pe = &ossaddr->users[ui];

  // update total sleep time
  timeradd(&sim_times[SLEEP_TIME], &pe->times[TIMER_BURST]);  // burst holds how long user was BLOCKED

  // udpate process state and timers
  pe->state = USER_READY;
	timerzero(&pe->times[TIMER_IOBLOCK]);
	timerzero(&pe->times[TIMER_BURST]);
  pe->times[TIMER_READY] = ossaddr->clock;

  if(bb_push(&fb_bb[0], ui) < 0){
    fprintf(stderr, "[%li.%li] Error: Queueing process with PID %d failed\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->pid);
		return -1;
  }else{
    printf("[%li.%li] OSS: Unblocked process with PID %d to queue 0\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, pe->ID);
		return 0;
  }
}

static int fork_ready(struct timespec *forkat){

  if(num_users < max_users){  //if we can fork more

    // if its time to fork
    if(timercmp(&ossaddr->clock, forkat)){

      //next fork time
      forkat->tv_sec = ossaddr->clock.tv_sec + 1;
      forkat->tv_nsec = 0;

      return 1;
    }
  }
  return 0; //not time to fokk
}

//Stop users. Used only in case of an alarm.
static void stop_users(){
  int i;
  struct msgbuf mb;
	memset(&mb, 0, sizeof(mb));

	mb.val = -1;

  for(i=0; i < USER_LIMIT; i++){
    if(ossaddr->users[i].pid > 0){
      mb.mtype = ossaddr->users[i].pid;
      msg_snd(&mb);
    }
  }
}

static void signal_handler(int sig){
  printf("[%li.%li] OSS: Signal %d\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec, sig);
  sig_flag = sig;
}

static void fb_init(){
	int i;
	for(i=0; i < FB_QUEUES; i++){
		bb_init(&fb_bb[i]);
		quants[i] = (i*2)*QUANTUM;
	}
	quants[0] = QUANTUM;
}

int main(void){

  signal(SIGINT,  signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGALRM, signal_handler);
  signal(SIGCHLD, SIG_IGN);

  ossaddr = oss_init(1);
  if(ossaddr == NULL){
    return -1;
  }

	memset(sim_times, 0, sizeof(sim_times));
  bb_init(&io_bb);
	fb_init();

  stdout = freopen(logfile, "w", stdout);
  if(stdout == NULL){
		perror("freopen");
		return -1;
	}

	alarm(3);

  int was_idle = 0;
  struct timespec was_idle_fom;
	struct timespec inc;		//clock increment
  struct timespec forkat;	//when to fork another process

	const unsigned int ns_step = 10000;

	inc.tv_sec = 1;
	timerzero(&forkat);

  while(!sig_flag){

		if(ext_procs >= max_users)
			break;

	  //increment system clock
	  inc.tv_nsec = rand() % ns_step;
	  timeradd(&ossaddr->clock, &inc);

		//if we are ready to fork, start a process
    if(fork_ready(&forkat) && (user_fork() < 0))
      break;

		//if a process is ready with IO, return it to schedule queue
    io_ready(ossaddr->users);

		//if a process is ready to run, dispatch it
    const int q = fb_ready(ossaddr->users);
    if(q >= 0){

      if(was_idle){	//if simulation was idle

        struct timespec temp;
        timersub(&temp, &ossaddr->clock, &was_idle_fom);
        timeradd(&sim_times[IDLE_TIME], &temp);

				timerzero(&was_idle_fom);
        was_idle = 0;
      }

      if(fb_dispatch(q) < 0)
        break;

			//TODO: do starvation checks

    }else{

      if(was_idle == 0){
        printf("[%li.%li] OSS: No process to dispatch\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec);
        was_idle_fom = ossaddr->clock;
        was_idle = 1;
      }

      if(bb_size(&io_bb) > 0){
				const int ui = bb_top(&io_bb);
        struct user * pe = &ossaddr->users[ui];

				ossaddr->clock = pe->times[TIMER_IOBLOCK];
        printf("[%li.%li] OSS: Advanced time\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec);
      }
    }
  }

	timerdiv(&sim_times[TURN_TIME], num_users);
  timerdiv(&sim_times[WAIT_TIME], num_users);
  timerdiv(&sim_times[SLEEP_TIME], num_users);

  printf("Quantum levels : ");
	int i;
	for(i=0; i < FB_QUEUES; i++)
		printf("%d ", quants[i]);
	printf("\n");
  printf("Runtime: %li:%li\n", ossaddr->clock.tv_sec, ossaddr->clock.tv_nsec);
	printf("Idletime: %li:%li\n", sim_times[IDLE_TIME].tv_sec, sim_times[IDLE_TIME].tv_nsec);
	printf("Average times\n");
  printf("\tTurnaround : %li:%li\n", sim_times[TURN_TIME].tv_sec, sim_times[TURN_TIME].tv_nsec);
  printf("\tWait: %li:%li\n", sim_times[WAIT_TIME].tv_sec, sim_times[WAIT_TIME].tv_nsec);
  printf("\tSleep : %li:%li\n", sim_times[SLEEP_TIME].tv_sec, sim_times[SLEEP_TIME].tv_nsec);

  stop_users(-1);
  oss_deinit(1);
  return 0;
}
