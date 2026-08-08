#include <kernel/scheduler.h>

static task_t *current_task = 0;

void scheduler_init(void)
{
    current_task = 0;
}

void scheduler_tick(void)
{
    /* TODO: Select the next runnable task */
}

void scheduler_yield(void)
{
    scheduler_tick();
}
