/* CSE 536: User-Level Threading Library */
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/ulthread.h"

/* Standard definitions */
#include <stdbool.h>
#include <stddef.h> 

#include<ulthread.h>

typedef struct ulthread_proc ulthread_proc;
struct ulthread_proc
{
    uint64 sp;
    uint64 s0_s11[12];
    uint64 f8_f9[2];
    uint64 f18_f27[10];
    uint64 ra;
    ulthread_state state;
};

typedef struct ulthreading_manager ulthreading_manager;
struct ilthreading_manager
{
    ulthread_scheduling_algorithm sch_algo;

    // context of the scheduler thread
    ulthread_proc sch_thread;

    // context of user level threads created and managed by sch_thread
    ulthread_proc** ulthreads;
    uint64 ulthreads_count;
};

/* Get thread ID */
int get_current_tid(void) {
    return 0;
}

/* Thread initialization */
void ulthread_init(int schedalgo) {}

/* Thread creation */
bool ulthread_create(uint64 start, uint64 stack, uint64 args[], int priority) {
    /* Please add thread-id instead of '0' here. */
    printf("[*] ultcreate(tid: %d, ra: %p, sp: %p)\n", 0, start, stack);

    return false;
}

/* Thread scheduler */
void ulthread_schedule(void) {
    
    /* Add this statement to denote which thread-id is being scheduled next */
    printf("[*] ultschedule (next tid: %d)\n", 0);

    // Switch between thread contexts
    ulthread_context_switch(NULL, NULL);
}

/* Yield CPU time to some other thread. */
void ulthread_yield(void) {

    /* Please add thread-id instead of '0' here. */
    printf("[*] ultyield(tid: %d)\n", 0);
}

/* Destroy thread */
void ulthread_destroy(void) {}
