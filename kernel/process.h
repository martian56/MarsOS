#ifndef MARS_OS_PROCESS_H
#define MARS_OS_PROCESS_H

#include <stdint.h>

#include "scheduler.h"

typedef struct {
	uint32_t pid;
	uint32_t tid;
	uint32_t cr3;
	uint32_t user_code_frame;
	uint32_t user_stack_frame;
	uint32_t syscall_mask_low;
	uint8_t is_kernel;
} process_info_t;

int process_init(void);
int process_spawn_user(const char *name, task_entry_t entry, void *arg);
int process_spawn_kernel(const char *name, task_entry_t entry, void *arg);
void process_reap(void);
uint32_t process_count(void);
uint32_t process_current_pid(void);
int process_pid_at(uint32_t index, uint32_t *pid_out, const char **name_out);
int process_info_at(uint32_t index, process_info_t *info_out);
int process_syscall_allowed(uint32_t syscall_no);
int process_current_is_kernel(void);
void process_activate_tid(uint32_t tid);

#endif