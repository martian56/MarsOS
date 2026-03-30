#include <stdint.h>

#include "gdt.h"
#include "kheap.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "scheduler.h"
#include "vfs.h"

#define PROCESS_MAX 32u

typedef struct {
    uint32_t pid;
    uint32_t tid;
    uint32_t cr3;
    uint32_t user_code_frame;
    uint32_t user_stack_frame;
    uint32_t user_entry;
    uint32_t syscall_mask_low;
    uint8_t is_kernel;
    uint8_t active;
    char name[16];
} process_entry_t;

static process_entry_t process_table[PROCESS_MAX];
static uint32_t next_pid;
static int process_ready;

#define PROCESS_USER_CODE_VADDR 0x40000000u
#define PROCESS_USER_STACK_TOP 0x40002000u
#define PROCESS_USER_IMAGE_MAX 0x4000u
#define PROCESS_USER_CODE_SIZE 0x1000u
#define PROCESS_USER_PROG_DEFAULT 1u
#define PROCESS_USER_PROG_PING_BURST 2u

static int process_build_user_image(uint32_t cr3, uint32_t user_prog_id, uint32_t *code_frame_out,
                                    uint32_t *stack_frame_out, uint32_t *entry_out,
                                    const char *program_name);
static int process_find_by_tid(uint32_t tid);
extern void usermode_switch(uint32_t entry, uint32_t user_stack_top);

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

#define ELF_MAGIC_0 0x7Fu
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELF_CLASS_32 1u
#define ELF_DATA_LE 1u
#define ELF_VERSION_CURRENT 1u
#define ELF_MACHINE_X86 3u
#define ELF_TYPE_EXEC 2u
#define ELF_PT_LOAD 1u

static uint32_t str_len(const char *s) {
    uint32_t n = 0;

    if (s == 0) {
        return 0;
    }

    while (s[n] != '\0') {
        n++;
    }

    return n;
}

static int parse_hex_nibble(char c, uint8_t *value_out) {
    if (c >= '0' && c <= '9') {
        *value_out = (uint8_t)(c - '0');
        return 1;
    }

    if (c >= 'a' && c <= 'f') {
        *value_out = (uint8_t)(10 + (c - 'a'));
        return 1;
    }

    if (c >= 'A' && c <= 'F') {
        *value_out = (uint8_t)(10 + (c - 'A'));
        return 1;
    }

    return 0;
}

