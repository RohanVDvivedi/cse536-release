/* CSE 536: User-Level Threading Library */
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/ulthread.h"

/* Standard definitions */
#include <stdbool.h>
#include <stddef.h> 

#include "ulthread.h"

#include "kernel/riscv.h"

typedef struct ulthread_proc ulthread_proc;
struct ulthread_proc
{
    uint64 ra;
    uint64 sp;
    uint64 s0_s11[12];

    // set it to argument of the function
    uint64 a0;

    int priority;

    ulthread_state state;

    int tid;

    uint64 stack_at;
};

typedef struct ulthreading_manager ulthreading_manager;
struct ulthreading_manager
{
    ulthread_scheduling_algorithm sch_algo;

    // context of the scheduler thread
    ulthread_proc sch_thread;

    // context of user level threads created and managed by sch_thread
    ulthread_proc* ulthreads;
    uint64 ulthreads_count;

    // tid of the thread currently running or the one that ran last
    int tid_running;
};

ulthreading_manager ulmgr;

void ulthread_context_switch(ulthread_proc* store, ulthread_proc* restore);

static uint64 get_sp(void) {
    asm("\
        mv a0, sp;\
        ret;\
    ");
}

/* Get thread ID */
/* the thread id is stored on to the stack as the first value */
int get_current_tid(void) {
    uint64 this_sp = get_sp();

    for(int i = 0; i < ulmgr.ulthreads_count; i++)
        if(ulmgr.ulthreads[i].stack_at <= this_sp && this_sp < ulmgr.ulthreads[i].stack_at + PGSIZE)
            return i;

    return -1;
}

/* Thread initialization */
void ulthread_init(int schedalgo) {
    ulmgr.sch_algo = schedalgo;
    ulmgr.sch_thread = (ulthread_proc){};
    ulmgr.ulthreads = NULL;
    ulmgr.ulthreads_count = 0;
}

static void* realloc(void* old_mem, uint64 old_size, uint64 new_size)
{
    if(old_size == new_size)
        return old_mem;
    void* new_mem = malloc(new_size);
    if(new_mem == NULL)
        return NULL;
    uint64 copy_size = (old_size < new_size) ? old_size : new_size;
    memmove(new_mem, old_mem, copy_size);
    if(old_mem != NULL && old_size > 0)
        free(old_mem);
    return new_mem;
}

static int assign_or_allocate_new_ulthreads_proc()
{
    int new_thread_id = 0;
    for(; new_thread_id < ulmgr.ulthreads_count && ulmgr.ulthreads[new_thread_id].state != FREE; new_thread_id++);
    if(new_thread_id < ulmgr.ulthreads_count)
        return new_thread_id;
    
    ulmgr.ulthreads = realloc(ulmgr.ulthreads, ulmgr.ulthreads_count * sizeof(ulthread_proc), (ulmgr.ulthreads_count + 1) * sizeof(ulthread_proc));
    ulmgr.ulthreads_count += 1;

    return new_thread_id;
}

/* Thread creation */
bool ulthread_create(uint64 start, uint64 stack, uint64 args[], int priority) {
    /* Please add thread-id instead of '0' here. */
    
    // get a new thread id for the new thread
    int new_thread_id = assign_or_allocate_new_ulthreads_proc();

    ulmgr.ulthreads[new_thread_id].priority = priority;
    ulmgr.ulthreads[new_thread_id].state = RUNNABLE;
    ulmgr.ulthreads[new_thread_id].tid = new_thread_id;
    ulmgr.ulthreads[new_thread_id].stack_at = stack;

    printf("[*] ultcreate(tid: %d, ra: %p, sp: %p)\n", new_thread_id, start, stack);

    ulmgr.ulthreads[new_thread_id].sp = stack + PGSIZE;
    ulmgr.ulthreads[new_thread_id].ra = start;
    ulmgr.ulthreads[new_thread_id].a0 = ((uint64)args);

    return false;
}

/*
    get_next_thread_to_run_* () will return the tid of the thread that should be run next
    else it will return -1 (an indication to quit the loop)
*/

static int get_next_thread_to_run_ROUNDROBIN()
{
    int next_tid = -1;

    for(int i = 0; i < ulmgr.ulthreads_count; i++)
    {
        int tid = (ulmgr.tid_running + 1 + i) % ulmgr.ulthreads_count;
        if(ulmgr.ulthreads[tid].state == RUNNABLE)
        {
            next_tid = tid;
            break;
        }
    }

    if(next_tid != -1)
        return next_tid;
    
    // all threads are either YIELD or FREE
    // we make all the YIELDED threads RUNNABLE now
    for(int i = 0; i < ulmgr.ulthreads_count; i++)
        if(ulmgr.ulthreads[i].state == YIELD)
            ulmgr.ulthreads[i].state = RUNNABLE;
    
    for(int i = 0; i < ulmgr.ulthreads_count; i++)
    {
        int tid = (ulmgr.tid_running + 1 + i) % ulmgr.ulthreads_count;
        if(ulmgr.ulthreads[tid].state == RUNNABLE)
        {
            next_tid = tid;
            break;
        }
    }

    return next_tid;
}

static int get_next_thread_to_run_PRIORITY()
{
    return -1;
}

static int get_next_thread_to_run_FCFS()
{
    return -1;
}

/* Thread scheduler */
void ulthread_schedule(void) {

while(1) {
    int next_tid = -1;

    switch(ulmgr.sch_algo)
    {
        case ROUNDROBIN :
        {
            next_tid = get_next_thread_to_run_ROUNDROBIN();
            break;
        }
        case PRIORITY :
        {
            next_tid = get_next_thread_to_run_PRIORITY();
            break;
        }
        case FCFS :
        {
            next_tid = get_next_thread_to_run_FCFS();
            break;
        }
    }

    if(next_tid == -1)
        break;
    
    /* Add this statement to denote which thread-id is being scheduled next */
    printf("[*] ultschedule (next tid: %d)\n", next_tid);

    // set the value to the next thread that will be run
    ulmgr.tid_running = next_tid;

    // Switch between thread contexts
    ulthread_context_switch(&(ulmgr.sch_thread), ulmgr.ulthreads + next_tid);
}

}

/* Yield CPU time to some other thread. */
void ulthread_yield(void) {

    int tid = get_current_tid();

    ulmgr.ulthreads[tid].state = YIELD;

    /* Please add thread-id instead of '0' here. */
    printf("[*] ultyield(tid: %d)\n", tid);

    ulthread_context_switch(ulmgr.ulthreads + tid, &(ulmgr.sch_thread));
}

/* Destroy thread */
void ulthread_destroy(void) {
    int tid = get_current_tid();

    ulmgr.ulthreads[tid].state = FREE;

    ulthread_context_switch(ulmgr.ulthreads + tid, &(ulmgr.sch_thread));
}
