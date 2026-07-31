#include <scene/paging.h>
#include <scene/physical.h>

#include <stddef.h>
#include <stdint.h>

#define X86_PAGE_PRESENT (UINT64_C(1) << 0)
#define X86_PAGE_WRITABLE (UINT64_C(1) << 1)
#define X86_PAGE_USER (UINT64_C(1) << 2)
#define X86_PAGE_HUGE (UINT64_C(1) << 7)
#define X86_PAGE_NO_EXECUTE (UINT64_C(1) << 63)
#define X86_PAGE_ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define X86_PAGE_ENTRY_COUNT 512U
#define SCENE_MAX_CLONED_TABLES 1024U

struct clone_record {
    uint64_t source;
    uint64_t destination;
    uint8_t level;
};

static uint64_t direct_map_offset;
static uint64_t root_physical_address;
static uint64_t root_control_flags;
static bool paging_ready;
static bool paging_owned;
static struct clone_record clone_records[SCENE_MAX_CLONED_TABLES];
static size_t clone_record_count;

static bool is_canonical(uint64_t address) {
    const uint64_t upper = address >> 48U;
    const bool sign = ((address >> 47U) & 1U) != 0;
    return sign ? upper == UINT64_C(0xffff) : upper == 0;
}

static volatile uint64_t *physical_table(uint64_t physical_address) {
    if (physical_address > UINT64_MAX - direct_map_offset) {
        return 0;
    }
    return (volatile uint64_t *)(uintptr_t)(
        physical_address + direct_map_offset
    );
}

static void zero_table(volatile uint64_t *table) {
    for (size_t index = 0; index < X86_PAGE_ENTRY_COUNT; ++index) {
        table[index] = 0;
    }
}

static bool next_table(
    volatile uint64_t *table,
    size_t index,
    bool user,
    volatile uint64_t **result
) {
    uint64_t entry = table[index];
    if ((entry & X86_PAGE_PRESENT) == 0) {
        uint64_t physical_address;
        if (!scene_physical_allocate(&physical_address)) {
            return false;
        }
        volatile uint64_t *created = physical_table(physical_address);
        if (created == 0) {
            return false;
        }
        zero_table(created);
        entry = physical_address | X86_PAGE_PRESENT | X86_PAGE_WRITABLE;
        if (user) {
            entry |= X86_PAGE_USER;
        }
        table[index] = entry;
    } else if ((entry & X86_PAGE_HUGE) != 0) {
        return false;
    } else if (user && (entry & X86_PAGE_USER) == 0) {
        entry |= X86_PAGE_USER;
        table[index] = entry;
    }

    *result = physical_table(entry & X86_PAGE_ADDRESS_MASK);
    return *result != 0;
}

bool scene_paging_init(uint64_t hhdm_offset) {
    uint64_t control_register;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(control_register));

    direct_map_offset = hhdm_offset;
    root_physical_address = control_register & X86_PAGE_ADDRESS_MASK;
    root_control_flags = control_register & UINT64_C(0xfff);
    paging_owned = false;
    paging_ready =
        hhdm_offset != 0 &&
        is_canonical(hhdm_offset) &&
        physical_table(root_physical_address) != 0;
    return paging_ready;
}

static bool find_clone(
    uint64_t source,
    uint8_t level,
    uint64_t *destination
) {
    for (size_t index = 0; index < clone_record_count; ++index) {
        if (clone_records[index].source == source &&
            clone_records[index].level == level) {
            *destination = clone_records[index].destination;
            return true;
        }
    }
    return false;
}

static bool clone_table(
    uint64_t source_physical,
    uint8_t level,
    uint64_t *destination_physical
) {
    if (find_clone(source_physical, level, destination_physical)) {
        return true;
    }
    if (clone_record_count >= SCENE_MAX_CLONED_TABLES) {
        return false;
    }

    uint64_t created_physical;
    if (!scene_physical_allocate(&created_physical)) {
        return false;
    }
    volatile uint64_t *source = physical_table(source_physical);
    volatile uint64_t *created = physical_table(created_physical);
    if (source == 0 || created == 0) {
        return false;
    }
    zero_table(created);

    clone_records[clone_record_count] = (struct clone_record){
        .source = source_physical,
        .destination = created_physical,
        .level = level
    };
    ++clone_record_count;

    for (size_t index = 0; index < X86_PAGE_ENTRY_COUNT; ++index) {
        const uint64_t entry = source[index];
        if ((entry & X86_PAGE_PRESENT) == 0) {
            continue;
        }
        if (level == 1U ||
            (level <= 3U && (entry & X86_PAGE_HUGE) != 0)) {
            created[index] = entry;
            continue;
        }

        uint64_t child_destination;
        if (!clone_table(
            entry & X86_PAGE_ADDRESS_MASK,
            (uint8_t)(level - 1U),
            &child_destination
        )) {
            return false;
        }
        created[index] =
            child_destination | (entry & ~X86_PAGE_ADDRESS_MASK);
    }

    *destination_physical = created_physical;
    return true;
}

bool scene_paging_take_ownership(void) {
    if (!paging_ready) {
        return false;
    }
    if (paging_owned) {
        return true;
    }

    clone_record_count = 0;
    uint64_t new_root;
    if (!clone_table(root_physical_address, 4U, &new_root)) {
        return false;
    }

    const uint64_t new_control_register = new_root | root_control_flags;
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(new_control_register)
        : "memory"
    );
    root_physical_address = new_root;
    paging_owned = true;
    return true;
}