static void mem_copy_u8(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

static void mem_zero_u8(uint8_t *dst, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = 0;
    }
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

static int str_eq_n(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int parse_user_image_hex(const char *text, volatile uint8_t *code, uint32_t code_cap,
                                uint32_t *out_len) {
    uint32_t count = 0;
    const char *p;

    if (text == 0 || code == 0 || out_len == 0 || code_cap == 0u) {
        return 0;
    }

    p = skip_ws(text);
    if (str_eq_n(p, "MARSHEX", 7u)) {
        p += 7;
    }

    while (1) {
        uint8_t hi;
        uint8_t lo;

        p = skip_ws(p);
        if (*p == '\0') {
            break;
        }

        if (p[1] == '\0') {
            return 0;
        }

        if (!parse_hex_nibble(p[0], &hi) || !parse_hex_nibble(p[1], &lo)) {
            return 0;
        }

        if (count >= code_cap) {
            return 0;
        }

        code[count++] = (uint8_t)((hi << 4) | lo);
        p += 2;
    }

    if (count == 0u) {
        return 0;
    }

    *out_len = count;
    return 1;
}

static int process_load_user_image_bytes_from_vfs(const char *program_name, uint8_t *dst,
                                                  uint32_t dst_cap, uint32_t *out_len) {
    const char *image_text;

    if (program_name == 0 || program_name[0] == '\0') {
        return 0;
    }

    image_text = vfs_read_file(program_name);
    if (image_text == 0) {
        char alt_name[40];
        uint32_t i = 0;
        const char *prefix = "app.";

        while (prefix[i] != '\0' && i + 1u < sizeof(alt_name)) {
            alt_name[i] = prefix[i];
            i++;
        }

        {
            uint32_t j = 0;
            while (program_name[j] != '\0') {
                if (i + 1u >= sizeof(alt_name)) {
                    return 0;
                }

                alt_name[i++] = program_name[j++];
            }
        }
        alt_name[i] = '\0';

        image_text = vfs_read_file(alt_name);
        if (image_text == 0) {
            return 0;
        }
    }

    if (str_len(image_text) < 2u) {
        return -1;
    }

    if (!parse_user_image_hex(image_text, dst, dst_cap, out_len)) {
        return -1;
    }

    return 1;
}

static int process_is_elf32_image(const uint8_t *image, uint32_t image_len) {
    if (image == 0 || image_len < sizeof(elf32_ehdr_t)) {
        return 0;
    }

    return image[0] == ELF_MAGIC_0 && image[1] == ELF_MAGIC_1 && image[2] == ELF_MAGIC_2 &&
           image[3] == ELF_MAGIC_3;
}

static int process_load_elf32_single_page(const uint8_t *image, uint32_t image_len,
                                          volatile uint8_t *code, uint32_t code_vaddr,
                                          uint32_t code_size, uint32_t *entry_out) {
    const elf32_ehdr_t *eh;
    int loaded_any = 0;
    int entry_in_load = 0;

    if (image == 0 || code == 0 || entry_out == 0 || image_len < sizeof(elf32_ehdr_t)) {
        return 0;
    }

    eh = (const elf32_ehdr_t *)image;

    if (eh->e_ident[0] != ELF_MAGIC_0 || eh->e_ident[1] != ELF_MAGIC_1 ||
        eh->e_ident[2] != ELF_MAGIC_2 || eh->e_ident[3] != ELF_MAGIC_3) {
        return 0;
    }
    if (eh->e_ident[4] != ELF_CLASS_32 || eh->e_ident[5] != ELF_DATA_LE ||
        eh->e_ident[6] != ELF_VERSION_CURRENT) {
        return 0;
    }
    if (eh->e_machine != ELF_MACHINE_X86) {
        return 0;
    }
    if (eh->e_type != ELF_TYPE_EXEC) {
        return 0;
    }
    if (eh->e_version != ELF_VERSION_CURRENT) {
        return 0;
    }
    if (eh->e_ehsize != sizeof(elf32_ehdr_t)) {
        return 0;
    }
    if (eh->e_phentsize != sizeof(elf32_phdr_t)) {
        return 0;
    }
    if (eh->e_phoff > image_len) {
        return 0;
    }
    if (eh->e_phnum > 0u) {
        const uint32_t ph_bytes = (uint32_t)eh->e_phnum * (uint32_t)eh->e_phentsize;
        if (eh->e_phoff > image_len - ph_bytes) {
            return 0;
        }
    }

    mem_zero_u8((uint8_t *)code, code_size);

    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(image + eh->e_phoff + ((uint32_t)i * eh->e_phentsize));
        uint32_t seg_off;

        if (ph->p_type != ELF_PT_LOAD) {
            continue;
        }

        if (ph->p_memsz == 0u) {
            continue;
        }

        if (ph->p_vaddr > UINT32_MAX - ph->p_memsz) {
            return 0;
        }

        if (ph->p_filesz > ph->p_memsz) {
            return 0;
        }

        if (ph->p_offset > image_len || ph->p_filesz > image_len - ph->p_offset) {
            return 0;
        }

        if (ph->p_vaddr < code_vaddr) {
            return 0;
        }

        seg_off = ph->p_vaddr - code_vaddr;
        if (seg_off > code_size || ph->p_memsz > code_size - seg_off) {
            return 0;
        }

        if (ph->p_filesz > 0u) {
            mem_copy_u8((uint8_t *)code + seg_off, image + ph->p_offset, ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            mem_zero_u8((uint8_t *)code + seg_off + ph->p_filesz, ph->p_memsz - ph->p_filesz);
        }

        if (eh->e_entry >= ph->p_vaddr && eh->e_entry < (ph->p_vaddr + ph->p_memsz)) {
            entry_in_load = 1;
        }

        loaded_any = 1;
    }

    if (!loaded_any) {
        return 0;
    }

    if (eh->e_entry < code_vaddr || eh->e_entry >= (code_vaddr + code_size)) {
        return 0;
    }

    if (!entry_in_load) {
        return 0;
    }

    *entry_out = eh->e_entry;
    return 1;
}

static uint32_t process_emit_ping_program(volatile uint8_t *code, uint32_t ping_count) {
    uint32_t off = 0;

    if (ping_count == 0u) {
        ping_count = 1u;
    }
    if (ping_count > 32u) {
        ping_count = 32u;
    }

    for (uint32_t i = 0; i < ping_count; i++) {
        code[off++] = 0xB8; /* mov eax, 17 */
        code[off++] = 17;
        code[off++] = 0;
        code[off++] = 0;
        code[off++] = 0;
        code[off++] = 0xCD; /* int 0x80 */
        code[off++] = 0x80;
    }

    code[off++] = 0xCD; /* int 0x81 */
    code[off++] = 0x81;
    code[off++] = 0xEB; /* jmp -2 (unreachable unless exit fails) */
    code[off++] = 0xFE;
    return off;
}

static uint32_t process_emit_exit_only_program(volatile uint8_t *code) {
    uint32_t off = 0;

    code[off++] = 0xCD; /* int 0x81 */
    code[off++] = 0x81;
    code[off++] = 0xEB; /* jmp -2 (unreachable unless exit fails) */
    code[off++] = 0xFE;
    return off;
}

static int process_table_is_empty(uint32_t *table) {
    for (uint32_t i = 0; i < 1024u; i++) {
        if ((table[i] & PAGING_FLAG_PRESENT) != 0) {
            return 0;
        }
    }

    return 1;
}

static void process_release_user_table(uint32_t cr3, uint32_t virt) {
    uint32_t *dir;
    const uint32_t pd_index = (virt >> 22) & 0x3FFu;

    if (cr3 == 0) {
        return;
    }

    dir = (uint32_t *)cr3;
    if ((dir[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        return;
    }

    if ((dir[pd_index] & PAGING_FLAG_USER) == 0) {
        return;
    }

    {
        uint32_t *table = (uint32_t *)(dir[pd_index] & 0xFFFFF000u);
        if (process_table_is_empty(table)) {
            pmm_free_frame((uint32_t)table);
            dir[pd_index] = 0;
        }
    }
}

static void process_unmap_user_pages(uint32_t cr3) {
    const uint32_t prev_cr3 = paging_current_directory_phys();

    if (cr3 == 0 || cr3 == paging_kernel_directory_phys()) {
        return;
    }

    paging_switch_directory(cr3);
    paging_unmap_page(PROCESS_USER_CODE_VADDR);
    paging_unmap_page(PROCESS_USER_STACK_TOP - 0x1000u);
    paging_switch_directory(prev_cr3);

    process_release_user_table(cr3, PROCESS_USER_CODE_VADDR);
}

static void process_user_task_entry(void *arg) {
    const uint32_t tid = scheduler_current_task_id();
    uint32_t user_entry = PROCESS_USER_CODE_VADDR;
    int idx;
    uint32_t kernel_stack_top;

    (void)arg;
    idx = process_find_by_tid(tid);
    if (idx >= 0 && process_table[idx].user_entry != 0u) {
        user_entry = process_table[idx].user_entry;
    }

    __asm__ volatile("mov %%esp, %0" : "=r"(kernel_stack_top));
    gdt_set_kernel_stack(kernel_stack_top);
    usermode_switch(user_entry, PROCESS_USER_STACK_TOP - 16u);
}

#define SYSCALL_BIT(x) (1u << (x))

static const uint32_t PROCESS_SYSCALL_MASK_KERNEL = 0xFFFFFFFFu;
static const uint32_t PROCESS_SYSCALL_MASK_USER = SYSCALL_BIT(0) |  /* ticks */
                                                  SYSCALL_BIT(1) |  /* heap total */
                                                  SYSCALL_BIT(2) |  /* heap used */
                                                  SYSCALL_BIT(3) |  /* task count */
                                                  SYSCALL_BIT(4) |  /* runnable count */
                                                  SYSCALL_BIT(5) |  /* current task */
                                                  SYSCALL_BIT(6) |  /* vfs count */
                                                  SYSCALL_BIT(7) |  /* vfs read */
                                                  SYSCALL_BIT(9) |  /* process count */
                                                  SYSCALL_BIT(10) | /* current pid */
                                                  SYSCALL_BIT(11) | /* exec count */
                                                  SYSCALL_BIT(13) | /* exec name */
                                                  SYSCALL_BIT(14) | /* ipc send */
                                                  SYSCALL_BIT(15) | /* ipc recv */
                                                  SYSCALL_BIT(16) | /* ipc pending */
                                                  SYSCALL_BIT(17) | /* user ping */
                                                  SYSCALL_BIT(18) | /* user ping count */
                                                  SYSCALL_BIT(19);  /* process exit */

static void str_copy(char *dst, const char *src, uint32_t n) {
    uint32_t i = 0;

    if (n == 0u) {
        return;
    }

    while (i + 1u < n && src != 0 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int process_find_free_slot(void) {
    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].active == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int process_pid_in_use(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].active != 0 && process_table[i].pid == pid) {
            return 1;
        }
    }

    return 0;
}

static uint32_t process_alloc_pid(void) {
    for (uint32_t tries = 0; tries <= PROCESS_MAX; tries++) {
        uint32_t pid = next_pid;

        if (pid < 2u) {
            pid = 2u;
        }

        next_pid = pid + 1u;
        if (next_pid < 2u) {
            next_pid = 2u;
        }

        if (!process_pid_in_use(pid)) {
            return pid;
        }
    }

    return 2u;
}

static int process_find_by_tid(uint32_t tid) {
    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].active != 0 && process_table[i].tid == tid) {
            return (int)i;
        }
    }
    return -1;
}

