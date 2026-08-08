#include <kernel/ipc.h>

int ipc_send(uint32_t source,
             uint32_t destination,
             const void *data,
             uint32_t length)
{
    (void)source;
    (void)destination;
    (void)data;
    (void)length;

    return 0;
}

int ipc_receive(uint32_t destination,
                ipc_message_t *message)
{
    (void)destination;
    (void)message;

    return 0;
}
