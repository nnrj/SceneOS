#include "acpi.h"

#include <scene/boot.h>
#include <scene/paging.h>
#include <scene/physical.h>

#include <stdbool.h>
#include <stdint.h>

#define SCENE_HPET_VIRTUAL UINT64_C(0xffffe00000002000)
#define SCENE_HPET_CAPABILITIES 0x000U
#define SCENE_HPET_CONFIGURATION 0x010U
#define SCENE_HPET_MAIN_COUNTER 0x0f0U
#define SCENE_HPET_ENABLE UINT64_C(1)
#define SCENE_HPET_LEGACY_REPLACEMENT (UINT64_C(1) << 1U)
#define SCENE_HPET_COUNTER_64_BIT (UINT64_C(1) << 13U)
#define SCENE_FEMTOSECONDS_PER_SECOND UINT64_C(1000000000000000)

struct __attribute__((packed)) acpi_generic_address {
    uint8_t address_space;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
};

struct __attribute__((packed)) acpi_hpet {
    struct scene_acpi_sdt_header header;
    uint32_t event_timer_block_id;
    struct acpi_generic_address base_address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
};

static volatile uint8_t *hpet_base;
static uint64_t hpet_frequency_hz;
static bool hpet_ready;

static uint64_t read_register(uint32_t offset) {
    return *(volatile uint64_t *)(hpet_base + offset);
}

static void write_register(uint32_t offset, uint64_t value) {
    *(volatile uint64_t *)(hpet_base + offset) = value;
}

bool scene_arch_hpet_init(const struct scene_boot_info *boot_info) {
    if (hpet_ready) {
        return true;
    }
    const struct acpi_hpet *table =
        (const struct acpi_hpet *)(const void *)scene_acpi_find_table(
            boot_info,
            "HPET"
        );
    if (table == 0 || table->header.length < sizeof(*table) ||
        table->base_address.address_space != 0U ||
        table->base_address.address == 0 ||
        (table->base_address.address & (SCENE_PAGE_SIZE - 1U)) != 0 ||
        !scene_paging_map(
            SCENE_HPET_VIRTUAL,
            table->base_address.address,
            SCENE_PAGE_WRITABLE
        )) {
        return false;
    }

    hpet_base = (volatile uint8_t *)(uintptr_t)SCENE_HPET_VIRTUAL;
    const uint64_t capabilities = read_register(SCENE_HPET_CAPABILITIES);
    const uint64_t period_femtoseconds = capabilities >> 32U;
    if ((capabilities & SCENE_HPET_COUNTER_64_BIT) == 0 ||
        period_femtoseconds == 0 ||
        period_femtoseconds > SCENE_FEMTOSECONDS_PER_SECOND) {
        return false;
    }
    const uint64_t frequency =
        SCENE_FEMTOSECONDS_PER_SECOND / period_femtoseconds;
    if (frequency == 0) {
        return false;
    }

    uint64_t configuration = read_register(SCENE_HPET_CONFIGURATION);
    configuration &= ~(
        SCENE_HPET_ENABLE | SCENE_HPET_LEGACY_REPLACEMENT
    );
    write_register(SCENE_HPET_CONFIGURATION, configuration);
    write_register(SCENE_HPET_MAIN_COUNTER, 0);
    write_register(
        SCENE_HPET_CONFIGURATION,
        configuration | SCENE_HPET_ENABLE
    );
    hpet_frequency_hz = frequency;
    hpet_ready = true;
    return true;
}

bool scene_arch_hpet_ready(void) {
    return hpet_ready;
}

uint64_t scene_arch_hpet_counter(void) {
    return hpet_ready ? read_register(SCENE_HPET_MAIN_COUNTER) : 0;
}

uint64_t scene_arch_hpet_frequency_hz(void) {
    return hpet_frequency_hz;
}
