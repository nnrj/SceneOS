#ifndef SCENE_PHYSICAL_H
#define SCENE_PHYSICAL_H

#include <scene/boot.h>

#include <stdbool.h>
#include <stdint.h>

#define SCENE_PAGE_SIZE UINT64_C(4096)

bool scene_physical_init(const struct scene_boot_info *boot_info);
bool scene_physical_allocate(uint64_t *physical_address);
bool scene_physical_free(uint64_t physical_address);
uint64_t scene_physical_free_pages(void);
uint64_t scene_physical_metadata_pages(void);

#endif
