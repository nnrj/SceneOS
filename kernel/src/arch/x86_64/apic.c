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
#define SCENE_LOCAL_APIC_MASKED (1U << 16U)
#define SCENE_IO_APIC_VERSION 0x01U
#define SCENE_IO_APIC_REDIRECTION_BASE 0x10U
#define SCENE_ACPI_MADT_IO_APIC 1U
#define SCENE_ACPI_MADT_INTERRUPT_OVERRIDE 2U
#define SCENE_ACPI_MADT_LOCAL_APIC_OVERRIDE 5U

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

struct __attribute__((packed)) acpi_sdt_header {
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

struct __attribute__((packed)) acpi_madt {
    struct acpi_sdt_header header;
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
    uint32_t timer_gsi;
    uint16_t timer_flags;
};

static volatile uint32_t *local_apic;
static volatile uint32_t *io_apic;
static uint32_t bootstrap_apic_id;
static uint32_t timer_global_interrupt;
static uint32_t io_apic_global_base;
static uint16_t timer_override_flags;
static bool apic_ready;

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
    if (physical_address > UINT64_MAX - hhdm_offset) {
        return 0;
    }
    return (const void *)(uintptr_t)(physical_address + hhdm_offset);
}

static const struct acpi_sdt_header *find_madt(
    const struct acpi_rsdp *rsdp,
    uint64_t hhdm_offset
) {
    const bool use_xsdt = rsdp->revision >= 2U &&
        rsdp->length >= sizeof(*rsdp) &&
        rsdp->xsdt_address != 0;
    const uint64_t root_address = use_xsdt
        ? rsdp->xsdt_address
        : rsdp->rsdt_address;
    const struct acpi_sdt_header *root =
        physical_pointer(root_address, hhdm_offset);
    if (root == 0 ||
        root->length < sizeof(*root) ||
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
        uint64_t address;
        if (use_xsdt) {
            const uint64_t *entry =
                (const uint64_t *)(const void *)(
                    entries + index * entry_size
                );
            address = *entry;
        } else {
            const uint32_t *entry =
                (const uint32_t *)(const void *)(
                    entries + index * entry_size
                );
            address = *entry;
        }
        const struct acpi_sdt_header *table =
            physical_pointer(address, hhdm_offset);
        if (table != 0 &&
            table->length >= sizeof(*table) &&
            bytes_equal(table->signature, "APIC", 4U) &&
            checksum_valid(table, table->length)) {
            return table;
        }
    }
    return 0;
}

static bool discover_apics(
    const struct scene_boot_info *boot_info,
    struct apic_discovery *discovery
) {
    if (boot_info == 0 || boot_info->rsdp == 0 ||
        boot_info->hhdm_offset == 0 || discovery == 0) {
        return false;
    }
    const struct acpi_rsdp *rsdp = boot_info->rsdp;
    if (!bytes_equal(rsdp->signature, "RSD PTR ", 8U) ||
        !checksum_valid(rsdp, 20U) ||
        (rsdp->revision >= 2U &&
            (rsdp->length < sizeof(*rsdp) ||
                !checksum_valid(rsdp, rsdp->length)))) {
        return false;
    }

    const struct acpi_madt *madt =
        (const struct acpi_madt *)(const void *)find_madt(
            rsdp,
            boot_info->hhdm_offset
        );
    if (madt == 0 || madt->header.length < sizeof(*madt)) {
        return false;
    }

    *discovery = (struct apic_discovery){
        .local_apic_address = madt->local_apic_address,
        .io_apic_address = 0,
        .io_apic_gsi_base = 0,
        .timer_gsi = 0,
        .timer_flags = 0
    };
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
            if (entry->bus == 0U && entry->source == 0U) {
                discovery->timer_gsi = entry->global_interrupt;
                discovery->timer_flags = entry->flags;
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
        discovery->io_apic_address != 0 &&
        discovery->timer_gsi >= discovery->io_apic_gsi_base;
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

    local_write(SCENE_LOCAL_APIC_TIMER, SCENE_LOCAL_APIC_MASKED);
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
    timer_global_interrupt = discovery.timer_gsi;
    timer_override_flags = discovery.timer_flags;
    io_apic_global_base = discovery.io_apic_gsi_base;
    apic_ready = true;
    return true;
}

bool scene_arch_apic_route_timer(uint8_t vector) {
    if (!apic_ready ||
        timer_global_interrupt < io_apic_global_base) {
        return false;
    }
    const uint32_t redirection =
        timer_global_interrupt - io_apic_global_base;
    const uint32_t version = io_read(SCENE_IO_APIC_VERSION);
    const uint32_t maximum_redirection = (version >> 16U) & 0xffU;
    if (redirection > maximum_redirection) {
        return false;
    }

    uint32_t low = vector;
    const uint16_t polarity = timer_override_flags & 0x03U;
    const uint16_t trigger = (timer_override_flags >> 2U) & 0x03U;
    if (polarity == 3U) {
        low |= 1U << 13U;
    }
    if (trigger == 3U) {
        low |= 1U << 15U;
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
    return timer_global_interrupt;
}
