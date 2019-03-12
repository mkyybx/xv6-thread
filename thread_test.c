//
// Created by Ice on 2019-03-11.
//

#include "types.h"
#include "user.h"
#include "mmu.h"
#include "x86.h"

typedef struct {
    int id;
    int locked;
} lock_t;

static int global_lock_id = 0;

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

void lock_init(lock_t* lock) {
    lock->id = global_lock_id++;
    lock->locked = 0;
}