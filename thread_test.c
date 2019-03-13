//
// Created by Ice on 2019-03-11.
//

#include "types.h"
#include "user.h"
#include "mmu.h"
#include "x86.h"

#define MAX_ANDERSON_QUEUE_LEN 50
#define ANDERSON_LOCK_MUST_WAIT 0
#define ANDERSON_LOCK_AVAILABLE 1

typedef struct {
    uint locked;
} lock_t;

typedef struct {
    uchar queue[MAX_ANDERSON_QUEUE_LEN];
    uint holding_pos;
    uint queueing_pos;
} anderson_lock_t;

typedef struct {
    int counter;
    anderson_lock_t lock;
} seqlock_t;

void thread_create(void *(*start_routine)(void*), void *arg) {
    char* stack = malloc(PGSIZE);
    void* exit_ptr = exit;
    if (clone(stack, PGSIZE) == 0) {
        asm volatile ("push %0;\n" : : "r" (arg));
        asm volatile ("push %0;\n" : : "r" (exit_ptr));
        asm volatile ("call %0;\n" : : "r" (start_routine));
        asm volatile ("pop %eax;\n");
    }
}

void lock_acquire(lock_t* lock) {
    if (lock->locked)
        return;
    while(xchg(&lock->locked, 1) != 0);
    __sync_synchronize();
}

void lock_release(lock_t* lock) {
    lock->locked = 0;
}

//lock should be allocated before invoking
void lock_init(lock_t* lock) {
    lock->locked = 0;
}

void anderson_lock_acquire(anderson_lock_t* lock) {
    uint my_pos = fetch_and_add(&(lock->queueing_pos), 1);
    while (lock->queue[my_pos % MAX_ANDERSON_QUEUE_LEN] == ANDERSON_LOCK_MUST_WAIT);
    lock->holding_pos = my_pos;
}

void anderson_lock_release(anderson_lock_t* lock) {
    lock->queue[lock->holding_pos % MAX_ANDERSON_QUEUE_LEN] = ANDERSON_LOCK_MUST_WAIT;
    lock->queue[(lock->holding_pos + 1) % MAX_ANDERSON_QUEUE_LEN] = ANDERSON_LOCK_AVAILABLE;
}

//lock should be allocated before invoking
void anderson_lock_init_and_acquire(anderson_lock_t* lock) {
    for (int i = 0; i < MAX_ANDERSON_QUEUE_LEN; i++)
        lock->queue[i] = ANDERSON_LOCK_MUST_WAIT;
    lock->holding_pos = 0;
    lock->queueing_pos = 0;
}

void seqlock_write_acquire(seqlock_t* lock) {
    lock->counter++;
    anderson_lock_acquire(&(lock->lock));
}

int seqlock_read_check(seqlock_t* lock) {
    return lock->counter;
}

void seqlock_write_release(seqlock_t* lock) {
    anderson_lock_release(&(lock->lock));
    lock->counter++;
}

//lock should be allocated before invoking
void seqlock_init(seqlock_t* lock) {
    lock->counter = 0;
}

lock_t frisbee;
int cur_passes;
int passes;
int threads;

void* sub_func(void* id) {
    while (cur_passes < passes) {
        lock_acquire(&frisbee);
        if (cur_passes % threads == (int)id)
            printf(2, "Pass number no: %d, Thread %d is passing the token to thread %d\n", cur_passes++, id, id + 1);
        lock_release(&frisbee);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        printf(2, "usage %s threads passes\n", argv[0]);
        exit();
    }
    threads = atoi(argv[1]);
    passes = atoi(argv[2]);
    cur_passes = 0;
    lock_init(&frisbee);
    for (int i = 0; i < threads; i++)
        thread_create(sub_func, (void*)i);
    exit();
}