#include <scene/arch.h>
#include <scene/descriptors.h>
#include <scene/exceptions.h>
#include <scene/log.h>

#include <stddef.h>
#include <stdint.h>

struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct __attribute__((packed)) descriptor_table_pointer {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry interrupt_descriptor_table[256];

extern const uintptr_t scene_exception_stubs[32];
extern void scene_arch_load_idt(
    const struct descriptor_table_pointer *pointer
);

void scene_arch_idt_set_gate(
    uint8_t vector,
    uintptr_t handler,
    uint8_t ist
) {
    interrupt_descriptor_table[vector] = (struct idt_entry){
        .offset_low = (uint16_t)(handler & UINT16_MAX),
        .selector = 0x08,
        .ist = (uint8_t)(ist & 0x07U),
        .attributes = 0x8e,
        .offset_middle = (uint16_t)((handler >> 16U) & UINT16_MAX),
        .offset_high = (uint32_t)(handler >> 32U),
        .reserved = 0
    };
}

void scene_arch_exceptions_init(void) {
    for (uint8_t vector = 0; vector < 32U; ++vector) {
        uint8_t ist = 0;
        if (vector == 8U) {
            ist = 1;
        } else if (vector == 2U) {
            ist = 2;
        } else if (vector == 18U) {
            ist = 3;
        }
#if defined(SCENEOS_EXCEPTION_SELF_TEST)
        else if (vector == 3U) {
            ist = 4;
        }
#endif
        scene_arch_idt_set_gate(
            vector,
            scene_exception_stubs[vector],
            ist
        );
    }

    const struct descriptor_table_pointer pointer = {
        .limit = (uint16_t)(sizeof(interrupt_descriptor_table) - 1U),
        .base = (uint64_t)(uintptr_t)interrupt_descriptor_table
    };
    scene_arch_load_idt(&pointer);
}

_Noreturn void scene_arch_exception_dispatch(
    const struct scene_exception_frame *frame
) {
    scene_log_line("");
    scene_log_line("[EXCEPTION] processor exception");
    if (frame != 0) {
        scene_log_u64("Vector: ", frame->vector);
        scene_log_hex("Error code: ", frame->error_code);
        scene_log_hex("Instruction pointer: ", frame->instruction_pointer);
        scene_log_hex("Flags: ", frame->flags);
        scene_log_u64(
            "IST index: ",
            scene_arch_ist_index((uintptr_t)frame)
        );
        if (frame->vector == 14U) {
            uint64_t fault_address;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
            scene_log_hex("Fault address: ", fault_address);
            scene_log_line(
                (frame->error_code & 1U) != 0
                    ? "Page fault cause: protection violation"
                    : "Page fault cause: non-present page"
            );
            scene_log_line(
                (frame->error_code & 2U) != 0
                    ? "Page fault access: write"
                    : "Page fault access: read"
            );
            scene_log_line(
                (frame->error_code & 4U) != 0
                    ? "Page fault privilege: user"
                    : "Page fault privilege: supervisor"
            );
        }
    }
    scene_arch_halt_forever();
}
