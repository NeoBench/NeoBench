#ifndef NB_IPC_H
#define NB_IPC_H

#include <neobench/types.h>

#define NB_IPC_QUEUE_SIZE 256
#define NB_IPC_DATA_SIZE  256

typedef struct
{
    uint8_t used;

    uint32_t source;
    uint32_t destination;

    uint32_t length;

    uint8_t data[NB_IPC_DATA_SIZE];

} ipc_message_t;

void ipc_init(void);

int ipc_send(uint32_t source,
             uint32_t destination,
             const void *data,
             uint32_t length);

int ipc_receive(uint32_t destination,
                ipc_message_t *message);

#endif
