#include <scene/physical.h>

#include <stddef.h>
#include <stdint.h>

#define SCENE_LOW_MEMORY_RESERVE UINT64_C(0x100000)
#define BITS_PER_BYTE 8U

struct allocator_region {
    uint64_t base;
    size_t page_count;
    size_t metadata_pages;
    size_t allocation_hint;
    volatile uint8_t *bitmap;
};

static struct allocator_region regions[SCENE_BOOT_MAX_USABLE_REGIONS];
static size_t region_count;
static size_t allocation_region_hint;
static uint64_t free_pages;
static uint64_t metadata_pages;

static uint64_t align_up(uint64_t value) {
    const uint64_t mask = SCENE_PAGE_SIZE - 1U;
    if (value > UINT64_MAX - mask) {
        return UINT64_MAX;
    }
    return (value + mask) & ~mask;
}

static uint64_t align_down(uint64_t value) {
    return value & ~(SCENE_PAGE_SIZE - 1U);
}

static bool bit_is_set(const struct allocator_region *region, size_t index) {
    const size_t byte_index = index / BITS_PER_BYTE;
    const uint8_t mask = (uint8_t)(1U << (index % BITS_PER_BYTE));
    return (region->bitmap[byte_index] & mask) != 0;
}

static void set_bit(struct allocator_region *region, size_t index) {
    const size_t byte_index = index / BITS_PER_BYTE;
    const uint8_t mask = (uint8_t)(1U << (index % BITS_PER_BYTE));
    region->bitmap[byte_index] |= mask;
}

static void clear_bit(struct allocator_region *region, size_t index) {
    const size_t byte_index = index / BITS_PER_BYTE;
    const uint8_t mask = (uint8_t)(1U << (index % BITS_PER_BYTE));
    region->bitmap[byte_index] &= (uint8_t)~mask;
}

bool scene_physical_init(const struct scene_boot_info *boot_info) {
    if (boot_info == 0 ||
        boot_info->hhdm_offset == 0 ||
        boot_info->usable_region_count == 0) {
        return false;
    }

    region_count = 0;
    allocation_region_hint = 0;
    free_pages = 0;
    metadata_pages = 0;

    for (size_t index = 0; index < boot_info->usable_region_count; ++index) {
        const struct scene_physical_region *source =
            &boot_info->usable_regions[index];
        if (source->base > UINT64_MAX - source->length) {
            continue;
        }

        uint64_t start = align_up(source->base);
        const uint64_t end = align_down(source->base + source->length);
        if (start < SCENE_LOW_MEMORY_RESERVE) {
            start = SCENE_LOW_MEMORY_RESERVE;
        }
        if (start == UINT64_MAX || start >= end ||
            start > UINT64_MAX - boot_info->hhdm_offset) {
            continue;
        }

        const uint64_t page_count_u64 = (end - start) / SCENE_PAGE_SIZE;
        if (page_count_u64 == 0 || page_count_u64 > SIZE_MAX) {
            continue;
        }
        const size_t page_count = (size_t)page_count_u64;
        if (page_count > SIZE_MAX - (BITS_PER_BYTE - 1U)) {
            continue;
        }
        const size_t bitmap_bytes =
            (page_count + (BITS_PER_BYTE - 1U)) / BITS_PER_BYTE;
        if (bitmap_bytes > SIZE_MAX - ((size_t)SCENE_PAGE_SIZE - 1U)) {
            continue;
        }
        const size_t bitmap_pages =
            (bitmap_bytes + (size_t)SCENE_PAGE_SIZE - 1U) /
            (size_t)SCENE_PAGE_SIZE;
        if (bitmap_pages >= page_count) {
            continue;
        }

        struct allocator_region *region = &regions[region_count];
        *region = (struct allocator_region){
            .base = start,
            .page_count = page_count,
            .metadata_pages = bitmap_pages,
            .allocation_hint = bitmap_pages,
            .bitmap = (volatile uint8_t *)(uintptr_t)(
                start + boot_info->hhdm_offset
            )
        };

        const size_t bitmap_storage_bytes =
            bitmap_pages * (size_t)SCENE_PAGE_SIZE;
        for (size_t byte = 0; byte < bitmap_storage_bytes; ++byte) {
            region->bitmap[byte] = 0;
        }
        for (size_t page = 0; page < bitmap_pages; ++page) {
            set_bit(region, page);
        }

        free_pages += (uint64_t)(page_count - bitmap_pages);
        metadata_pages += (uint64_t)bitmap_pages;
        ++region_count;
    }

    return region_count != 0;
}

bool scene_physical_allocate(uint64_t *physical_address) {
    if (physical_address == 0 || free_pages == 0 || region_count == 0) {
        return false;
    }

    for (size_t region_offset = 0;
        region_offset < region_count;
        ++region_offset) {
        const size_t region_index =
            (allocation_region_hint + region_offset) % region_count;
        struct allocator_region *region = &regions[region_index];

        for (size_t page_offset = 0;
            page_offset < region->page_count;
            ++page_offset) {
            const size_t page_index =
                (region->allocation_hint + page_offset) % region->page_count;
            if (!bit_is_set(region, page_index)) {
                set_bit(region, page_index);
                region->allocation_hint =
                    (page_index + 1U) % region->page_count;
                allocation_region_hint = region_index;
                --free_pages;
                *physical_address =
                    region->base +
                    (uint64_t)page_index * SCENE_PAGE_SIZE;
                return true;
            }
        }
    }
    return false;
}

bool scene_physical_free(uint64_t physical_address) {
    if ((physical_address & (SCENE_PAGE_SIZE - 1U)) != 0) {
        return false;
    }

    for (size_t index = 0; index < region_count; ++index) {
        struct allocator_region *region = &regions[index];
        const uint64_t region_length =
            (uint64_t)region->page_count * SCENE_PAGE_SIZE;
        if (physical_address < region->base ||
            physical_address - region->base >= region_length) {
            continue;
        }

        const size_t page_index = (size_t)(
            (physical_address - region->base) / SCENE_PAGE_SIZE
        );
        if (page_index < region->metadata_pages ||
            !bit_is_set(region, page_index)) {
            return false;
        }
        clear_bit(region, page_index);
        if (page_index < region->allocation_hint) {
            region->allocation_hint = page_index;
        }
        allocation_region_hint = index;
        ++free_pages;
        return true;
    }
    return false;
}

uint64_t scene_physical_free_pages(void) {
    return free_pages;
}

uint64_t scene_physical_metadata_pages(void) {
    return metadata_pages;
}
