#include "acpi.h"
#include "apic.h"

#include <scene/arch.h>
#include <scene/boot.h>
#include <scene/paging.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_LOCAL_APIC_VIRTUAL UINT64_C(0xffffe00000000000)
#define SCENE_IO_APIC_VIRTUAL UINT64_C(0xffffe00000001000)
#define SCENE_IA32_APIC_BASE_MSR 0x1bU
#define SCENE_IA32_APIC_X2_ENABLE (UINT64_C(1) << 10U)
#define SCENE_IA32_APIC_ENABLE (UINT64_C(1) << 11U)
#define SCENE_IA32_APIC_ADDRESS_MASK UINT64_C(0x0000000ffffff000)
#define SCENE_LOCAL_APIC_ID 0x020U
#define SCENE_LOCAL_APIC_TPR 0x080U
#define SCENE_LOCAL_APIC_EOI 0x0b0U
#define SCENE_LOCAL_APIC_SPURIOUS 0x0f0U
#define SCENE_LOCAL_APIC_CMCI 0x2f0U
#define SCENE_LOCAL_APIC_TIMER 0x320U
#define SCENE_LOCAL_APIC_THERMAL 0x330U
#define SCENE_LOCAL_APIC_PERFORMANCE 0x340U
#define SCENE_LOCAL_APIC_LINT0 0x350U
#define SCENE_LOCAL_APIC_LINT1 0x360U
#define SCENE_LOCAL_APIC_ERROR 0x370U
#define SCENE_LOCAL_APIC_INITIAL_COUNT 0x380U
#define SCENE_LOCAL_APIC_CURRENT_COUNT 0x390U
#define SCENE_LOCAL_APIC_DIVIDE 0x3e0U
#define SCENE_LOCAL_APIC_ISR 0x100U
#define SCENE_LOCAL_APIC_IRR 0x200U
#define SCENE_LOCAL_APIC_DIVIDE_16 0x03U
#define SCENE_LOCAL_APIC_PERIODIC (1U << 17U)
#define SCENE_LOCAL_APIC_SEND_PENDING (1U << 12U)
#define SCENE_LOCAL_APIC_MASKED (1U << 16U)
#define SCENE_IO_APIC_VERSION 0x01U
#define SCENE_IO_APIC_REDIRECTION_BASE 0x10U
#define SCENE_ACPI_MADT_IO_APIC 1U
#define SCENE_ACPI_MADT_INTERRUPT_OVERRIDE 2U
#define SCENE_ACPI_MADT_LOCAL_APIC_OVERRIDE 5U
#define SCENE_LEGACY_IRQ_COUNT 16U

struct __attribute__((packed)) acpi_madt {
    struct scene_acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
};

struct __attribute__((packed)) madt_entry_header {
    uint8_t type;
    uint8_t length;
};

struct __attribute__((packed)) madt_io_apic {
    struct madt_entry_header header;
    uint8_t id;
    uint8_t reserved;
    uint32_t address;
    uint32_t global_interrupt_base;
};

struct __attribute__((packed)) madt_interrupt_override {
    struct madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t global_interrupt;
    uint16_t flags;
};

struct __attribute__((packed)) madt_local_apic_override {
    struct madt_entry_header header;
    uint16_t reserved;
    uint64_t address;
};

struct apic_discovery {
    uint64_t local_apic_address;
    uint64_t io_apic_address;
    uint32_t io_apic_gsi_base;
    uint32_t legacy_gsi[SCENE_LEGACY_IRQ_COUNT];
    uint16_t legacy_flags[SCENE_LEGACY_IRQ_COUNT];
};

static volatile uint32_t *local_apic;
static volatile uint32_t *io_apic;
static uint32_t bootstrap_apic_id;
static uint32_t io_apic_global_base;
static uint32_t legacy_global_interrupts[SCENE_LEGACY_IRQ_COUNT];
static uint16_t legacy_override_flags[SCENE_LEGACY_IRQ_COUNT];
static bool apic_ready;

