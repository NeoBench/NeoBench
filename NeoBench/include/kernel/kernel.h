#ifndef NB_KERNEL_H
#define NB_KERNEL_H

void kernel_main(void);

void kernel_init(void);

void scheduler_init(void);

void kernel_loop(void);

void panic(const char *);

#endif