static int process_spawn_common(const char *name, task_entry_t entry, void *arg,
                                uint8_t is_kernel) {
    int slot;
    int tid;
    uint32_t cr3 = 0;
    uint32_t code_frame = 0;
    uint32_t stack_frame = 0;
    uint32_t user_entry = PROCESS_USER_CODE_VADDR;
    uint32_t user_prog_id = PROCESS_USER_PROG_DEFAULT;

    if (!process_ready) {
        return -1;
    }

    if (is_kernel != 0 && entry == 0) {
        return -1;
    }

    slot = process_find_free_slot();
    if (slot < 0) {
        return -1;
    }

    if (is_kernel != 0) {
        cr3 = paging_kernel_directory_phys();
    } else {
        if ((uint32_t)arg != 0u) {
            user_prog_id = (uint32_t)arg;
        }

        if (!paging_clone_kernel_directory(&cr3)) {
            return -1;
        }

        if (!process_build_user_image(cr3, user_prog_id, &code_frame, &stack_frame, &user_entry,
                                      name)) {
            pmm_free_frame(cr3);
            return -1;
        }
    }

    if (is_kernel != 0) {
        tid = scheduler_create_task(entry, arg, name);
    } else {
        tid = scheduler_create_task(process_user_task_entry, (void *)(uint32_t)(slot + 1), name);
    }
    if (tid < 0) {
        if (is_kernel == 0) {
            process_unmap_user_pages(cr3);
            pmm_free_frame(stack_frame);
            pmm_free_frame(code_frame);
            pmm_free_frame(cr3);
        }
        return -1;
    }

    process_table[slot].pid = process_alloc_pid();
    process_table[slot].tid = (uint32_t)tid;
    process_table[slot].cr3 = cr3;
    process_table[slot].user_code_frame = code_frame;
    process_table[slot].user_stack_frame = stack_frame;
    process_table[slot].user_entry = user_entry;
    process_table[slot].syscall_mask_low =
        (is_kernel != 0) ? PROCESS_SYSCALL_MASK_KERNEL : PROCESS_SYSCALL_MASK_USER;
    process_table[slot].is_kernel = is_kernel;
    process_table[slot].active = 1;
    str_copy(process_table[slot].name, name != 0 ? name : "proc", sizeof(process_table[slot].name));

    return (int)process_table[slot].pid;
}