static bool discover_apics(
    const struct scene_boot_info *boot_info,
    struct apic_discovery *discovery
) {
    if (boot_info == 0 || discovery == 0) {
        return false;
    }
    const struct acpi_madt *madt =
        (const struct acpi_madt *)(const void *)scene_acpi_find_table(
            boot_info,
            "APIC"
        );
    if (madt == 0 || madt->header.length < sizeof(*madt)) {
        return false;
    }

    *discovery = (struct apic_discovery){
        .local_apic_address = madt->local_apic_address,
        .io_apic_address = 0,
        .io_apic_gsi_base = 0
    };
    for (uint32_t irq = 0; irq < SCENE_LEGACY_IRQ_COUNT; ++irq) {
        discovery->legacy_gsi[irq] = irq;
        discovery->legacy_flags[irq] = 0;
    }
    const uint8_t *cursor = madt->entries;
    const uint8_t *end =
        (const uint8_t *)madt + madt->header.length;
    while ((size_t)(end - cursor) >= sizeof(struct madt_entry_header)) {
        const struct madt_entry_header *header =
            (const struct madt_entry_header *)(const void *)cursor;
        if (header->length < sizeof(*header) ||
            (size_t)(end - cursor) < header->length) {
            return false;
        }
        if (header->type == SCENE_ACPI_MADT_IO_APIC &&
            header->length >= sizeof(struct madt_io_apic)) {
            const struct madt_io_apic *entry =
                (const struct madt_io_apic *)(const void *)cursor;
            if (discovery->io_apic_address == 0) {
                discovery->io_apic_address = entry->address;
                discovery->io_apic_gsi_base =
                    entry->global_interrupt_base;
            }
        } else if (
            header->type == SCENE_ACPI_MADT_INTERRUPT_OVERRIDE &&
            header->length >= sizeof(struct madt_interrupt_override)
        ) {
            const struct madt_interrupt_override *entry =
                (const struct madt_interrupt_override *)(const void *)cursor;
            if (entry->bus == 0U &&
                entry->source < SCENE_LEGACY_IRQ_COUNT) {
                discovery->legacy_gsi[entry->source] =
                    entry->global_interrupt;
                discovery->legacy_flags[entry->source] = entry->flags;
            }
        } else if (
            header->type == SCENE_ACPI_MADT_LOCAL_APIC_OVERRIDE &&
            header->length >= sizeof(struct madt_local_apic_override)
        ) {
            const struct madt_local_apic_override *entry =
                (const struct madt_local_apic_override *)(const void *)cursor;
            discovery->local_apic_address = entry->address;
        }
        cursor += header->length;
    }
    return discovery->local_apic_address != 0 &&
        discovery->io_apic_address != 0;
}

static uint64_t read_msr(uint32_t index) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(index)
    );
    return ((uint64_t)high << 32U) | low;
}

static void write_msr(uint32_t index, uint64_t value) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(index),
          "a"((uint32_t)value),
          "d"((uint32_t)(value >> 32U))
        : "memory"
    );
}

static bool processor_has_apic(void) {
    uint32_t eax = 1U;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile (
        "cpuid"
        : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    );
    (void)ebx;
    (void)ecx;
    return (edx & (1U << 9U)) != 0;
}

static uint32_t local_read(uint32_t offset) {
    return local_apic[offset / sizeof(uint32_t)];
}

static void local_write(uint32_t offset, uint32_t value) {
    local_apic[offset / sizeof(uint32_t)] = value;
    (void)local_read(SCENE_LOCAL_APIC_ID);
}

static uint32_t io_read(uint8_t index) {
    io_apic[0] = index;
    return io_apic[4];
}

static void io_write(uint8_t index, uint32_t value) {
    io_apic[0] = index;
    io_apic[4] = value;
}

