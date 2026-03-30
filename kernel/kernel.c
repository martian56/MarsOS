#include <stdint.h>

#include "interrupts.h"
#include "io.h"
#include "exec.h"
#include "gdt.h"
#include "ipc.h"
#include "kheap.h"
#include "keyboard.h"
#include "multiboot.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "scheduler.h"
#include "serial.h"
#include "timer.h"
#include "usermode.h"
#include "vga.h"
#include "vfs.h"

#define SHELL_LINE_MAX 128u
#define SHELL_HISTORY_MAX 8u

static int streq(const char *a, const char *b);

static void demo_task(void *arg) {
    uint32_t id = (uint32_t)arg;

    for (uint32_t i = 0; i < 12u; i++) {
        serial_puts("task ");
        serial_put_hex32(id);
        serial_puts(" tick ");
        serial_put_hex32(i);
        serial_puts("\n");
        scheduler_yield();
    }
}

static void kernel_run_selftest(void) {
    uint32_t ping_before;
    uint32_t ping_after;
    uint32_t ping_delta;
    uint32_t prog_count;
    int pid_probe;
    int pid_hello_ping;
    int pid_helloapp;
    int pid_badimg = -1;

    serial_puts("selftest: start\n");

    __asm__ volatile("int $0x80" : "=a"(ping_before) : "a"(18u) : "cc", "memory");
    __asm__ volatile("int $0x80" : "=a"(prog_count) : "a"(11u) : "cc", "memory");

    pid_probe = exec_spawn("userprobe");
    pid_hello_ping = exec_spawn("hello_ping");
    pid_helloapp = exec_spawn("helloapp");
    if (vfs_write_file("app.badimg", "MARSHEX GG")) {
        pid_badimg = exec_spawn("badimg");
    }

    if (pid_probe >= 0 || pid_hello_ping >= 0 || pid_helloapp >= 0) {
        for (uint32_t i = 0; i < 12u; i++) {
            scheduler_yield();
            process_reap();
        }
    }
    __asm__ volatile("int $0x80" : "=a"(ping_after) : "a"(18u) : "cc", "memory");
    ping_delta = ping_after - ping_before;

    serial_puts("selftest: ping_delta=");
    serial_put_hex32(ping_delta);
    serial_puts(" pid_probe=");
    serial_put_hex32((uint32_t)pid_probe);
    serial_puts(" pid_hello_ping=");
    serial_put_hex32((uint32_t)pid_hello_ping);
    serial_puts(" pid_helloapp=");
    serial_put_hex32((uint32_t)pid_helloapp);
    serial_puts(" pid_badimg=");
    serial_put_hex32((uint32_t)pid_badimg);
    serial_puts(" prog_count=");
    serial_put_hex32(prog_count);
    serial_puts("\n");

    if (prog_count >= 7u && pid_probe >= 0 && pid_hello_ping >= 0 && pid_helloapp >= 0 &&
        pid_badimg < 0 && ping_delta == 6u) {
        serial_puts("selftest: PASS\n");
    } else {
        serial_puts("selftest: FAIL spawn path\n");
    }
}

static void shell_replace_line(char *line, uint32_t *line_len, const char *text) {
    while (*line_len > 0) {
        vga_backspace();
        (*line_len)--;
    }

    while (*text != '\0' && *line_len < (SHELL_LINE_MAX - 1u)) {
        line[*line_len] = *text;
        vga_putc(*text);
        (*line_len)++;
        text++;
    }

    line[*line_len] = '\0';
}

static void shell_redraw_line(const char *line, uint32_t line_len, uint32_t cursor_pos) {
    for (uint32_t i = 0; i < line_len; i++) {
        vga_backspace();
    }

    for (uint32_t i = 0; i < line_len; i++) {
        vga_putc(line[i]);
    }

    for (uint32_t i = line_len; i > cursor_pos; i--) {
        vga_backspace();
    }
}

static uint32_t shell_move_cursor_to_end(const char *line, uint32_t cursor_pos, uint32_t line_len) {
    for (uint32_t i = cursor_pos; i < line_len; i++) {
        vga_putc(line[i]);
    }
    return line_len;
}

