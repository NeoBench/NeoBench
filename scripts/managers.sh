#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "========================================"
echo "Creating NeoBench Process Manager"
echo "========================================"

mkdir -p kernel/process
mkdir -p include/kernel

###############################################################################
# Process Manager
###############################################################################

cat > kernel/process/process.c <<'EOF'
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
EOF

###############################################################################
# Thread Manager
###############################################################################

cat > kernel/process/thread.c <<'EOF'
#include <kernel/process.h>

void thread_init(void)
{

}

thread_t *thread_create(process_t *proc)
{
    (void)proc;
    return 0;
}
EOF

###############################################################################
# Header
###############################################################################

cat > include/kernel/process.h <<'EOF'
#ifndef NB_PROCESS_H
#define NB_PROCESS_H

#include <neobench/types.h>

#define NB_MAX_PROCESSES 128
#define NB_MAX_THREADS   512

typedef enum
{
    PROCESS_UNUSED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_ZOMBIE
} process_state_t;

typedef struct
{
    uint32_t tid;
    void *stack;
    void *context;
} thread_t;

typedef struct
{
    uint32_t pid;
    process_state_t state;
    thread_t *main_thread;
} process_t;

void process_init(void);

process_t *process_create(void);

void process_destroy(process_t *);

void thread_init(void);

thread_t *thread_create(process_t *);

#endif
EOF

echo
echo "Process Manager created."
