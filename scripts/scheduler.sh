#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "======================================="
echo "Creating NeoBench Scheduler"
echo "======================================="

mkdir -p kernel/scheduler
mkdir -p include/kernel

###############################################################################
# Scheduler
###############################################################################

cat > kernel/scheduler/scheduler.c <<'EOF'
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
EOF

###############################################################################
# Task Management
###############################################################################

cat > kernel/scheduler/task.c <<'EOF'
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
EOF

###############################################################################
# Context Switching (Stub)
###############################################################################

cat > kernel/scheduler/context.S <<'EOF'
    .text
    .global context_switch

context_switch:
    /* TODO:
     * Save registers
     * Restore next task registers
     * Return to new task
     */
    rts
EOF

###############################################################################
# Header
###############################################################################

cat > include/kernel/scheduler.h <<'EOF'
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
EOF

echo
echo "Scheduler subsystem created."
