#include <kernel/scheduler.h>

static task_t task_table[NB_MAX_TASKS];

void task_init(void)
{
    for (int i = 0; i < NB_MAX_TASKS; ++i)
    {
        task_table[i].state = TASK_UNUSED;
    }
}

task_t *task_create(void)
{
    for (int i = 0; i < NB_MAX_TASKS; ++i)
    {
        if (task_table[i].state == TASK_UNUSED)
        {
            task_table[i].state = TASK_READY;
            return &task_table[i];
        }
    }

    return 0;
}