static int process_build_user_image(uint32_t cr3, uint32_t user_prog_id, uint32_t *code_frame_out,
                                    uint32_t *stack_frame_out, uint32_t *entry_out,
                                    const char *program_name) {
    const uint32_t prev_cr3 = paging_current_directory_phys();
    uint32_t code_frame = 0;
    uint32_t stack_frame = 0;
    uint32_t entry = PROCESS_USER_CODE_VADDR;
    int code_mapped = 0;
    int stack_mapped = 0;
    volatile uint8_t *code;

    paging_switch_directory(cr3);

    /* Remove inherited user test mapping from cloned kernel directory, if present. */
    paging_unmap_page(PROCESS_USER_CODE_VADDR);
    paging_unmap_page(PROCESS_USER_STACK_TOP - 0x1000u);

    code_frame = pmm_alloc_frame();
    stack_frame = pmm_alloc_frame();
    if (code_frame == 0 || stack_frame == 0) {
        goto fail;
    }

    if (!paging_map_page(PROCESS_USER_CODE_VADDR, code_frame,
                         PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER)) {
        goto fail;
    }
    code_mapped = 1;

    if (!paging_map_page(PROCESS_USER_STACK_TOP - 0x1000u, stack_frame,
                         PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER)) {
        goto fail;
    }
    stack_mapped = 1;

    code = (volatile uint8_t *)PROCESS_USER_CODE_VADDR;
    {
        uint8_t *image = (uint8_t *)kmalloc(PROCESS_USER_IMAGE_MAX);
        uint32_t loaded_len = 0;
        int image_status = 0;
        int loaded = 0;

        if (image != 0) {
            image_status = process_load_user_image_bytes_from_vfs(
                program_name, image, PROCESS_USER_IMAGE_MAX, &loaded_len);
            if (image_status > 0) {
                if (process_is_elf32_image(image, loaded_len)) {
                    loaded = process_load_elf32_single_page(image, loaded_len, code,
                                                            PROCESS_USER_CODE_VADDR,
                                                            PROCESS_USER_CODE_SIZE, &entry);
                } else {
                    if (loaded_len > PROCESS_USER_CODE_SIZE) {
                        loaded = 0;
                    } else {
                        mem_zero_u8((uint8_t *)code, PROCESS_USER_CODE_SIZE);
                        mem_copy_u8((uint8_t *)code, image, loaded_len);
                        entry = PROCESS_USER_CODE_VADDR;
                        loaded = 1;
                    }
                }

                if (!loaded) {
                    image_status = -1;
                }
            }
            kfree(image);
        }

        if (image_status < 0) {
            goto fail;
        }

        if (!loaded) {
            if (user_prog_id == PROCESS_USER_PROG_DEFAULT) {
                process_emit_exit_only_program(code);
            } else if (user_prog_id == PROCESS_USER_PROG_PING_BURST) {
                process_emit_ping_program(code, 8u);
            } else {
                process_emit_ping_program(code, 2u);
            }
            entry = PROCESS_USER_CODE_VADDR;
        }
    }

    paging_switch_directory(prev_cr3);
    *code_frame_out = code_frame;
    *stack_frame_out = stack_frame;
    *entry_out = entry;
    return 1;

fail:
    if (stack_mapped) {
        paging_unmap_page(PROCESS_USER_STACK_TOP - 0x1000u);
    }
    if (code_mapped) {
        paging_unmap_page(PROCESS_USER_CODE_VADDR);
    }
    if (stack_mapped || code_mapped) {
        process_release_user_table(cr3, PROCESS_USER_CODE_VADDR);
    }
    if (stack_frame != 0) {
        pmm_free_frame(stack_frame);
    }
    if (code_frame != 0) {
        pmm_free_frame(code_frame);
    }

    paging_switch_directory(prev_cr3);
    return 0;
}

