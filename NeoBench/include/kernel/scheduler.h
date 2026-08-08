#ifndef NB_SCHEDULER_H
#define NB_SCHEDULER_H

#include <neobench/types.h>

#define NB_MAX_TASKS 64

typedef enum
{
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_BLOCKED
} task_state_t;

typedef struct task
{
    uint32_t pid;
    task_state_t state;
    void *stack;
    void *entry;
} task_t;

void scheduler_init(void);
void scheduler_tick(void);
void scheduler_yield(void);

void task_init(void);
task_t *task_create(void);

#endif