static void shell_store_history(char history[SHELL_HISTORY_MAX][SHELL_LINE_MAX],
                                uint32_t *history_count, const char *line) {
    if (line[0] == '\0') {
        return;
    }

    if (*history_count > 0) {
        if (streq(history[*history_count - 1u], line)) {
            return;
        }
    }

    if (*history_count < SHELL_HISTORY_MAX) {
        uint32_t i = 0;
        const uint32_t slot = *history_count;

        while (line[i] != '\0' && i < (SHELL_LINE_MAX - 1u)) {
            history[slot][i] = line[i];
            i++;
        }
        history[slot][i] = '\0';
        (*history_count)++;
        return;
    }

    for (uint32_t row = 1; row < SHELL_HISTORY_MAX; row++) {
        uint32_t col = 0;
        while (1) {
            history[row - 1u][col] = history[row][col];
            if (history[row][col] == '\0') {
                break;
            }
            col++;
        }
    }

    {
        uint32_t i = 0;
        while (line[i] != '\0' && i < (SHELL_LINE_MAX - 1u)) {
            history[SHELL_HISTORY_MAX - 1u][i] = line[i];
            i++;
        }
        history[SHELL_HISTORY_MAX - 1u][i] = '\0';
    }
}

static int streq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text != *prefix) {
            return 0;
        }
        text++;
        prefix++;
    }
    return 1;
}

static int parse_u32(const char *s, uint32_t *out) {
    uint32_t value = 0;

    if (*s == '\0') {
        return 0;
    }

    while (*s != '\0') {
        const char ch = *s;

        if (ch < '0' || ch > '9') {
            return 0;
        }

        if (value > (UINT32_MAX / 10u)) {
            return 0;
        }
        value = value * 10u + (uint32_t)(ch - '0');
        if (value < (uint32_t)(ch - '0')) {
            return 0;
        }
        s++;
    }

    *out = value;
    return 1;
}

static int parse_u32_auto(const char *s, uint32_t *out) {
    uint32_t value = 0;
    uint32_t base = 10u;

    if (*s == '\0') {
        return 0;
    }

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16u;
        s += 2;
        if (*s == '\0') {
            return 0;
        }
    }

    while (*s != '\0') {
        uint32_t digit;
        const char ch = *s;

        if (ch >= '0' && ch <= '9') {
            digit = (uint32_t)(ch - '0');
        } else if (base == 16u && ch >= 'a' && ch <= 'f') {
            digit = (uint32_t)(10 + (ch - 'a'));
        } else if (base == 16u && ch >= 'A' && ch <= 'F') {
            digit = (uint32_t)(10 + (ch - 'A'));
        } else {
            return 0;
        }

        if (digit >= base) {
            return 0;
        }
        if (value > (UINT32_MAX / base)) {
            return 0;
        }

        value = value * base + digit;
        if (value < digit) {
            return 0;
        }

        s++;
    }

    *out = value;
    return 1;
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ') {
        s++;
    }
    return s;
}

static const char *read_token(const char *s, char *token, uint32_t token_size) {
    uint32_t i = 0;

    s = skip_spaces(s);
    if (*s == '\0') {
        token[0] = '\0';
        return s;
    }

    while (*s != '\0' && *s != ' ') {
        if (i + 1u < token_size) {
            token[i++] = *s;
        }
        s++;
    }
    token[i] = '\0';
    return s;
}

static void sleep_seconds(uint32_t seconds) {
    uint32_t wait_ticks;
    const uint32_t hz = timer_get_hz();
    const uint32_t start = timer_get_ticks32();

    if (seconds > (UINT32_MAX / hz)) {
        wait_ticks = UINT32_MAX;
    } else {
        wait_ticks = seconds * hz;
    }

    while ((timer_get_ticks32() - start) < wait_ticks) {
        __asm__ volatile("hlt");
    }
}

static void sleep_milliseconds(uint32_t milliseconds) {
    uint32_t wait_ticks;
    const uint32_t hz = timer_get_hz();
    const uint32_t start = timer_get_ticks32();

    if (milliseconds > (UINT32_MAX / hz)) {
        wait_ticks = UINT32_MAX / 1000u;
    } else {
        wait_ticks = (milliseconds * hz) / 1000u;
    }
    if (wait_ticks == 0 && milliseconds > 0) {
        wait_ticks = 1;
    }

    while ((timer_get_ticks32() - start) < wait_ticks) {
        __asm__ volatile("hlt");
    }
}

static void reboot_system(void) {
    __asm__ volatile("cli");

    while ((inb(0x64) & 0x02u) != 0) {
    }

    outb(0x64, 0xFE);

    while (1) {
        __asm__ volatile("hlt");
    }
}

