#ifndef __UTHREAD_H__
#define __UTHREAD_H__

#include <stdbool.h>

#define MAXULTHREADS 100

typedef enum ulthread_state ulthread_state;
enum ulthread_state {
  FREE,
  RUNNABLE,
  YIELD,
};

typedef enum ulthread_scheduling_algorithm ulthread_scheduling_algorithm;
enum ulthread_scheduling_algorithm {
  ROUNDROBIN,   
  PRIORITY,     
  FCFS,         // first-come-first serve
};

#endif