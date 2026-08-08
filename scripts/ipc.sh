#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "========================================"
echo "Creating NeoBench IPC Subsystem"
echo "========================================"

mkdir -p kernel/ipc
mkdir -p include/kernel

###############################################################################
# IPC Manager
###############################################################################

cat > kernel/ipc/ipc.c <<'EOF'
#include <kernel/ipc.h>

static ipc_message_t queue[NB_IPC_QUEUE_SIZE];

void ipc_init(void)
{
    for (int i = 0; i < NB_IPC_QUEUE_SIZE; i++)
    {
        queue[i].used = 0;
    }
}
EOF

###############################################################################
# Message Passing
###############################################################################

cat > kernel/ipc/message.c <<'EOF'
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
EOF

###############################################################################
# Header
###############################################################################

cat > include/kernel/ipc.h <<'EOF'
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
EOF

echo
echo "IPC subsystem created."
