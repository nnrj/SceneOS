#include "acpi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_ACPI_RSDP_V1_SIZE 20U
#define SCENE_ACPI_MAX_RSDP_SIZE 4096U
#define SCENE_ACPI_MAX_TABLE_SIZE (1024U * 1024U)

struct __attribute__((packed)) acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
};

static bool bytes_equal(
    const char *left,
    const char *right,
    size_t count
) {
    for (size_t index = 0; index < count; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static bool checksum_valid(const void *address, size_t length) {
    const uint8_t *bytes = address;
    uint8_t sum = 0;
    for (size_t index = 0; index < length; ++index) {
        sum = (uint8_t)(sum + bytes[index]);
    }
    return sum == 0;
}

static const void *physical_pointer(
    uint64_t physical_address,
    uint64_t hhdm_offset
) {
    if (physical_address == 0 ||
        physical_address > UINT64_MAX - hhdm_offset) {
        return 0;
    }
    return (const void *)(uintptr_t)(physical_address + hhdm_offset);
}

static uint64_t read_unaligned_entry(
    const uint8_t *address,
    size_t size
) {
    uint64_t value = 0;
    for (size_t index = 0; index < size; ++index) {
        value |= (uint64_t)address[index] << (index * 8U);
    }
    return value;
}

const struct scene_acpi_sdt_header *scene_acpi_find_table(
    const struct scene_boot_info *boot_info,
    const char signature[4]
) {
    if (boot_info == 0 || boot_info->rsdp == 0 ||
        boot_info->hhdm_offset == 0 || signature == 0) {
        return 0;
    }
    const struct acpi_rsdp *rsdp = boot_info->rsdp;
    if (!bytes_equal(rsdp->signature, "RSD PTR ", 8U) ||
        !checksum_valid(rsdp, SCENE_ACPI_RSDP_V1_SIZE)) {
        return 0;
    }
    if (rsdp->revision >= 2U &&
        (rsdp->length < sizeof(*rsdp) ||
            rsdp->length > SCENE_ACPI_MAX_RSDP_SIZE ||
            !checksum_valid(rsdp, rsdp->length))) {
        return 0;
    }

    const bool use_xsdt = rsdp->revision >= 2U &&
        rsdp->length >= sizeof(*rsdp) &&
        rsdp->xsdt_address != 0;
    const uint64_t root_address = use_xsdt
        ? rsdp->xsdt_address
        : rsdp->rsdt_address;
    const struct scene_acpi_sdt_header *root =
        physical_pointer(root_address, boot_info->hhdm_offset);
    if (root == 0 || root->length < sizeof(*root) ||
        root->length > SCENE_ACPI_MAX_TABLE_SIZE ||
        !bytes_equal(
            root->signature,
            use_xsdt ? "XSDT" : "RSDT",
            4U
        ) ||
        !checksum_valid(root, root->length)) {
        return 0;
    }

    const size_t entry_size = use_xsdt
        ? sizeof(uint64_t)
        : sizeof(uint32_t);
    const size_t entry_count =
        (root->length - sizeof(*root)) / entry_size;
    const uint8_t *entries =
        (const uint8_t *)root + sizeof(*root);
    for (size_t index = 0; index < entry_count; ++index) {
        const uint64_t address = read_unaligned_entry(
            entries + index * entry_size,
            entry_size
        );
        const struct scene_acpi_sdt_header *table =
            physical_pointer(address, boot_info->hhdm_offset);
        if (table != 0 && table->length >= sizeof(*table) &&
            table->length <= SCENE_ACPI_MAX_TABLE_SIZE &&
            bytes_equal(table->signature, signature, 4U) &&
            checksum_valid(table, table->length)) {
            return table;
        }
    }
    return 0;
}
