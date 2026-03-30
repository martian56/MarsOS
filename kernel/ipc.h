#ifndef MARS_OS_IPC_H
#define MARS_OS_IPC_H

#include <stdint.h>

int ipc_init(void);
int ipc_send(uint32_t from_pid, uint32_t to_pid, const char *message);
int ipc_recv(uint32_t to_pid, char *out, uint32_t out_size, uint32_t *from_pid_out);
uint32_t ipc_pending_count(void);

#endif