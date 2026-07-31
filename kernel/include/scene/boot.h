#ifndef SCENE_BOOT_H
#define SCENE_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_BOOT_MAX_USABLE_REGIONS 64U

struct scene_physical_region {
    uint64_t base;
    uint64_t length;
};

struct scene_framebuffer {
    volatile uint8_t *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bits_per_pixel;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct scene_boot_info {
    const char *bootloader_name;
    const char *bootloader_version;
    const void *rsdp;
    const struct scene_framebuffer *framebuffer;
    uint64_t memory_map_entries;
    uint64_t usable_memory_bytes;
    uint64_t hhdm_offset;
    size_t usable_region_count;
    bool usable_regions_truncated;
    struct scene_physical_region
        usable_regions[SCENE_BOOT_MAX_USABLE_REGIONS];
};

bool scene_boot_protocol_supported(void);
bool scene_boot_collect(struct scene_boot_info *info);

#endif