bool scene_arch_apic_init(const struct scene_boot_info *boot_info) {
    struct apic_discovery discovery;
    if (!processor_has_apic() ||
        !discover_apics(boot_info, &discovery) ||
        !scene_paging_map(
            SCENE_LOCAL_APIC_VIRTUAL,
            discovery.local_apic_address,
            SCENE_PAGE_WRITABLE
        ) ||
        !scene_paging_map(
            SCENE_IO_APIC_VIRTUAL,
            discovery.io_apic_address,
            SCENE_PAGE_WRITABLE
        )) {
        return false;
    }

    local_apic =
        (volatile uint32_t *)(uintptr_t)SCENE_LOCAL_APIC_VIRTUAL;
    io_apic =
        (volatile uint32_t *)(uintptr_t)SCENE_IO_APIC_VIRTUAL;
    uint64_t apic_base = read_msr(SCENE_IA32_APIC_BASE_MSR);
    apic_base &= ~(
        SCENE_IA32_APIC_ADDRESS_MASK |
        SCENE_IA32_APIC_X2_ENABLE
    );
    apic_base |= discovery.local_apic_address |
        SCENE_IA32_APIC_ENABLE;
    write_msr(SCENE_IA32_APIC_BASE_MSR, apic_base);

    local_write(SCENE_LOCAL_APIC_TIMER,
        SCENE_LOCAL_APIC_MASKED | SCENE_LOCAL_TIMER_VECTOR);
    local_write(SCENE_LOCAL_APIC_INITIAL_COUNT, 0);
    local_write(SCENE_LOCAL_APIC_CMCI, SCENE_LOCAL_APIC_MASKED);
    local_write(SCENE_LOCAL_APIC_THERMAL, SCENE_LOCAL_APIC_MASKED);
    local_write(SCENE_LOCAL_APIC_PERFORMANCE, SCENE_LOCAL_APIC_MASKED);
    local_write(SCENE_LOCAL_APIC_LINT0, SCENE_LOCAL_APIC_MASKED);
    local_write(SCENE_LOCAL_APIC_LINT1, SCENE_LOCAL_APIC_MASKED);
    local_write(SCENE_LOCAL_APIC_ERROR, SCENE_LOCAL_APIC_MASKED);
    local_write(SCENE_LOCAL_APIC_TPR, 0);
    local_write(SCENE_LOCAL_APIC_SPURIOUS, 0x100U | 0xffU);
    const uint32_t version = io_read(SCENE_IO_APIC_VERSION);
    const uint32_t maximum_redirection = (version >> 16U) & 0xffU;
    for (uint32_t redirection = 0;
        redirection <= maximum_redirection;
        ++redirection) {
        const uint8_t register_index = (uint8_t)(
            SCENE_IO_APIC_REDIRECTION_BASE + redirection * 2U
        );
        io_write((uint8_t)(register_index + 1U), 0);
        io_write(register_index, 1U << 16U);
    }
    bootstrap_apic_id = local_read(SCENE_LOCAL_APIC_ID) >> 24U;
    io_apic_global_base = discovery.io_apic_gsi_base;
    for (uint32_t irq = 0; irq < SCENE_LEGACY_IRQ_COUNT; ++irq) {
        legacy_global_interrupts[irq] = discovery.legacy_gsi[irq];
        legacy_override_flags[irq] = discovery.legacy_flags[irq];
    }
    apic_ready = true;
    return true;
}

