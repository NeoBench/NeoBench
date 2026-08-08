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
