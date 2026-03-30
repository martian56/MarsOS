#include <stdint.h>

#include "ipc.h"

#define IPC_MAX_MESSAGES 64u
#define IPC_TEXT_MAX 64u

typedef struct {
    uint32_t from_pid;
    uint32_t to_pid;
    char text[IPC_TEXT_MAX];
    uint8_t used;
} ipc_message_t;

static ipc_message_t ipc_messages[IPC_MAX_MESSAGES];
static int ipc_ready;

static int str_copy(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || src == 0 || dst_size == 0u) {
        return 0;
    }

    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return (int)i;
}

int ipc_init(void) {
    for (uint32_t i = 0; i < IPC_MAX_MESSAGES; i++) {
        ipc_messages[i].from_pid = 0;
        ipc_messages[i].to_pid = 0;
        ipc_messages[i].text[0] = '\0';
        ipc_messages[i].used = 0;
    }

    ipc_ready = 1;
    return 1;
}

int ipc_send(uint32_t from_pid, uint32_t to_pid, const char *message) {
    if (!ipc_ready || to_pid == 0 || message == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < IPC_MAX_MESSAGES; i++) {
        if (ipc_messages[i].used == 0) {
            ipc_messages[i].from_pid = from_pid;
            ipc_messages[i].to_pid = to_pid;
            if (str_copy(ipc_messages[i].text, IPC_TEXT_MAX, message) <= 0) {
                return 0;
            }
            ipc_messages[i].used = 1;
            return 1;
        }
    }

    return 0;
}

int ipc_recv(uint32_t to_pid, char *out, uint32_t out_size, uint32_t *from_pid_out) {
    if (!ipc_ready || to_pid == 0 || out == 0 || out_size == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < IPC_MAX_MESSAGES; i++) {
        if (ipc_messages[i].used != 0 && ipc_messages[i].to_pid == to_pid) {
            const int copied = str_copy(out, out_size, ipc_messages[i].text);

            if (from_pid_out != 0) {
                *from_pid_out = ipc_messages[i].from_pid;
            }

            ipc_messages[i].used = 0;
            ipc_messages[i].from_pid = 0;
            ipc_messages[i].to_pid = 0;
            ipc_messages[i].text[0] = '\0';
            return copied;
        }
    }

    out[0] = '\0';
    return 0;
}

uint32_t ipc_pending_count(void) {
    uint32_t count = 0;

    if (!ipc_ready) {
        return 0;
    }

    for (uint32_t i = 0; i < IPC_MAX_MESSAGES; i++) {
        if (ipc_messages[i].used != 0) {
            count++;
        }
    }

    return count;
}
