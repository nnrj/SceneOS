#ifndef SCENE_X86_64_ACPI_H
#define SCENE_X86_64_ACPI_H

#include <scene/boot.h>

#include <stdint.h>

struct __attribute__((packed)) scene_acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
};

const struct scene_acpi_sdt_header *scene_acpi_find_table(
    const struct scene_boot_info *boot_info,
    const char signature[4]
);

#endif
