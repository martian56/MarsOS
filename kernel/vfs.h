#ifndef MARS_OS_VFS_H
#define MARS_OS_VFS_H

#include <stdint.h>

int vfs_init(void);
int vfs_write_file(const char *name, const char *data);
const char *vfs_read_file(const char *name);
int vfs_read_file_into(const char *name, char *out, uint32_t out_size);
uint32_t vfs_file_count(void);
const char *vfs_file_name_at(uint32_t index);

#endif