#include <stdint.h>

#include "interrupts.h"
#include "io.h"
#include "keyboard.h"
#include "multiboot.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "timer.h"
#include "vga.h"

#define SHELL_LINE_MAX 128u
#define SHELL_HISTORY_MAX 8u

static int streq(const char *a, const char *b);

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
        vga_puts("commands: help clear mem mmap pmm alloc allocn free vm vmmap vmunmap vmxlate "
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
    interrupts_init();
    timer_init(100);
    pmm_init(mbi, (uint32_t)&__kernel_end);
    paging_init();
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