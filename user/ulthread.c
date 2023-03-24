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

#include "kernel/riscv.h"

typedef struct ulthread_proc ulthread_proc;
struct ulthread_proc
{
    uint64 sp;
    uint64 s0_s11[12];
    uint64 f8_f9[2];
    uint64 f18_f27[10];
    uint64 ra;
    uint64 a0;

    int priority;

    ulthread_state state;
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
};

ulthreading_manager ulmgr;

void ulthread_context_switch(ulthread_proc* store, ulthread_proc* restore);

static uint64 get_sp(void) {
    asm("\
        mov a0, sp;\
        ret;\
    ");
}

/* Get thread ID */
/* the thread id is stored on to the stack as the first value */
int get_current_tid(void) {
    uint64 this_sp = get_sp();
    return *((uint64*)(PGGROUNDUP(this_sp) - 8));
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
    void* new_mem = malloc(new_size);
    if(new_mem == NULL)
        return NULL;
    uint64 copy_size = (old_size < new_size) ? old_size : new_size;
    memmove(new_mem, old_mem, copy_size);
    free(old_mem);
    return new_mem;
}

/* Thread creation */
bool ulthread_create(uint64 start, uint64 stack, uint64 args[], int priority) {
    /* Please add thread-id instead of '0' here. */
    
    // get a new thread id for the new thread
    int new_thread_id = ulmgr.ulthreads_count;

    // expand ulmgr.ulthreads
    void* new_ulmgr_ulthreads = realloc(ulmgr.ulthreads, ulmgr.ulthreads_count * sizeof(ulthread_proc), (ulmgr.ulthreads_count + 1) * sizeof(ulthread_proc));
    if(new_ulmgr_ulthreads == NULL)
        return false;
    ulmgr.ulthreads = new_ulmgr_ulthreads;
    ulmgr.ulthreads_count += 1;

    ulmgr.ulthreads[new_thread_id].priority = priority;
    ulmgr.ulthreads[new_thread_id].state = RUNNABLE;

    printf("[*] ultcreate(tid: %d, ra: %p, sp: %p)\n", new_thread_id, start, stack);

    ulmgr.ulthreads[new_thread_id].sp = stack + PGSIZE;
    ulmgr.ulthreads[new_thread_id].ra = start;
    ulmgr.ulthreads[new_thread_id].a0 = args;
    ulmgr.ulthreads[new_thread_id].sp -= 8;
    *((uint64*)(ulmgr.ulthreads[new_thread_id].sp)) = new_thread_id;

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
