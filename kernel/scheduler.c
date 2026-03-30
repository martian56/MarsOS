#include <stdint.h>

#include "kheap.h"
#include "process.h"
#include "scheduler.h"

#define MAX_TASKS 16u
#define TASK_STACK_SIZE 8192u
#define SCHED_QUANTUM_TICKS 10u

typedef enum {
    TASK_UNUSED = 0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_ZOMBIE,
} task_state_t;

typedef struct {
    uint32_t *sp;
    uint8_t *stack_base;
    task_entry_t entry;
    void *arg;
    task_state_t state;
    char name[16];
} task_t;

extern void context_switch(uint32_t **old_sp, uint32_t *new_sp);

static task_t tasks[MAX_TASKS];
static int scheduler_started;
static int current_task;
static volatile uint32_t tick_since_switch;
static volatile int preempt_pending;

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

static int scheduler_pick_next(void) {
    if (!scheduler_started) {
        return -1;
    }

    for (uint32_t i = 1; i < MAX_TASKS; i++) {
        const uint32_t idx = ((uint32_t)current_task + i) % MAX_TASKS;
        if (tasks[idx].state == TASK_RUNNABLE) {
            return (int)idx;
        }
    }

    /*
     * If no runnable task exists but task 0 is still alive, force it as fallback
     * so an exiting task does not deadlock in the scheduler.
     */
    if (current_task != 0 && tasks[0].state != TASK_UNUSED && tasks[0].state != TASK_ZOMBIE) {
        return 0;
    }

    return -1;
}

static void scheduler_task_exit(void) {
    tasks[current_task].state = TASK_ZOMBIE;

    while (1) {
        int next = scheduler_pick_next();
        if (next < 0) {
            __asm__ volatile("hlt");
            continue;
        }

        {
            int prev = current_task;
            current_task = next;
            tasks[current_task].state = TASK_RUNNING;
            process_activate_tid((uint32_t)current_task);
            context_switch(&tasks[prev].sp, tasks[current_task].sp);
        }
    }
}

static void scheduler_task_bootstrap(void) {
    task_entry_t entry = tasks[current_task].entry;
    void *arg = tasks[current_task].arg;

    if (entry != 0) {
        entry(arg);
    }

    scheduler_task_exit();
}

void scheduler_exit_current(void) {
    if (!scheduler_started) {
        return;
    }

    if (current_task == 0) {
        return;
    }

    scheduler_task_exit();
}

void scheduler_init(void) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tasks[i].sp = 0;
        tasks[i].stack_base = 0;
        tasks[i].entry = 0;
        tasks[i].arg = 0;
        tasks[i].state = TASK_UNUSED;
        tasks[i].name[0] = '\0';
    }

    tasks[0].state = TASK_RUNNING;
    str_copy(tasks[0].name, "kernel", sizeof(tasks[0].name));
    current_task = 0;
    tick_since_switch = 0;
    preempt_pending = 0;
    scheduler_started = 1;
}

int scheduler_create_task(task_entry_t entry, void *arg, const char *name) {
    uint32_t idx;

    if (!scheduler_started || entry == 0) {
        return -1;
    }

    for (idx = 1; idx < MAX_TASKS; idx++) {
        if (tasks[idx].state == TASK_UNUSED || tasks[idx].state == TASK_ZOMBIE) {
            uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
            uint32_t *sp;

            if (stack == 0) {
                return -1;
            }

            if (tasks[idx].state == TASK_ZOMBIE && tasks[idx].stack_base != 0) {
                kfree(tasks[idx].stack_base);
            }

            sp = (uint32_t *)(stack + TASK_STACK_SIZE);
            sp--;
            *sp = (uint32_t)scheduler_task_bootstrap; /* return address */
            sp--;
            *sp = 0u; /* ebp */
            sp--;
            *sp = 0u; /* ebx */
            sp--;
            *sp = 0u; /* esi */
            sp--;
            *sp = 0u; /* edi */

            tasks[idx].stack_base = stack;
            tasks[idx].sp = sp;
            tasks[idx].entry = entry;
            tasks[idx].arg = arg;
            tasks[idx].state = TASK_RUNNABLE;
            str_copy(tasks[idx].name, name != 0 ? name : "task", sizeof(tasks[idx].name));
            return (int)idx;
        }
    }

    return -1;
}

void scheduler_yield(void) {
    int next;
    int prev;

    if (!scheduler_started) {
        return;
    }

    next = scheduler_pick_next();
    if (next < 0) {
        return;
    }

    prev = current_task;
    if (tasks[prev].state == TASK_RUNNING) {
        tasks[prev].state = TASK_RUNNABLE;
    }

    current_task = next;
    tasks[current_task].state = TASK_RUNNING;
    tick_since_switch = 0;
    preempt_pending = 0;

    process_activate_tid((uint32_t)current_task);
    context_switch(&tasks[prev].sp, tasks[current_task].sp);
}

void scheduler_on_tick(void) {
    if (!scheduler_started) {
        return;
    }

    tick_since_switch++;
    if (tick_since_switch >= SCHED_QUANTUM_TICKS) {
        preempt_pending = 1;
    }
}

void scheduler_run_pending(void) {
    if (preempt_pending != 0) {
        scheduler_yield();
    }
}

uint32_t scheduler_task_count(void) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED) {
            count++;
        }
    }

    return count;
}

uint32_t scheduler_runnable_count(void) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_RUNNABLE || tasks[i].state == TASK_RUNNING) {
            count++;
        }
    }

    return count;
}

uint32_t scheduler_current_task_id(void) { return (uint32_t)current_task; }

int scheduler_task_is_zombie(uint32_t tid) {
    if (tid >= MAX_TASKS) {
        return 0;
    }

    return tasks[tid].state == TASK_ZOMBIE;
}

int scheduler_reap_task(uint32_t tid) {
    if (tid == 0 || tid >= MAX_TASKS) {
        return 0;
    }

    if (tasks[tid].state != TASK_ZOMBIE) {
        return 0;
    }

    if (tasks[tid].stack_base != 0) {
        kfree(tasks[tid].stack_base);
    }

    tasks[tid].sp = 0;
    tasks[tid].stack_base = 0;
    tasks[tid].entry = 0;
    tasks[tid].arg = 0;
    tasks[tid].state = TASK_UNUSED;
    tasks[tid].name[0] = '\0';
    return 1;
}
