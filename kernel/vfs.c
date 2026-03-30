#include <stdint.h>

#include "kheap.h"
#include "vfs.h"

#define VFS_MAX_FILES 64u
#define VFS_NAME_MAX 32u

typedef struct {
    char name[VFS_NAME_MAX];
    char *data;
    uint32_t len;
    uint8_t used;
} vfs_file_t;

static vfs_file_t files[VFS_MAX_FILES];
static int vfs_ready;

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int str_eq(const char *a, const char *b) {
    uint32_t i = 0;

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == '\0' && b[i] == '\0';
}

static void str_copy(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (max_len == 0u) {
        return;
    }

    while (i + 1u < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int vfs_find_file(const char *name) {
    for (uint32_t i = 0; i < VFS_MAX_FILES; i++) {
        if (files[i].used != 0 && str_eq(files[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int vfs_find_free_slot(void) {
    for (uint32_t i = 0; i < VFS_MAX_FILES; i++) {
        if (files[i].used == 0) {
            return (int)i;
        }
    }
    return -1;
}

int vfs_init(void) {
    for (uint32_t i = 0; i < VFS_MAX_FILES; i++) {
        files[i].name[0] = '\0';
        files[i].data = 0;
        files[i].len = 0;
        files[i].used = 0;
    }

    vfs_ready = 1;

    if (!vfs_write_file("welcome.txt", "Welcome to Mars OS ramfs.")) {
        return 0;
    }

    return 1;
}

int vfs_write_file(const char *name, const char *data) {
    int idx;
    uint32_t len;
    char *buf;

    if (!vfs_ready || name == 0 || data == 0) {
        return 0;
    }

    if (name[0] == '\0') {
        return 0;
    }

    len = str_len(data);
    buf = (char *)kmalloc((uint32_t)len + 1u);
    if (buf == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < len; i++) {
        buf[i] = data[i];
    }
    buf[len] = '\0';

    idx = vfs_find_file(name);
    if (idx < 0) {
        idx = vfs_find_free_slot();
        if (idx < 0) {
            kfree(buf);
            return 0;
        }

        files[idx].used = 1;
        str_copy(files[idx].name, name, VFS_NAME_MAX);
    } else if (files[idx].data != 0) {
        kfree(files[idx].data);
    }

    files[idx].data = buf;
    files[idx].len = len;
    return 1;
}

const char *vfs_read_file(const char *name) {
    int idx;

    if (!vfs_ready || name == 0) {
        return 0;
    }

    idx = vfs_find_file(name);
    if (idx < 0) {
        return 0;
    }

    return files[idx].data;
}

int vfs_read_file_into(const char *name, char *out, uint32_t out_size) {
    int idx;
    uint32_t i;

    if (!vfs_ready || name == 0 || out == 0 || out_size == 0u) {
        return -1;
    }

    idx = vfs_find_file(name);
    if (idx < 0) {
        return -1;
    }

    if (files[idx].data == 0) {
        out[0] = '\0';
        return 0;
    }

    i = 0;
    while (i + 1u < out_size && files[idx].data[i] != '\0') {
        out[i] = files[idx].data[i];
        i++;
    }
    out[i] = '\0';

    return (int)i;
}

uint32_t vfs_file_count(void) {
    uint32_t count = 0;

    if (!vfs_ready) {
        return 0;
    }

    for (uint32_t i = 0; i < VFS_MAX_FILES; i++) {
        if (files[i].used != 0) {
            count++;
        }
    }

    return count;
}

const char *vfs_file_name_at(uint32_t index) {
    uint32_t seen = 0;

    if (!vfs_ready) {
        return 0;
    }

    for (uint32_t i = 0; i < VFS_MAX_FILES; i++) {
        if (files[i].used != 0) {
            if (seen == index) {
                return files[i].name;
            }
            seen++;
        }
    }

    return 0;
}
