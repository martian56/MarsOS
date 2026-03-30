#include <stdint.h>

#include "exec.h"
#include "ipc.h"
#include "kheap.h"
#include "paging.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
#include "timer.h"
#include "vfs.h"

#define SYSCALL_GET_TICKS32 0u
#define SYSCALL_GET_HEAP_TOTAL 1u
#define SYSCALL_GET_HEAP_USED 2u
#define SYSCALL_GET_TASK_COUNT 3u
#define SYSCALL_GET_RUNNABLE_COUNT 4u
#define SYSCALL_GET_CURRENT_TASK 5u
#define SYSCALL_VFS_COUNT 6u
#define SYSCALL_VFS_READ 7u
#define SYSCALL_VFS_WRITE 8u
#define SYSCALL_PROCESS_COUNT 9u
#define SYSCALL_PROCESS_CURRENT 10u
#define SYSCALL_EXEC_COUNT 11u
#define SYSCALL_EXEC_SPAWN 12u
#define SYSCALL_EXEC_NAME_AT 13u
#define SYSCALL_IPC_SEND 14u
#define SYSCALL_IPC_RECV 15u
#define SYSCALL_IPC_PENDING 16u
#define SYSCALL_USER_PING 17u
#define SYSCALL_USER_PING_COUNT 18u
#define SYSCALL_PROCESS_EXIT 19u

static uint32_t user_ping_count;

static int ptr_range_mapped(const void *ptr, uint32_t size) {
    uint32_t start;
    uint32_t end;
    uint32_t page;
    uint32_t phys;

    if (ptr == 0 || size == 0u) {
        return 0;
    }

    start = (uint32_t)ptr;
    if (start > UINT32_MAX - (size - 1u)) {
        return 0;
    }

    end = start + size - 1u;
    page = start & 0xFFFFF000u;

    while (1) {
        if (!paging_translate(page, &phys)) {
            return 0;
        }

        if (page >= (end & 0xFFFFF000u)) {
            break;
        }

        page += 0x1000u;
    }

    return 1;
}

static int ptr_range_ok(const void *ptr, uint32_t size, int user_only) {
    if (!ptr_range_mapped(ptr, size)) {
        return 0;
    }

    if (user_only != 0) {
        return paging_user_accessible((uint32_t)ptr, size);
    }

    return 1;
}

static uint32_t copy_in_string(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || src == 0 || dst_size == 0u) {
        return UINT32_MAX;
    }

    while (i + 1u < dst_size) {
        if (!ptr_range_mapped(src + i, 1u)) {
            return UINT32_MAX;
        }

        if (src[i] == '\0') {
            break;
        }

        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

uint32_t syscall_dispatch(uint32_t *frame) {
    const uint32_t syscall_no = frame[7];
    const int user_only_ptrs = process_current_is_kernel() ? 0 : 1;

    if (!process_syscall_allowed(syscall_no)) {
        return 0xFFFFFFFDu;
    }

    if (syscall_no == SYSCALL_GET_TICKS32) {
        return timer_get_ticks32();
    }

    if (syscall_no == SYSCALL_GET_HEAP_TOTAL) {
        return kheap_total_bytes();
    }

    if (syscall_no == SYSCALL_GET_HEAP_USED) {
        return kheap_used_bytes();
    }

    if (syscall_no == SYSCALL_GET_TASK_COUNT) {
        return scheduler_task_count();
    }

    if (syscall_no == SYSCALL_GET_RUNNABLE_COUNT) {
        return scheduler_runnable_count();
    }

    if (syscall_no == SYSCALL_GET_CURRENT_TASK) {
        return scheduler_current_task_id();
    }

    if (syscall_no == SYSCALL_VFS_COUNT) {
        return vfs_file_count();
    }

    if (syscall_no == SYSCALL_VFS_READ) {
        const char *name_ptr = (const char *)frame[4];
        char *out_ptr = (char *)frame[6];
        const uint32_t out_size = frame[5];
        char name_buf[40];

        if (copy_in_string(name_buf, sizeof(name_buf), name_ptr) == UINT32_MAX ||
            name_buf[0] == '\0') {
            return 0xFFFFFFFFu;
        }

        if (!ptr_range_ok(out_ptr, out_size, user_only_ptrs)) {
            return 0xFFFFFFFFu;
        }

        return (uint32_t)vfs_read_file_into(name_buf, out_ptr, out_size);
    }

    if (syscall_no == SYSCALL_VFS_WRITE) {
        const char *name_ptr = (const char *)frame[4];
        const char *data_ptr = (const char *)frame[6];
        char name_buf[40];
        char data_buf[128];

        if (copy_in_string(name_buf, sizeof(name_buf), name_ptr) == UINT32_MAX ||
            name_buf[0] == '\0') {
            return 0u;
        }

        if (copy_in_string(data_buf, sizeof(data_buf), data_ptr) == UINT32_MAX) {
            return 0u;
        }

        return (uint32_t)vfs_write_file(name_buf, data_buf);
    }

    if (syscall_no == SYSCALL_PROCESS_COUNT) {
        return process_count();
    }

    if (syscall_no == SYSCALL_PROCESS_CURRENT) {
        return process_current_pid();
    }

    if (syscall_no == SYSCALL_EXEC_COUNT) {
        return exec_program_count();
    }

    if (syscall_no == SYSCALL_EXEC_SPAWN) {
        const char *name_ptr = (const char *)frame[4];
        char name_buf[40];

        if (copy_in_string(name_buf, sizeof(name_buf), name_ptr) == UINT32_MAX ||
            name_buf[0] == '\0') {
            return 0xFFFFFFFFu;
        }

        return (uint32_t)exec_spawn(name_buf);
    }

    if (syscall_no == SYSCALL_EXEC_NAME_AT) {
        const uint32_t index = frame[4];
        char *out_ptr = (char *)frame[6];
        const uint32_t out_size = frame[5];

        if (!ptr_range_ok(out_ptr, out_size, user_only_ptrs)) {
            return 0xFFFFFFFFu;
        }

        return (uint32_t)exec_name_copy_at(index, out_ptr, out_size);
    }

    if (syscall_no == SYSCALL_IPC_SEND) {
        const uint32_t from_pid = process_current_pid();
        const uint32_t to_pid = frame[4];
        const char *msg_ptr = (const char *)frame[6];
        char msg_buf[64];

        if (copy_in_string(msg_buf, sizeof(msg_buf), msg_ptr) == UINT32_MAX || msg_buf[0] == '\0') {
            return 0u;
        }

        return (uint32_t)ipc_send(from_pid, to_pid, msg_buf);
    }

    if (syscall_no == SYSCALL_IPC_RECV) {
        char *out_ptr = (char *)frame[6];
        const uint32_t out_size = frame[5];
        uint32_t from_pid = 0;

        if (!ptr_range_ok(out_ptr, out_size, user_only_ptrs)) {
            return 0xFFFFFFFFu;
        }

        return (uint32_t)ipc_recv(process_current_pid(), out_ptr, out_size, &from_pid);
    }

    if (syscall_no == SYSCALL_IPC_PENDING) {
        return ipc_pending_count();
    }

    if (syscall_no == SYSCALL_USER_PING) {
        user_ping_count++;
        return user_ping_count;
    }

    if (syscall_no == SYSCALL_USER_PING_COUNT) {
        return user_ping_count;
    }

    if (syscall_no == SYSCALL_PROCESS_EXIT) {
        scheduler_exit_current();
        return 0u;
    }

    return 0xFFFFFFFFu;
}
