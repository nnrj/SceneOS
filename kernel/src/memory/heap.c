#include <scene/heap.h>
#include <scene/paging.h>
#include <scene/physical.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_HEAP_BASE UINT64_C(0xffffc00000000000)
#define SCENE_HEAP_CAPACITY (UINT64_C(16) * 1024U * 1024U)
#define SCENE_HEAP_GROWTH_PAGES 4U
#define SCENE_HEAP_BLOCK_MAGIC UINT64_C(0x53434e424c4f434b)
#define SCENE_HEAP_PREFIX_MAGIC UINT64_C(0x53434e414c4c4f43)
#define SCENE_HEAP_MINIMUM_SPLIT 32U

struct heap_block {
    uint64_t magic;
    size_t size;
    struct heap_block *previous;
    struct heap_block *next;
    bool free;
};

struct allocation_prefix {
    uint64_t magic;
    struct heap_block *block;
};

static struct heap_block *first_block;
static struct heap_block *last_block;
static uint64_t heap_committed_end;
static size_t allocated_bytes;
static bool heap_ready;

static bool is_power_of_two(size_t value) {
    return value != 0 && (value & (value - 1U)) == 0;
}

static bool align_up(uint64_t value, uint64_t alignment, uint64_t *result) {
    const uint64_t mask = alignment - 1U;
    if (value > UINT64_MAX - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

static uint8_t *block_payload(struct heap_block *block) {
    return (uint8_t *)(block + 1);
}

static void append_committed_space(uint64_t start, size_t byte_count) {
    if (first_block == 0) {
        first_block = (struct heap_block *)(uintptr_t)start;
        *first_block = (struct heap_block){
            .magic = SCENE_HEAP_BLOCK_MAGIC,
            .size = byte_count - sizeof(struct heap_block),
            .previous = 0,
            .next = 0,
            .free = true
        };
        last_block = first_block;
        return;
    }

    uint8_t *last_end =
        block_payload(last_block) + last_block->size;
    if (last_block->free &&
        (uint64_t)(uintptr_t)last_end == start) {
        last_block->size += byte_count;
        return;
    }

    struct heap_block *created =
        (struct heap_block *)(uintptr_t)start;
    *created = (struct heap_block){
        .magic = SCENE_HEAP_BLOCK_MAGIC,
        .size = byte_count - sizeof(struct heap_block),
        .previous = last_block,
        .next = 0,
        .free = true
    };
    last_block->next = created;
    last_block = created;
}

static bool grow_heap(size_t minimum_bytes) {
    if ((uint64_t)minimum_bytes > SCENE_HEAP_CAPACITY) {
        return false;
    }

    size_t requested_pages =
        (minimum_bytes + (size_t)SCENE_PAGE_SIZE - 1U) /
        (size_t)SCENE_PAGE_SIZE;
    if (requested_pages < SCENE_HEAP_GROWTH_PAGES) {
        requested_pages = SCENE_HEAP_GROWTH_PAGES;
    }
    const uint64_t committed_bytes =
        heap_committed_end - SCENE_HEAP_BASE;
    const uint64_t remaining_bytes =
        SCENE_HEAP_CAPACITY - committed_bytes;
    const size_t remaining_pages =
        (size_t)(remaining_bytes / SCENE_PAGE_SIZE);
    if (requested_pages > remaining_pages) {
        requested_pages = remaining_pages;
    }
    if (requested_pages == 0) {
        return false;
    }

    const uint64_t growth_start = heap_committed_end;
    size_t mapped_pages = 0;
    while (mapped_pages < requested_pages) {
        if (!scene_paging_map_new(
            heap_committed_end,
            SCENE_PAGE_WRITABLE
        )) {
            break;
        }
        heap_committed_end += SCENE_PAGE_SIZE;
        ++mapped_pages;
    }
    if (mapped_pages == 0) {
        return false;
    }
    append_committed_space(
        growth_start,
        mapped_pages * (size_t)SCENE_PAGE_SIZE
    );
    return mapped_pages == requested_pages;
}

static bool block_layout(
    struct heap_block *block,
    size_t size,
    size_t alignment,
    uint64_t *user_address,
    size_t *consumed_bytes
) {
    const uint64_t payload =
        (uint64_t)(uintptr_t)block_payload(block);
    if (payload > UINT64_MAX - sizeof(struct allocation_prefix)) {
        return false;
    }
    if (!align_up(
        payload + sizeof(struct allocation_prefix),
        (uint64_t)alignment,
        user_address
    ) || (uint64_t)size > UINT64_MAX - *user_address) {
        return false;
    }

    const uint64_t raw_consumed = *user_address + (uint64_t)size - payload;
    uint64_t aligned_consumed;
    if (!align_up(
        raw_consumed,
        (uint64_t)_Alignof(struct heap_block),
        &aligned_consumed
    ) || aligned_consumed > SIZE_MAX) {
        return false;
    }
    *consumed_bytes = (size_t)aligned_consumed;
    return *consumed_bytes <= block->size;
}

static void split_block(struct heap_block *block, size_t consumed_bytes) {
    const size_t remainder = block->size - consumed_bytes;
    if (remainder <
        sizeof(struct heap_block) + SCENE_HEAP_MINIMUM_SPLIT) {
        return;
    }

    struct heap_block *tail = (struct heap_block *)(
        block_payload(block) + consumed_bytes
    );
    *tail = (struct heap_block){
        .magic = SCENE_HEAP_BLOCK_MAGIC,
        .size = remainder - sizeof(struct heap_block),
        .previous = block,
        .next = block->next,
        .free = true
    };
    if (tail->next != 0) {
        tail->next->previous = tail;
    } else {
        last_block = tail;
    }
    block->next = tail;
    block->size = consumed_bytes;
}

static struct heap_block *merge_with_next(struct heap_block *block) {
    struct heap_block *next = block->next;
    if (next == 0 || !next->free ||
        next->magic != SCENE_HEAP_BLOCK_MAGIC) {
        return block;
    }

    block->size += sizeof(struct heap_block) + next->size;
    block->next = next->next;
    if (block->next != 0) {
        block->next->previous = block;
    } else {
        last_block = block;
    }
    next->magic = 0;
    return block;
}

bool scene_heap_init(void) {
    first_block = 0;
    last_block = 0;
    heap_committed_end = SCENE_HEAP_BASE;
    allocated_bytes = 0;
    heap_ready = grow_heap((size_t)SCENE_PAGE_SIZE);
    return heap_ready;
}

void *scene_heap_allocate(size_t size, size_t alignment) {
    if (!heap_ready || size == 0 || !is_power_of_two(alignment)) {
        return 0;
    }

    for (size_t attempt = 0; attempt < 2U; ++attempt) {
        for (struct heap_block *block = first_block;
            block != 0;
            block = block->next) {
            if (!block->free || block->magic != SCENE_HEAP_BLOCK_MAGIC) {
                continue;
            }

            uint64_t user_address;
            size_t consumed_bytes;
            if (!block_layout(
                block,
                size,
                alignment,
                &user_address,
                &consumed_bytes
            )) {
                continue;
            }
            split_block(block, consumed_bytes);
            block->free = false;
            allocated_bytes += block->size;

            struct allocation_prefix *prefix =
                (struct allocation_prefix *)(uintptr_t)(
                    user_address - sizeof(struct allocation_prefix)
                );
            *prefix = (struct allocation_prefix){
                .magic = SCENE_HEAP_PREFIX_MAGIC,
                .block = block
            };
            return (void *)(uintptr_t)user_address;
        }

        if (size > SIZE_MAX - alignment ||
            size + alignment >
                SIZE_MAX - sizeof(struct allocation_prefix) -
                sizeof(struct heap_block) ||
            !grow_heap(
                size + alignment +
                sizeof(struct allocation_prefix) +
                sizeof(struct heap_block)
            )) {
            return 0;
        }
    }
    return 0;
}

bool scene_heap_free(void *address) {
    if (!heap_ready || address == 0) {
        return false;
    }
    const uint64_t user_address = (uint64_t)(uintptr_t)address;
    if (user_address < SCENE_HEAP_BASE + sizeof(struct allocation_prefix) ||
        user_address >= heap_committed_end) {
        return false;
    }

    struct allocation_prefix *prefix =
        (struct allocation_prefix *)(uintptr_t)(
            user_address - sizeof(struct allocation_prefix)
        );
    if (prefix->magic != SCENE_HEAP_PREFIX_MAGIC ||
        prefix->block == 0 ||
        prefix->block->magic != SCENE_HEAP_BLOCK_MAGIC ||
        prefix->block->free) {
        return false;
    }

    struct heap_block *block = prefix->block;
    prefix->magic = 0;
    allocated_bytes -= block->size;
    block->free = true;
    block = merge_with_next(block);
    if (block->previous != 0 && block->previous->free) {
        block = merge_with_next(block->previous);
    }
    (void)block;
    return true;
}

size_t scene_heap_committed_pages(void) {
    return (size_t)(
        (heap_committed_end - SCENE_HEAP_BASE) / SCENE_PAGE_SIZE
    );
}

size_t scene_heap_allocated_bytes(void) {
    return allocated_bytes;
}
