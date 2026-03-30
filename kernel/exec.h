#ifndef MARS_OS_EXEC_H
#define MARS_OS_EXEC_H

#include <stdint.h>

int exec_init(void);
uint32_t exec_program_count(void);
const char *exec_program_name_at(uint32_t index);
int exec_name_copy_at(uint32_t index, char *out, uint32_t out_size);
int exec_spawn(const char *name);

#endif