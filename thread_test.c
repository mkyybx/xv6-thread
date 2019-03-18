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

void asm_code(void *(*start_routine)(void*), void *arg) {
    asm volatile ("push %0;\n" : : "r" (arg));
    asm volatile ("push %0;\n" : : "r" (exit));
    asm volatile ("call %0;\n" : : "r" (start_routine));
    asm volatile ("pop %eax;\n");
}

void thread_create(void *(*start_routine)(void*), void *arg) {
    char* stack = malloc(PGSIZE);
    *(uint*)(stack + PGSIZE - 4) = (uint)arg;
    *(uint*)(stack + PGSIZE - 8) = (uint)exit;
    *(uint*)(stack + PGSIZE - 12) = (uint)start_routine;
    clone(stack, PGSIZE);
}

void lock_acquire(lock_t* lock) {
    while(xchg(&lock->locked, 1) != 0);
    __sync_synchronize();
}

void lock_release(lock_t* lock) {
    __sync_synchronize();
    asm volatile("movl $0, %0" : "+m" (lock->locked) : );
}

//lock should be allocated before invoking
void lock_init(lock_t* lock) {
    lock->locked = 0;
}

void anderson_lock_acquire(anderson_lock_t* lock) {
    uint my_pos = fetch_and_add(&(lock->queueing_pos), 1);
    my_pos++;
    while (lock->queue[my_pos % MAX_ANDERSON_QUEUE_LEN] == ANDERSON_LOCK_MUST_WAIT) {
       // printf(2, "queue[%d]=%d\n", my_pos % MAX_ANDERSON_QUEUE_LEN, lock->queue[my_pos % MAX_ANDERSON_QUEUE_LEN]);
    }
    lock->holding_pos = my_pos;
}

void anderson_lock_release(anderson_lock_t* lock) {
    lock->queue[lock->holding_pos % MAX_ANDERSON_QUEUE_LEN] = ANDERSON_LOCK_MUST_WAIT;
    lock->queue[(lock->holding_pos + 1) % MAX_ANDERSON_QUEUE_LEN] = ANDERSON_LOCK_AVAILABLE;
    //printf(2, "que[%d]=%d\n", (lock->holding_pos + 1) % MAX_ANDERSON_QUEUE_LEN, lock->queue[(lock->holding_pos + 1) % MAX_ANDERSON_QUEUE_LEN]);
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
    anderson_lock_init_and_acquire(&(lock->lock));
    anderson_lock_release(&(lock->lock));
}

lock_t frisbee_spin;
anderson_lock_t frisbee_anderson;
seqlock_t frisbee_seqlock;
anderson_lock_t time_lock;
int cur_passes;
int passes;
int threads;
int time_printed;

void* spin_sub_func(void* id) {
    int ticks = uptime();
    while (cur_passes < passes) {
        lock_acquire(&frisbee_spin);
        if (cur_passes % threads == (int) id && cur_passes < passes)
            printf(2, "Pass number no: %d, Thread %d is passing the token to thread %d\n", cur_passes++, id, id + 1);
        lock_release(&frisbee_spin);
    }
    if (!time_printed) {
        anderson_lock_acquire(&time_lock);
        if (!time_printed)
            printf(2, "time elapsed:%d\n", uptime() - ticks);
        time_printed = 1;
        anderson_lock_release(&time_lock);
    }
    exit();
}

void* anderson_sub_func(void* id) {
    int ticks = uptime();
    while (cur_passes < passes) {
        anderson_lock_acquire(&frisbee_anderson);
        if (cur_passes % threads == (int)id && cur_passes < passes)
            printf(2, "Pass number no: %d, Thread %d is passing the token to thread %d\n", cur_passes++, id, id + 1);
        anderson_lock_release(&frisbee_anderson);
    }
    if (!time_printed) {
        anderson_lock_acquire(&time_lock);
        if (!time_printed)
            printf(2, "time elapsed:%d\n", uptime() - ticks);
        time_printed = 1;
        anderson_lock_release(&time_lock);
    }
    exit();
}

void* seqlock_sub_func(void* id) {
    int ticks = uptime();
    while (cur_passes < passes) {
        if (cur_passes % threads == (int)id && ((seqlock_read_check(&frisbee_seqlock) & 1) == 0)) {
            seqlock_write_acquire(&frisbee_seqlock);
            if (cur_passes % threads == (int)id && cur_passes < passes)
                printf(2, "Pass number no: %d, Thread %d is passing the token to thread %d\n", cur_passes++, id, id + 1);
            seqlock_write_release(&frisbee_seqlock);
        }
    }
    if (!time_printed) {
        anderson_lock_acquire(&time_lock);
        if (!time_printed)
            printf(2, "time elapsed:%d\n", uptime() - ticks);
        time_printed = 1;
        anderson_lock_release(&time_lock);
    }
    exit();
}

int main(int argc, char** argv) {
    if (argc != 4) {
        printf(2, "usage %s threads passes methods\n(1.spin 2.anderson 3.seqlock)\n", argv[0]);
        exit();
    }
    threads = atoi(argv[1]);
    passes = atoi(argv[2]);
    int methods = atoi(argv[3]);
    cur_passes = 0;
    anderson_lock_init_and_acquire(&time_lock);
    anderson_lock_release(&time_lock);
    if (methods == 1) {
        lock_init(&frisbee_spin);
        for (int i = 0; i < threads; i++)
            thread_create(spin_sub_func, (void *) i);
    }
    else if (methods == 2) {
        anderson_lock_init_and_acquire(&frisbee_anderson);
        anderson_lock_release(&frisbee_anderson);
        for (int i = 0; i < threads; i++)
            thread_create(anderson_sub_func, (void *) i);
    }
    else if (methods == 3) {
        seqlock_init(&frisbee_seqlock);
        for (int i = 0; i < threads; i++)
            thread_create(seqlock_sub_func, (void *) i);
    }
    for (int i = 0; i < threads; i++)
        wait( );
    exit();
}