static void print_mem_info(const multiboot_info_t *mbi) {
    if ((mbi->flags & MULTIBOOT_INFO_MEM_FLAG) != 0) {
        vga_puts("mem lower KB: ");
        vga_put_dec32(mbi->mem_lower);
        vga_putc('\n');

        vga_puts("mem upper KB: ");
        vga_put_dec32(mbi->mem_upper);
        vga_putc('\n');
    } else {
        vga_puts("mem info not provided\n");
    }

    if ((mbi->flags & MULTIBOOT_INFO_MMAP_FLAG) != 0) {
        uint32_t entries = 0;
        uint32_t usable_entries = 0;
        const uint32_t mmap_end = mbi->mmap_addr + mbi->mmap_length;
        multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)mbi->mmap_addr;

        while ((uint32_t)entry < mmap_end) {
            entries++;
            if (entry->type == 1) {
                usable_entries++;
            }
            entry = (multiboot_mmap_entry_t *)((uint32_t)entry + entry->size + sizeof(entry->size));
        }

        vga_puts("mmap entries: ");
        vga_put_dec32(entries);
        vga_putc('\n');

        vga_puts("usable entries: ");
        vga_put_dec32(usable_entries);
        vga_putc('\n');
    } else {
        vga_puts("mmap not provided\n");
    }
}

static void print_mmap_regions(const multiboot_info_t *mbi) {
    if ((mbi->flags & MULTIBOOT_INFO_MMAP_FLAG) == 0) {
        vga_puts("mmap not provided\n");
        return;
    }

    const uint32_t mmap_end = mbi->mmap_addr + mbi->mmap_length;
    multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)mbi->mmap_addr;
    uint32_t shown = 0;

    while ((uint32_t)entry < mmap_end) {
        vga_puts("#");
        vga_put_dec32(shown);
        vga_puts(" base=");
        vga_put_hex64(entry->addr);
        vga_puts(" len=");
        vga_put_hex64(entry->len);
        vga_puts(" type=");
        vga_put_dec32(entry->type);
        vga_putc('\n');

        shown++;
        if (shown >= 8u) {
            vga_puts("...\n");
            break;
        }

        entry = (multiboot_mmap_entry_t *)((uint32_t)entry + entry->size + sizeof(entry->size));
    }
}