int process_init(void) {
    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        process_table[i].pid = 0;
        process_table[i].tid = 0;
        process_table[i].cr3 = 0;
        process_table[i].user_code_frame = 0;
        process_table[i].user_stack_frame = 0;
        process_table[i].user_entry = 0;
        process_table[i].syscall_mask_low = 0;
        process_table[i].is_kernel = 0;
        process_table[i].active = 0;
        process_table[i].name[0] = '\0';
    }

    process_table[0].pid = 1;
    process_table[0].tid = 0;
    process_table[0].cr3 = paging_kernel_directory_phys();
    process_table[0].user_code_frame = 0;
    process_table[0].user_stack_frame = 0;
    process_table[0].user_entry = 0;
    process_table[0].syscall_mask_low = PROCESS_SYSCALL_MASK_KERNEL;
    process_table[0].is_kernel = 1;
    process_table[0].active = 1;
    str_copy(process_table[0].name, "kernel", sizeof(process_table[0].name));

    next_pid = 2;
    process_ready = 1;
    return 1;
}

int process_spawn_user(const char *name, task_entry_t entry, void *arg) {
    return process_spawn_common(name, entry, arg, 0u);
}

int process_spawn_kernel(const char *name, task_entry_t entry, void *arg) {
    return process_spawn_common(name, entry, arg, 1u);
}

