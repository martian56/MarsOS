#include <stddef.h>
#include <stdint.h>

#include "kheap.h"
#include "paging.h"
#include "pmm.h"

#define KHEAP_START 0xD0000000u
#define KHEAP_INITIAL_PAGES 16u
#define KHEAP_GROW_PAGES 8u

typedef struct heap_block {
    uint32_t size;
    uint32_t free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

static heap_block_t *heap_head;
static uint32_t heap_end;
static uint32_t heap_total;
static uint32_t heap_used;
static int heap_ready;

static uint32_t align_up_8(uint32_t value) { return (value + 7u) & ~7u; }

static void heap_split_block(heap_block_t *block, uint32_t size) {
    heap_block_t *new_block;

    if (block->size <= size + sizeof(heap_block_t) + 8u) {
        return;
    }

    new_block = (heap_block_t *)((uint8_t *)block + sizeof(heap_block_t) + size);
    new_block->size = block->size - size - (uint32_t)sizeof(heap_block_t);
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;

    if (block->next != 0) {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = size;
}

static void heap_coalesce(heap_block_t *block) {
    if (block->next != 0 && block->next->free != 0) {
        heap_block_t *next = block->next;
        block->size += (uint32_t)sizeof(heap_block_t) + next->size;
        block->next = next->next;
        if (block->next != 0) {
            block->next->prev = block;
        }
    }

    if (block->prev != 0 && block->prev->free != 0) {
        heap_block_t *prev = block->prev;
        prev->size += (uint32_t)sizeof(heap_block_t) + block->size;
        prev->next = block->next;
        if (block->next != 0) {
            block->next->prev = prev;
        }
    }
}

static int heap_map_more(uint32_t page_count) {
    for (uint32_t i = 0; i < page_count; i++) {
        const uint32_t phys = pmm_alloc_frame();
        const uint32_t virt = heap_end;
        heap_block_t *tail;

        if (phys == 0) {
            return 0;
        }

        if (!paging_map_page(virt, phys, PAGING_FLAG_PRESENT | PAGING_FLAG_RW)) {
            return 0;
        }

        heap_end += 4096u;
        heap_total += 4096u;

        tail = heap_head;
        while (tail->next != 0) {
            tail = tail->next;
        }

        if (tail->free != 0) {
            tail->size += 4096u;
        } else {
            heap_block_t *new_block =
                (heap_block_t *)((uint8_t *)tail + sizeof(heap_block_t) + tail->size);
            new_block->size = 4096u - (uint32_t)sizeof(heap_block_t);
            new_block->free = 1;
            new_block->next = 0;
            new_block->prev = tail;
            tail->next = new_block;
        }
    }

    return 1;
}

int kheap_init(void) {
    for (uint32_t i = 0; i < KHEAP_INITIAL_PAGES; i++) {
        const uint32_t phys = pmm_alloc_frame();
        const uint32_t virt = KHEAP_START + (i * 4096u);

        if (phys == 0) {
            return 0;
        }

        if (!paging_map_page(virt, phys, PAGING_FLAG_PRESENT | PAGING_FLAG_RW)) {
            return 0;
        }
    }

    heap_head = (heap_block_t *)KHEAP_START;
    heap_head->size = (KHEAP_INITIAL_PAGES * 4096u) - (uint32_t)sizeof(heap_block_t);
    heap_head->free = 1;
    heap_head->next = 0;
    heap_head->prev = 0;

    heap_end = KHEAP_START + (KHEAP_INITIAL_PAGES * 4096u);
    heap_total = KHEAP_INITIAL_PAGES * 4096u;
    heap_used = 0;
    heap_ready = 1;

    return 1;
}

void *kmalloc(size_t size) {
    heap_block_t *block;
    uint32_t want;

    if (!heap_ready || size == 0u) {
        return 0;
    }

    if (size > 0x7FFFFFFFu) {
        return 0;
    }

    want = align_up_8((uint32_t)size);

    while (1) {
        block = heap_head;
        while (block != 0) {
            if (block->free != 0 && block->size >= want) {
                heap_split_block(block, want);
                block->free = 0;
                heap_used += block->size;
                return (void *)((uint8_t *)block + sizeof(heap_block_t));
            }
            block = block->next;
        }

        if (!heap_map_more(KHEAP_GROW_PAGES)) {
            return 0;
        }
    }
}

void kfree(void *ptr) {
    heap_block_t *block;

    if (!heap_ready || ptr == 0) {
        return;
    }

    if ((uint32_t)ptr < (KHEAP_START + sizeof(heap_block_t)) || (uint32_t)ptr >= heap_end) {
        return;
    }

    block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (block->free != 0) {
        return;
    }

    block->free = 1;
    if (heap_used >= block->size) {
        heap_used -= block->size;
    } else {
        heap_used = 0;
    }

    heap_coalesce(block);
}

uint32_t kheap_total_bytes(void) { return heap_total; }

uint32_t kheap_used_bytes(void) { return heap_used; }
