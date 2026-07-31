#include <scene/boot.h>

#include <limine.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_bootloader_info_request bootloader_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static struct scene_framebuffer primary_framebuffer;

bool scene_boot_protocol_supported(void) {
    return LIMINE_BASE_REVISION_SUPPORTED(base_revision);
}

bool scene_boot_collect(struct scene_boot_info *info) {
    if (info == 0 || !scene_boot_protocol_supported()) {
        return false;
    }

    info->bootloader_name = 0;
    info->bootloader_version = 0;
    info->rsdp = 0;
    info->framebuffer = 0;
    info->memory_map_entries = 0;
    info->usable_memory_bytes = 0;
    info->hhdm_offset = 0;
    info->usable_region_count = 0;
    info->usable_regions_truncated = false;

    if (bootloader_request.response != 0) {
        info->bootloader_name = bootloader_request.response->name;
        info->bootloader_version = bootloader_request.response->version;
    }

    if (hhdm_request.response != 0) {
        info->hhdm_offset = hhdm_request.response->offset;
    }

    if (rsdp_request.response != 0) {
        info->rsdp = rsdp_request.response->address;
    }

    if (memory_map_request.response != 0) {
        const struct limine_memmap_response *response = memory_map_request.response;
        info->memory_map_entries = response->entry_count;
        for (uint64_t index = 0; index < response->entry_count; ++index) {
            const struct limine_memmap_entry *entry = response->entries[index];
            if (entry != 0 && entry->type == LIMINE_MEMMAP_USABLE) {
                info->usable_memory_bytes += entry->length;
                if (info->usable_region_count <
                    SCENE_BOOT_MAX_USABLE_REGIONS) {
                    info->usable_regions[info->usable_region_count] =
                        (struct scene_physical_region){
                            .base = entry->base,
                            .length = entry->length
                        };
                    ++info->usable_region_count;
                } else {
                    info->usable_regions_truncated = true;
                }
            }
        }
    }

    if (framebuffer_request.response != 0 &&
        framebuffer_request.response->framebuffer_count != 0) {
        const struct limine_framebuffer *source =
            framebuffer_request.response->framebuffers[0];
        if (source != 0) {
            primary_framebuffer = (struct scene_framebuffer){
                .address = source->address,
                .width = source->width,
                .height = source->height,
                .pitch = source->pitch,
                .bits_per_pixel = source->bpp,
                .red_mask_size = source->red_mask_size,
                .red_mask_shift = source->red_mask_shift,
                .green_mask_size = source->green_mask_size,
                .green_mask_shift = source->green_mask_shift,
                .blue_mask_size = source->blue_mask_size,
                .blue_mask_shift = source->blue_mask_shift
            };
            info->framebuffer = &primary_framebuffer;
        }
    }

    return true;
}