bool scene_arch_apic_set_irq(
    uint8_t irq,
    uint8_t vector,
    bool masked
) {
    if (!apic_ready || irq >= SCENE_LEGACY_IRQ_COUNT ||
        legacy_global_interrupts[irq] < io_apic_global_base) {
        return false;
    }
    const uint32_t redirection =
        legacy_global_interrupts[irq] - io_apic_global_base;
    const uint32_t version = io_read(SCENE_IO_APIC_VERSION);
    const uint32_t maximum_redirection = (version >> 16U) & 0xffU;
    if (redirection > maximum_redirection) {
        return false;
    }

    uint32_t low = vector;
    const uint16_t flags = legacy_override_flags[irq];
    const uint16_t polarity = flags & 0x03U;
    const uint16_t trigger = (flags >> 2U) & 0x03U;
    if (polarity == 3U) {
        low |= 1U << 13U;
    }
    if (trigger == 3U) {
        low |= 1U << 15U;
    }
    if (masked) {
        low |= 1U << 16U;
    }
    const uint8_t register_index = (uint8_t)(
        SCENE_IO_APIC_REDIRECTION_BASE + redirection * 2U
    );
    io_write(
        (uint8_t)(register_index + 1U),
        bootstrap_apic_id << 24U
    );
    io_write(register_index, low);
    return true;
}

void scene_arch_apic_disable_for_pic(void) {
    /* Firmware may leave the APIC enabled with the PIC's LINT0 path masked.
     * Global disable lets the BSP accept legacy 8259A interrupts directly.
     * This is a boot-time fallback, before any SceneOS IRQ is enabled. */
    if (processor_has_apic()) {
        const uint64_t apic_base = read_msr(SCENE_IA32_APIC_BASE_MSR);
        write_msr(SCENE_IA32_APIC_BASE_MSR, apic_base & ~(
            SCENE_IA32_APIC_ENABLE | SCENE_IA32_APIC_X2_ENABLE
        ));
    }
    apic_ready = false;
}

void scene_arch_apic_eoi(void) {
    if (apic_ready) {
        local_write(SCENE_LOCAL_APIC_EOI, 0);
    }
}

bool scene_arch_apic_ready(void) {
    return apic_ready;
}

uint32_t scene_arch_apic_id(void) {
    return bootstrap_apic_id;
}

uint32_t scene_arch_apic_timer_gsi(void) {
    return legacy_global_interrupts[0];
}

bool scene_arch_local_timer_available(void) {
    return apic_ready;
}

bool scene_arch_local_timer_pending(void) {
    if (!apic_ready) {
        return false;
    }
    const uint32_t bank = (SCENE_LOCAL_TIMER_VECTOR / 32U) * 16U;
    const uint32_t bit = UINT32_C(1) << (SCENE_LOCAL_TIMER_VECTOR % 32U);
    return ((local_read(SCENE_LOCAL_APIC_IRR + bank) |
        local_read(SCENE_LOCAL_APIC_ISR + bank)) & bit) != 0 ||
        (local_read(SCENE_LOCAL_APIC_TIMER) &
            SCENE_LOCAL_APIC_SEND_PENDING) != 0;
}

void scene_arch_local_timer_stop(void) {
    if (apic_ready) {
        local_write(SCENE_LOCAL_APIC_TIMER,
            SCENE_LOCAL_TIMER_VECTOR | SCENE_LOCAL_APIC_MASKED);
        local_write(SCENE_LOCAL_APIC_INITIAL_COUNT, 0);
    }
}

bool scene_arch_local_timer_configure(
    uint32_t initial_count, bool periodic, bool masked
) {
    if (!apic_ready || initial_count == 0 ||
        scene_arch_local_timer_pending()) {
        return false;
    }
    scene_arch_local_timer_stop();
    local_write(SCENE_LOCAL_APIC_DIVIDE, SCENE_LOCAL_APIC_DIVIDE_16);
    uint32_t control = SCENE_LOCAL_TIMER_VECTOR;
    if (periodic) {
        control |= SCENE_LOCAL_APIC_PERIODIC;
    }
    if (masked) {
        control |= SCENE_LOCAL_APIC_MASKED;
    }
    local_write(SCENE_LOCAL_APIC_TIMER, control);
    /* Initial count is written last; it is what starts the countdown. */
    local_write(SCENE_LOCAL_APIC_INITIAL_COUNT, initial_count);
    return true;
}

uint32_t scene_arch_local_timer_current_count(void) {
    return apic_ready ? local_read(SCENE_LOCAL_APIC_CURRENT_COUNT) : 0;
}