static void run_command(const char *line, const multiboot_info_t *mbi) {
    if (line[0] == '\0') {
        return;
    }

    if (streq(line, "help")) {
        vga_puts(
            "commands: help clear mem mmap pmm heap kmalloc syscall syscallfs tasks taskdemo ps "
            "spawnproc progs run sysrun send recv ipc upings selftest userprep usertest psx ls "
            "cat write "
            "aspace "
            "alloc allocn free vm vmmap "
            "vmunmap "
            "vmxlate "
            "vmfault uptime time ticks sleep sleepms reboot echo\n");
        vga_puts("editing: up/down for history\n");
        return;
    }

    if (streq(line, "clear")) {
        vga_clear();
        return;
    }

    if (streq(line, "mem")) {
        print_mem_info(mbi);
        return;
    }

    if (streq(line, "mmap")) {
        print_mmap_regions(mbi);
        return;
    }

    if (streq(line, "pmm")) {
        if (!pmm_ready()) {
            vga_puts("pmm: not initialized\n");
            return;
        }

        vga_puts("pmm total frames: ");
        vga_put_dec32(pmm_total_frames());
        vga_putc('\n');

        vga_puts("pmm used frames: ");
        vga_put_dec32(pmm_used_frames());
        vga_putc('\n');

        vga_puts("pmm free frames: ");
        vga_put_dec32(pmm_free_frames());
        vga_putc('\n');
        return;
    }

    if (streq(line, "heap")) {
        vga_puts("heap total bytes: ");
        vga_put_dec32(kheap_total_bytes());
        vga_putc('\n');

        vga_puts("heap used bytes: ");
        vga_put_dec32(kheap_used_bytes());
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "kmalloc ")) {
        uint32_t bytes;
        void *ptr;

        if (!parse_u32(line + 8, &bytes)) {
            vga_puts("usage: kmalloc <bytes>\n");
            return;
        }

        ptr = kmalloc(bytes);
        if (ptr == 0) {
            vga_puts("kmalloc failed\n");
            return;
        }

        vga_puts("kmalloc ptr: ");
        vga_put_hex32((uint32_t)ptr);
        vga_putc('\n');

        kfree(ptr);
        vga_puts("kfree done\n");
        return;
    }

    if (streq(line, "syscall")) {
        uint32_t ticks;
        uint32_t heap_total;
        uint32_t heap_used;

        __asm__ volatile("int $0x80" : "=a"(ticks) : "a"(0u) : "cc", "memory");
        __asm__ volatile("int $0x80" : "=a"(heap_total) : "a"(1u) : "cc", "memory");
        __asm__ volatile("int $0x80" : "=a"(heap_used) : "a"(2u) : "cc", "memory");

        vga_puts("sys ticks: ");
        vga_put_dec32(ticks);
        vga_putc('\n');

        vga_puts("sys heap total: ");
        vga_put_dec32(heap_total);
        vga_putc('\n');

        vga_puts("sys heap used: ");
        vga_put_dec32(heap_used);
        vga_putc('\n');
        return;
    }

    if (streq(line, "syscallfs")) {
        uint32_t task_count;
        uint32_t runnable_count;
        uint32_t current_task;
        uint32_t file_count;
        uint32_t write_ok;
        uint32_t read_len;
        static char name[] = "sys.txt";
        static char data[] = "hello-from-syscall";
        static char out[64];

        __asm__ volatile("int $0x80" : "=a"(task_count) : "a"(3u) : "cc", "memory");
        __asm__ volatile("int $0x80" : "=a"(runnable_count) : "a"(4u) : "cc", "memory");
        __asm__ volatile("int $0x80" : "=a"(current_task) : "a"(5u) : "cc", "memory");
        __asm__ volatile("int $0x80" : "=a"(file_count) : "a"(6u) : "cc", "memory");

        __asm__ volatile("int $0x80"
                         : "=a"(write_ok)
                         : "a"(8u), "b"(name), "c"(data)
                         : "cc", "memory");

        __asm__ volatile("int $0x80"
                         : "=a"(read_len)
                         : "a"(7u), "b"(name), "d"((uint32_t)sizeof(out)), "c"(out)
                         : "cc", "memory");

        vga_puts("sys tasks: ");
        vga_put_dec32(task_count);
        vga_puts(", runnable: ");
        vga_put_dec32(runnable_count);
        vga_puts(", current: ");
        vga_put_dec32(current_task);
        vga_putc('\n');

        vga_puts("sys files: ");
        vga_put_dec32(file_count);
        vga_putc('\n');

        vga_puts("sys write: ");
        vga_put_dec32(write_ok);
        vga_puts(", read_len: ");
        vga_put_dec32(read_len);
        vga_putc('\n');

        vga_puts("sys read: ");
        vga_puts(out);
        vga_putc('\n');
        return;
    }

    if (streq(line, "tasks")) {
        vga_puts("tasks total: ");
        vga_put_dec32(scheduler_task_count());
        vga_putc('\n');

        vga_puts("tasks runnable: ");
        vga_put_dec32(scheduler_runnable_count());
        vga_putc('\n');
        return;
    }

    if (streq(line, "taskdemo")) {
        int t1 = scheduler_create_task(demo_task, (void *)1u, "demo1");
        int t2 = scheduler_create_task(demo_task, (void *)2u, "demo2");

        if (t1 < 0 || t2 < 0) {
            vga_puts("taskdemo: create failed\n");
            return;
        }

        vga_puts("taskdemo: created\n");
        for (uint32_t i = 0; i < 40u; i++) {
            scheduler_yield();
        }
        vga_puts("taskdemo: done\n");
        return;
    }

    if (streq(line, "ps")) {
        const uint32_t count = process_count();
        vga_puts("processes: ");
        vga_put_dec32(count);
        vga_putc('\n');

        for (uint32_t i = 0; i < count; i++) {
            uint32_t pid;
            const char *name;
            if (process_pid_at(i, &pid, &name)) {
                vga_puts("pid=");
                vga_put_dec32(pid);
                vga_puts(" name=");
                vga_puts(name);
                vga_putc('\n');
            }
        }
        return;
    }

    if (streq(line, "spawnproc")) {
        int p1 = process_spawn_kernel("proc1", demo_task, (void *)0x101u);
        int p2 = process_spawn_kernel("proc2", demo_task, (void *)0x102u);

        if (p1 < 0 || p2 < 0) {
            vga_puts("spawnproc failed\n");
            return;
        }

        vga_puts("spawned pids: ");
        vga_put_dec32((uint32_t)p1);
        vga_puts(", ");
        vga_put_dec32((uint32_t)p2);
        vga_putc('\n');
        return;
    }

    if (streq(line, "psx")) {
        const uint32_t count = process_count();

        for (uint32_t i = 0; i < count; i++) {
            process_info_t info;

            if (!process_info_at(i, &info)) {
                continue;
            }

            vga_puts("pid=");
            vga_put_dec32(info.pid);
            vga_puts(" tid=");
            vga_put_dec32(info.tid);
            vga_puts(" kind=");
            vga_puts(info.is_kernel != 0 ? "kernel" : "user");
            vga_putc('\n');

            vga_puts("  cr3=");
            vga_put_hex32(info.cr3);
            vga_puts(" ucode=");
            vga_put_hex32(info.user_code_frame);
            vga_puts(" ustack=");
            vga_put_hex32(info.user_stack_frame);
            vga_puts(" uentry=");
            vga_put_hex32(info.user_entry);
            vga_putc('\n');

            vga_puts("  mask=");
            vga_put_hex32(info.syscall_mask_low);
            vga_putc('\n');
        }
        return;
    }

    if (streq(line, "progs")) {
        const uint32_t count = exec_program_count();
        vga_puts("programs: ");
        vga_put_dec32(count);
        vga_putc('\n');

        for (uint32_t i = 0; i < count; i++) {
            const char *name = exec_program_name_at(i);
            if (name != 0) {
                vga_puts("- ");
                vga_puts(name);
                vga_putc('\n');
            }
        }
        return;
    }

    if (starts_with(line, "run ")) {
        char name[40];
        int pid;

        read_token(line + 4, name, sizeof(name));
        if (name[0] == '\0') {
            vga_puts("usage: run <program>\n");
            return;
        }

        pid = exec_spawn(name);
        if (pid < 0) {
            vga_puts("run failed\n");
            return;
        }

        vga_puts("run pid: ");
        vga_put_dec32((uint32_t)pid);
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "sysrun ")) {
        char name[40];
        uint32_t pid;

        read_token(line + 7, name, sizeof(name));
        if (name[0] == '\0') {
            vga_puts("usage: sysrun <program>\n");
            return;
        }

        __asm__ volatile("int $0x80" : "=a"(pid) : "a"(12u), "b"(name) : "cc", "memory");
        if (pid == 0xFFFFFFFFu) {
            vga_puts("sysrun failed\n");
            return;
        }

        vga_puts("sysrun pid: ");
        vga_put_dec32(pid);
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "send ")) {
        char pid_tok[16];
        const char *p = line + 5;
        const char *msg;
        uint32_t to_pid;

        p = read_token(p, pid_tok, sizeof(pid_tok));
        msg = skip_spaces(p);

        if (!parse_u32(pid_tok, &to_pid) || msg[0] == '\0') {
            vga_puts("usage: send <pid> <message>\n");
            return;
        }

        if (!ipc_send(process_current_pid(), to_pid, msg)) {
            vga_puts("send failed\n");
            return;
        }

        vga_puts("sent\n");
        return;
    }

    if (streq(line, "recv")) {
        char msg[64];
        uint32_t from_pid = 0;
        const int received = ipc_recv(process_current_pid(), msg, sizeof(msg), &from_pid);

        if (received < 0) {
            vga_puts("recv failed\n");
            return;
        }

        if (received == 0) {
            vga_puts("no messages\n");
            return;
        }

        vga_puts("from ");
        vga_put_dec32(from_pid);
        vga_puts(": ");
        vga_puts(msg);
        vga_putc('\n');
        return;
    }

    if (streq(line, "ipc")) {
        vga_puts("ipc pending: ");
        vga_put_dec32(ipc_pending_count());
        vga_putc('\n');
        return;
    }

    if (streq(line, "upings")) {
        uint32_t count;

        __asm__ volatile("int $0x80" : "=a"(count) : "a"(18u) : "cc", "memory");
        vga_puts("user ping count: ");
        vga_put_dec32(count);
        vga_putc('\n');
        return;
    }

    if (streq(line, "selftest")) {
        kernel_run_selftest();
        vga_puts("selftest done (see serial log)\n");
        return;
    }

    if (streq(line, "aspace")) {
        vga_puts("cr3 current: ");
        vga_put_hex32(paging_current_directory_phys());
        vga_putc('\n');

        vga_puts("cr3 kernel: ");
        vga_put_hex32(paging_kernel_directory_phys());
        vga_putc('\n');
        return;
    }

    if (streq(line, "userprep")) {
        if (!usermode_prepare()) {
            vga_puts("userprep failed\n");
            return;
        }

        vga_puts("userprep ok\n");
        return;
    }

    if (streq(line, "usertest")) {
        if (!usermode_is_prepared()) {
            vga_puts("run userprep first\n");
            return;
        }

        vga_puts("entering user mode test\n");
        usermode_enter_test();
        vga_puts("user mode test returned\n");
        return;
    }

    if (streq(line, "ls")) {
        const uint32_t count = vfs_file_count();
        vga_puts("files: ");
        vga_put_dec32(count);
        vga_putc('\n');

        for (uint32_t i = 0; i < count; i++) {
            const char *name = vfs_file_name_at(i);
            if (name != 0) {
                vga_puts("- ");
                vga_puts(name);
                vga_putc('\n');
            }
        }
        return;
    }

    if (starts_with(line, "cat ")) {
        char name[40];
        const char *content;

        read_token(line + 4, name, sizeof(name));
        if (name[0] == '\0') {
            vga_puts("usage: cat <name>\n");
            return;
        }

        content = vfs_read_file(name);
        if (content == 0) {
            vga_puts("cat: file not found\n");
            return;
        }

        vga_puts(content);
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "write ")) {
        char name[40];
        const char *p = line + 6;
        const char *text;

        p = read_token(p, name, sizeof(name));
        text = skip_spaces(p);

        if (name[0] == '\0' || text[0] == '\0') {
            vga_puts("usage: write <name> <text>\n");
            return;
        }

        if (!vfs_write_file(name, text)) {
            vga_puts("write failed\n");
            return;
        }

        vga_puts("write ok\n");
        return;
    }

    if (streq(line, "vm")) {
        vga_puts("paging: ");
        vga_puts(paging_enabled() ? "enabled\n" : "disabled\n");
        return;
    }

    if (starts_with(line, "vmmap ")) {
        char tok1[32];
        char tok2[32];
        const char *p = line + 6;
        uint32_t virt;
        uint32_t phys;

        p = read_token(p, tok1, sizeof(tok1));
        p = read_token(p, tok2, sizeof(tok2));

        if (!parse_u32_auto(tok1, &virt) || !parse_u32_auto(tok2, &phys)) {
            vga_puts("usage: vmmap <virt> <phys>\n");
            return;
        }

        if (!paging_map_page(virt, phys, PAGING_FLAG_PRESENT | PAGING_FLAG_RW)) {
            vga_puts("vmmap failed\n");
            return;
        }

        vga_puts("mapped ");
        vga_put_hex32(virt & 0xFFFFF000u);
        vga_puts(" -> ");
        vga_put_hex32(phys & 0xFFFFF000u);
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "vmunmap ")) {
        uint32_t virt;

        if (!parse_u32_auto(line + 8, &virt)) {
            vga_puts("usage: vmunmap <virt>\n");
            return;
        }

        if (!paging_unmap_page(virt)) {
            vga_puts("vmunmap: not mapped\n");
            return;
        }

        vga_puts("unmapped ");
        vga_put_hex32(virt & 0xFFFFF000u);
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "vmxlate ")) {
        uint32_t virt;
        uint32_t phys;

        if (!parse_u32_auto(line + 8, &virt)) {
            vga_puts("usage: vmxlate <virt>\n");
            return;
        }

        if (!paging_translate(virt, &phys)) {
            vga_puts("vmxlate: unmapped\n");
            return;
        }

        vga_puts("phys: ");
        vga_put_hex32(phys);
        vga_putc('\n');
        return;
    }

    if (streq(line, "vmfault")) {
        volatile uint32_t *bad = (volatile uint32_t *)0x50000000u;
        vga_puts("triggering page fault...\n");
        *bad = 0xDEADBEEFu;
        vga_puts("unexpected: write succeeded\n");
        return;
    }

    if (streq(line, "alloc")) {
        const uint32_t frame = pmm_alloc_frame();
        if (frame == 0) {
            vga_puts("alloc failed\n");
            return;
        }

        vga_puts("frame: ");
        vga_put_hex32(frame);
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "allocn ")) {
        uint32_t count;
        uint32_t ok = 0;

        if (!parse_u32(line + 7, &count)) {
            vga_puts("usage: allocn <count>\n");
            return;
        }

        for (uint32_t i = 0; i < count; i++) {
            if (pmm_alloc_frame() == 0) {
                break;
            }
            ok++;
        }

        vga_puts("allocated frames: ");
        vga_put_dec32(ok);
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "free ")) {
        uint32_t frame;

        if (!parse_u32_auto(line + 5, &frame)) {
            vga_puts("usage: free <frame_addr>\n");
            return;
        }

        if (!pmm_free_frame(frame)) {
            vga_puts("free failed\n");
            return;
        }

        vga_puts("freed frame: ");
        vga_put_hex32(frame & 0xFFFFF000u);
        vga_putc('\n');
        return;
    }

    if (streq(line, "uptime")) {
        vga_puts("uptime: ");
        vga_put_dec32(timer_get_uptime_seconds());
        vga_puts(" s\n");
        return;
    }

    if (streq(line, "time")) {
        vga_puts("hz: ");
        vga_put_dec32(timer_get_hz());
        vga_puts(", ticks: ");
        vga_put_dec32(timer_get_ticks32());
        vga_puts(", uptime: ");
        vga_put_dec32(timer_get_uptime_seconds());
        vga_puts(" s\n");
        return;
    }

    if (streq(line, "ticks")) {
        vga_puts("ticks: ");
        vga_put_dec32(timer_get_ticks32());
        vga_putc('\n');
        return;
    }

    if (starts_with(line, "sleep ")) {
        uint32_t seconds;

        if (!parse_u32(line + 6, &seconds)) {
            vga_puts("usage: sleep <seconds>\n");
            return;
        }

        sleep_seconds(seconds);
        return;
    }

    if (starts_with(line, "sleepms ")) {
        uint32_t milliseconds;

        if (!parse_u32(line + 8, &milliseconds)) {
            vga_puts("usage: sleepms <milliseconds>\n");
            return;
        }

        sleep_milliseconds(milliseconds);
        return;
    }

    if (streq(line, "reboot")) {
        reboot_system();
        return;
    }

    if (starts_with(line, "echo ")) {
        vga_puts(line + 5);
        vga_putc('\n');
        return;
    }

    vga_puts("unknown command: ");
    vga_puts(line);
    vga_putc('\n');
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    extern uint8_t __kernel_end;
    uint32_t kernel_stack_top;

    vga_clear();
    vga_puts("Mars OS\n");
    serial_init();
    serial_puts("Mars OS boot\n");

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_puts("error: invalid multiboot magic\n");
        serial_puts("error: invalid multiboot magic\n");
        while (1) {
            __asm__ volatile("hlt");
        }
    }

    multiboot_info_t *mbi = (multiboot_info_t *)multiboot_info_addr;

    vga_puts("boot ok, mbi=");
    vga_put_hex32(multiboot_info_addr);
    vga_putc('\n');
    serial_puts("mbi=");
    serial_put_hex32(multiboot_info_addr);
    serial_puts("\n");

    __asm__ volatile("mov %%esp, %0" : "=r"(kernel_stack_top));
    gdt_init(kernel_stack_top);

    interrupts_init();
    timer_init(100);
    pmm_init(mbi, (uint32_t)&__kernel_end);
    paging_init();
    if (!kheap_init()) {
        vga_puts("kheap: init failed\n");
        serial_puts("kheap: init failed\n");
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    scheduler_init();
    if (!process_init()) {
        vga_puts("process: init failed\n");
        serial_puts("process: init failed\n");
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    if (!exec_init()) {
        vga_puts("exec: init failed\n");
        serial_puts("exec: init failed\n");
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    if (!ipc_init()) {
        vga_puts("ipc: init failed\n");
        serial_puts("ipc: init failed\n");
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    if (usermode_prepare()) {
        vga_puts("usermode: prepared\n");
        serial_puts("usermode: prepared\n");
    } else {
        vga_puts("usermode: unavailable\n");
        serial_puts("usermode: unavailable\n");
    }
    if (!vfs_init()) {
        vga_puts("vfs: init failed\n");
        serial_puts("vfs: init failed\n");
        while (1) {
            __asm__ volatile("hlt");
        }
    }
    vga_puts("interrupts: enabled (keyboard irq)\n");
    serial_puts("interrupts enabled\n");
    if (paging_enabled()) {
        vga_puts("paging: enabled\n");
        serial_puts("paging: enabled\n");
    } else {
        vga_puts("paging: disabled\n");
        serial_puts("paging: disabled\n");
    }
    if (pmm_ready()) {
        vga_puts("pmm: ready\n");
        serial_puts("pmm: ready\n");
        vga_puts("pmm total frames: ");
        vga_put_dec32(pmm_total_frames());
        vga_putc('\n');
    } else {
        vga_puts("pmm: unavailable\n");
        serial_puts("pmm: unavailable\n");
    }
    vga_puts("kheap: ready\n");
    serial_puts("kheap: ready\n");
    vga_puts("scheduler: ready\n");
    serial_puts("scheduler: ready\n");
    vga_puts("process: ready\n");
    serial_puts("process: ready\n");
    vga_puts("exec: ready\n");
    serial_puts("exec: ready\n");
    vga_puts("ipc: ready\n");
    serial_puts("ipc: ready\n");
    vga_puts("vfs: ready\n");
    serial_puts("vfs: ready\n");
    kernel_run_selftest();
    vga_puts("type 'help'\n\n");

    char line[SHELL_LINE_MAX];
    char history[SHELL_HISTORY_MAX][SHELL_LINE_MAX];
    uint32_t history_count = 0;
    uint32_t line_len = 0;

    while (1) {
        char c;

        vga_puts("mars> ");
        line_len = 0;
        uint32_t cursor_pos = 0;
        line[0] = '\0';
        int32_t history_cursor = -1;

        while (1) {
            scheduler_run_pending();
            process_reap();
            c = keyboard_try_getchar();
            if (c == 0) {
                __asm__ volatile("hlt");
                continue;
            }

            if (c == KEY_SPECIAL_UP) {
                if (history_count == 0) {
                    continue;
                }

                if (history_cursor < 0) {
                    history_cursor = (int32_t)history_count - 1;
                } else if (history_cursor > 0) {
                    history_cursor--;
                }

                cursor_pos = shell_move_cursor_to_end(line, cursor_pos, line_len);
                shell_replace_line(line, &line_len, history[history_cursor]);
                cursor_pos = line_len;
                continue;
            }

            if (c == KEY_SPECIAL_DOWN) {
                if (history_count == 0 || history_cursor < 0) {
                    continue;
                }

                history_cursor++;
                if ((uint32_t)history_cursor >= history_count) {
                    history_cursor = -1;
                    cursor_pos = shell_move_cursor_to_end(line, cursor_pos, line_len);
                    shell_replace_line(line, &line_len, "");
                } else {
                    cursor_pos = shell_move_cursor_to_end(line, cursor_pos, line_len);
                    shell_replace_line(line, &line_len, history[history_cursor]);
                }
                cursor_pos = line_len;
                continue;
            }

            if (c == KEY_SPECIAL_LEFT) {
                if (cursor_pos > 0) {
                    cursor_pos--;
                    vga_backspace();
                }
                continue;
            }

            if (c == KEY_SPECIAL_RIGHT) {
                if (cursor_pos < line_len) {
                    vga_putc(line[cursor_pos]);
                    cursor_pos++;
                }
                continue;
            }

            if (c == KEY_SPECIAL_DELETE) {
                if (cursor_pos < line_len) {
                    for (uint32_t i = cursor_pos; i + 1u < line_len; i++) {
                        line[i] = line[i + 1u];
                    }
                    line_len--;
                    line[line_len] = '\0';
                    shell_redraw_line(line, line_len, cursor_pos);
                }
                continue;
            }

            if (c == '\n') {
                line[line_len] = '\0';
                vga_putc('\n');
                break;
            }

            if (c == '\b') {
                if (cursor_pos > 0) {
                    if (cursor_pos == line_len) {
                        line_len--;
                        cursor_pos--;
                        line[line_len] = '\0';
                        vga_backspace();
                    } else {
                        const uint32_t remove_at = cursor_pos - 1u;
                        for (uint32_t i = remove_at; i + 1u < line_len; i++) {
                            line[i] = line[i + 1u];
                        }
                        line_len--;
                        cursor_pos--;
                        line[line_len] = '\0';
                        shell_redraw_line(line, line_len, cursor_pos);
                    }
                }
                continue;
            }

            if (line_len < (SHELL_LINE_MAX - 1u)) {
                if (cursor_pos == line_len) {
                    line[line_len++] = c;
                    cursor_pos++;
                    line[line_len] = '\0';
                    vga_putc(c);
                } else {
                    for (uint32_t i = line_len; i > cursor_pos; i--) {
                        line[i] = line[i - 1u];
                    }
                    line[cursor_pos] = c;
                    line_len++;
                    cursor_pos++;
                    line[line_len] = '\0';
                    shell_redraw_line(line, line_len, cursor_pos);
                }
            }
        }

        shell_store_history(history, &history_count, line);
        run_command(line, mbi);
    }
}