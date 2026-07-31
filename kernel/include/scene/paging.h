#ifndef SCENE_PAGING_H
#define SCENE_PAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum scene_page_flags {
    SCENE_PAGE_WRITABLE = 1U << 0,
    SCENE_PAGE_USER = 1U << 1,
    SCENE_PAGE_EXECUTABLE = 1U << 2
};

bool scene_paging_init(uint64_t hhdm_offset);
bool scene_paging_take_ownership(void);
bool scene_paging_map(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);
bool scene_paging_map_new(uint64_t virtual_address, uint64_t flags);
bool scene_paging_unmap(
    uint64_t virtual_address,
    uint64_t *physical_address
);
bool scene_paging_translate(
    uint64_t virtual_address,
    uint64_t *physical_address
);
uint64_t scene_paging_root(void);
bool scene_paging_is_owned(void);
size_t scene_paging_owned_table_count(void);

#endif
