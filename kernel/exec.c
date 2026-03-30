#include <stdint.h>

#include "exec.h"
#include "process.h"
#include "scheduler.h"
#include "serial.h"
#include "timer.h"
#include "vfs.h"

typedef enum {
    EXEC_MODE_KERNEL = 0,
    EXEC_MODE_USER,
} exec_mode_t;

typedef struct {
    const char *name;
    exec_mode_t mode;
    task_entry_t entry;
    uint32_t user_prog_id;
} program_entry_t;

static int str_eq(const char *a, const char *b) {
    uint32_t i = 0;

    if (a == 0 || b == 0) {
        return 0;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == '\0' && b[i] == '\0';
}

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

static void app_counter(void *arg) {
    uint32_t tag = (uint32_t)arg;

    for (uint32_t i = 0; i < 16u; i++) {
        serial_puts("app counter tag=");
        serial_put_hex32(tag);
        serial_puts(" i=");
        serial_put_hex32(i);
        serial_puts(" ticks=");
        serial_put_hex32(timer_get_ticks32());
        serial_puts("\n");
        scheduler_yield();
    }
}

static void app_writer(void *arg) {
    (void)arg;

    for (uint32_t i = 0; i < 4u; i++) {
        char payload[40];
        payload[0] = 'w';
        payload[1] = 'r';
        payload[2] = 'i';
        payload[3] = 't';
        payload[4] = 'e';
        payload[5] = '#';
        payload[6] = (char)('0' + (i % 10u));
        payload[7] = '\0';

        vfs_write_file("app.log", payload);
        scheduler_yield();
    }
}

static program_entry_t programs[] = {
    {"counter", EXEC_MODE_KERNEL, app_counter, 0u},
    {"writer", EXEC_MODE_KERNEL, app_writer, 0u},
    {"hello_ping", EXEC_MODE_USER, 0, 0u},
    {"helloapp", EXEC_MODE_USER, 0, 0u},
    {"userprobe", EXEC_MODE_USER, 0, 1u},
    {"uping", EXEC_MODE_USER, 0, 2u},
};

int exec_init(void) { return 1; }

uint32_t exec_program_count(void) { return (uint32_t)(sizeof(programs) / sizeof(programs[0])); }

const char *exec_program_name_at(uint32_t index) {
    if (index >= exec_program_count()) {
        return 0;
    }

    return programs[index].name;
}

int exec_name_copy_at(uint32_t index, char *out, uint32_t out_size) {
    const char *name = exec_program_name_at(index);

    if (name == 0) {
        return -1;
    }

    return str_copy(out, out_size, name);
}

int exec_spawn(const char *name) {
    for (uint32_t i = 0; i < exec_program_count(); i++) {
        if (str_eq(programs[i].name, name)) {
            int pid;

            if (programs[i].mode == EXEC_MODE_USER) {
                pid = process_spawn_user(programs[i].name, 0, (void *)programs[i].user_prog_id);
            } else {
                pid = process_spawn_kernel(programs[i].name, programs[i].entry, (void *)(i + 1u));
            }

            if (pid < 0) {
                return -1;
            }
            return pid;
        }
    }

    return -1;
}
