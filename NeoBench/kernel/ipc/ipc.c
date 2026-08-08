#include <kernel/ipc.h>

static ipc_message_t queue[NB_IPC_QUEUE_SIZE];

void ipc_init(void)
{
    for (int i = 0; i < NB_IPC_QUEUE_SIZE; i++)
    {
        queue[i].used = 0;
    }
}