bool scene_paging_map(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if (!paging_ready ||
        !is_canonical(virtual_address) ||
        (virtual_address & (SCENE_PAGE_SIZE - 1U)) != 0 ||
        (physical_address & (SCENE_PAGE_SIZE - 1U)) != 0 ||
        (physical_address & ~X86_PAGE_ADDRESS_MASK) != 0) {
        return false;
    }

    const size_t level4_index = (size_t)((virtual_address >> 39U) & 0x1ffU);
    const size_t level3_index = (size_t)((virtual_address >> 30U) & 0x1ffU);
    const size_t level2_index = (size_t)((virtual_address >> 21U) & 0x1ffU);
    const size_t level1_index = (size_t)((virtual_address >> 12U) & 0x1ffU);
    const bool user = (flags & SCENE_PAGE_USER) != 0;

    volatile uint64_t *level4 = physical_table(root_physical_address);
    volatile uint64_t *level3;
    volatile uint64_t *level2;
    volatile uint64_t *level1;
    if (level4 == 0 ||
        !next_table(level4, level4_index, user, &level3) ||
        !next_table(level3, level3_index, user, &level2) ||
        !next_table(level2, level2_index, user, &level1) ||
        (level1[level1_index] & X86_PAGE_PRESENT) != 0) {
        return false;
    }

    uint64_t entry = physical_address | X86_PAGE_PRESENT;
    if ((flags & SCENE_PAGE_WRITABLE) != 0) {
        entry |= X86_PAGE_WRITABLE;
    }
    if (user) {
        entry |= X86_PAGE_USER;
    }
    if ((flags & SCENE_PAGE_EXECUTABLE) == 0) {
        entry |= X86_PAGE_NO_EXECUTE;
    }
    level1[level1_index] = entry;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return true;
}

bool scene_paging_map_new(uint64_t virtual_address, uint64_t flags) {
    uint64_t physical_address;
    if (!scene_physical_allocate(&physical_address)) {
        return false;
    }

    volatile uint64_t *page = physical_table(physical_address);
    if (page == 0) {
        (void)scene_physical_free(physical_address);
        return false;
    }
    zero_table(page);
    if (!scene_paging_map(virtual_address, physical_address, flags)) {
        (void)scene_physical_free(physical_address);
        return false;
    }
    return true;
}

bool scene_paging_unmap(
    uint64_t virtual_address,
    uint64_t *physical_address
) {
    if (!paging_ready ||
        !is_canonical(virtual_address) ||
        (virtual_address & (SCENE_PAGE_SIZE - 1U)) != 0) {
        return false;
    }

    const size_t indices[] = {
        (size_t)((virtual_address >> 39U) & 0x1ffU),
        (size_t)((virtual_address >> 30U) & 0x1ffU),
        (size_t)((virtual_address >> 21U) & 0x1ffU),
        (size_t)((virtual_address >> 12U) & 0x1ffU)
    };
    volatile uint64_t *table = physical_table(root_physical_address);
    if (table == 0) {
        return false;
    }

    for (size_t level = 0; level < 3U; ++level) {
        const uint64_t entry = table[indices[level]];
        if ((entry & X86_PAGE_PRESENT) == 0 ||
            (entry & X86_PAGE_HUGE) != 0) {
            return false;
        }
        table = physical_table(entry & X86_PAGE_ADDRESS_MASK);
        if (table == 0) {
            return false;
        }
    }

    const uint64_t leaf = table[indices[3]];
    if ((leaf & X86_PAGE_PRESENT) == 0) {
        return false;
    }
    table[indices[3]] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    if (physical_address != 0) {
        *physical_address = leaf & X86_PAGE_ADDRESS_MASK;
    }
    return true;
}

bool scene_paging_translate(
    uint64_t virtual_address,
    uint64_t *physical_address
) {
    if (!paging_ready || !is_canonical(virtual_address) ||
        physical_address == 0) {
        return false;
    }

    const size_t indices[] = {
        (size_t)((virtual_address >> 39U) & 0x1ffU),
        (size_t)((virtual_address >> 30U) & 0x1ffU),
        (size_t)((virtual_address >> 21U) & 0x1ffU),
        (size_t)((virtual_address >> 12U) & 0x1ffU)
    };
    volatile uint64_t *table = physical_table(root_physical_address);
    if (table == 0) {
        return false;
    }

    for (size_t level = 0; level < 3U; ++level) {
        const uint64_t entry = table[indices[level]];
        if ((entry & X86_PAGE_PRESENT) == 0 ||
            (entry & X86_PAGE_HUGE) != 0) {
            return false;
        }
        table = physical_table(entry & X86_PAGE_ADDRESS_MASK);
        if (table == 0) {
            return false;
        }
    }

    const uint64_t leaf = table[indices[3]];
    if ((leaf & X86_PAGE_PRESENT) == 0) {
        return false;
    }
    *physical_address =
        (leaf & X86_PAGE_ADDRESS_MASK) |
        (virtual_address & (SCENE_PAGE_SIZE - 1U));
    return true;
}

uint64_t scene_paging_root(void) {
    return root_physical_address;
}

bool scene_paging_is_owned(void) {
    return paging_owned;
}

size_t scene_paging_owned_table_count(void) {
    return paging_owned ? clone_record_count : 0;
}
