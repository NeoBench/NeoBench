#include <kernel/process.h>

static process_t process_table[NB_MAX_PROCESSES];

void process_init(void)
{
    for (int i = 0; i < NB_MAX_PROCESSES; i++)
    {
        process_table[i].state = PROCESS_UNUSED;
        process_table[i].pid = i;
    }
}

process_t *process_create(void)
{
    for (int i = 0; i < NB_MAX_PROCESSES; i++)
    {
        if (process_table[i].state == PROCESS_UNUSED)
        {
            process_table[i].state = PROCESS_READY;
            return &process_table[i];
        }
    }

    return 0;
}

void process_destroy(process_t *proc)
{
    if (!proc)
        return;

    proc->state = PROCESS_UNUSED;
}
