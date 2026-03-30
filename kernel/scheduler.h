#ifndef MARS_OS_SCHEDULER_H
#define MARS_OS_SCHEDULER_H

#include <stdint.h>

typedef void (*task_entry_t)(void *arg);

void scheduler_init(void);
int scheduler_create_task(task_entry_t entry, void *arg, const char *name);
void scheduler_yield(void);
void scheduler_on_tick(void);
void scheduler_run_pending(void);
void scheduler_exit_current(void);
uint32_t scheduler_task_count(void);
uint32_t scheduler_runnable_count(void);
uint32_t scheduler_current_task_id(void);
int scheduler_task_is_zombie(uint32_t tid);
int scheduler_reap_task(uint32_t tid);

#endif