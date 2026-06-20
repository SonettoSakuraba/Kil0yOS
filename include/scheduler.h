#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"

#define MAX_TASKS        16
#define TASK_STACK_SIZE  4096

#define TASK_DEAD    0
#define TASK_READY   1
#define TASK_RUNNING 2

typedef struct task {
    uint32_t esp;
    int status;
    char name[32];
    uint8_t stack[TASK_STACK_SIZE];
} task_t;

void scheduler_init();
int  task_create(void (*entry)(void), const char* name);
uint32_t scheduler_tick(uint32_t current_esp);

#endif