void process_reap(void) {
    if (!process_ready) {
        return;
    }

    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].active != 0 && process_table[i].tid != 0) {
            if (scheduler_task_is_zombie(process_table[i].tid)) {
                const uint32_t tid = process_table[i].tid;

                process_unmap_user_pages(process_table[i].cr3);
                if (process_table[i].cr3 != 0 &&
                    process_table[i].cr3 != paging_kernel_directory_phys()) {
                    pmm_free_frame(process_table[i].cr3);
                }
                if (process_table[i].user_code_frame != 0) {
                    pmm_free_frame(process_table[i].user_code_frame);
                }
                if (process_table[i].user_stack_frame != 0) {
                    pmm_free_frame(process_table[i].user_stack_frame);
                }

                process_table[i].active = 0;
                process_table[i].pid = 0;
                process_table[i].tid = 0;
                process_table[i].cr3 = 0;
                process_table[i].user_code_frame = 0;
                process_table[i].user_stack_frame = 0;
                process_table[i].user_entry = 0;
                process_table[i].syscall_mask_low = 0;
                process_table[i].is_kernel = 0;
                process_table[i].name[0] = '\0';

                scheduler_reap_task(tid);
            }
        }
    }
}

uint32_t process_count(void) {
    uint32_t count = 0;

    if (!process_ready) {
        return 0;
    }

    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].active != 0) {
            count++;
        }
    }

    return count;
}

uint32_t process_current_pid(void) {
    const uint32_t tid = scheduler_current_task_id();
    int idx;

    if (!process_ready) {
        return 0;
    }

    idx = process_find_by_tid(tid);
    if (idx < 0) {
        return 0;
    }

    return process_table[idx].pid;
}

int process_pid_at(uint32_t index, uint32_t *pid_out, const char **name_out) {
    uint32_t seen = 0;

    if (!process_ready || pid_out == 0 || name_out == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].active != 0) {
            if (seen == index) {
                *pid_out = process_table[i].pid;
                *name_out = process_table[i].name;
                return 1;
            }
            seen++;
        }
    }

    return 0;
}

int process_info_at(uint32_t index, process_info_t *info_out) {
    uint32_t seen = 0;

    if (!process_ready || info_out == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].active != 0) {
            if (seen == index) {
                info_out->pid = process_table[i].pid;
                info_out->tid = process_table[i].tid;
                info_out->cr3 = process_table[i].cr3;
                info_out->user_code_frame = process_table[i].user_code_frame;
                info_out->user_stack_frame = process_table[i].user_stack_frame;
                info_out->user_entry = process_table[i].user_entry;
                info_out->syscall_mask_low = process_table[i].syscall_mask_low;
                info_out->is_kernel = process_table[i].is_kernel;
                return 1;
            }
            seen++;
        }
    }

    return 0;
}

int process_syscall_allowed(uint32_t syscall_no) {
    const uint32_t tid = scheduler_current_task_id();
    int idx;

    if (!process_ready) {
        return 0;
    }

    if (syscall_no >= 32u) {
        return 0;
    }

    idx = process_find_by_tid(tid);
    if (idx < 0) {
        return 0;
    }

    return (process_table[idx].syscall_mask_low & (1u << syscall_no)) != 0u;
}

int process_current_is_kernel(void) {
    const uint32_t tid = scheduler_current_task_id();
    int idx;

    if (!process_ready) {
        return 1;
    }

    idx = process_find_by_tid(tid);
    if (idx < 0) {
        return 1;
    }

    return process_table[idx].is_kernel != 0;
}

void process_activate_tid(uint32_t tid) {
    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (process_table[i].active != 0 && process_table[i].tid == tid) {
            if (process_table[i].cr3 != 0) {
                paging_switch_directory(process_table[i].cr3);
                return;
            }
            break;
        }
    }

    paging_switch_directory(paging_kernel_directory_phys());
}
