#include <scene/exceptions.h>
#include <scene/interrupts.h>
#include <scene/irq.h>

#include <stdint.h>

#define SCENE_PIC_MASTER_COMMAND 0x20U
#define SCENE_PIC_MASTER_DATA 0x21U
#define SCENE_PIC_SLAVE_COMMAND 0xa0U
#define SCENE_PIC_SLAVE_DATA 0xa1U
#define SCENE_PIC_EOI 0x20U
#define SCENE_PIC_IRQ_BASE 32U
#define SCENE_PIC_IRQ_COUNT 16U

extern const uintptr_t scene_interrupt_stubs[SCENE_PIC_IRQ_COUNT];
extern void scene_apic_spurious_stub(void);
extern bool scene_arch_apic_init(const struct scene_boot_info *boot_info);
extern bool scene_arch_apic_set_irq(
    uint8_t irq,
    uint8_t vector,
    bool masked
);
extern void scene_arch_apic_eoi(void);
extern bool scene_arch_apic_ready(void);
extern uint32_t scene_arch_apic_id(void);
extern uint32_t scene_arch_apic_timer_gsi(void);

static uint16_t interrupt_mask = UINT16_MAX;

static void write_port8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void) {
    write_port8(0x80U, 0);
}

static void write_masks(void) {
    write_port8(
        SCENE_PIC_MASTER_DATA,
        (uint8_t)(interrupt_mask & UINT8_MAX)
    );
    write_port8(
        SCENE_PIC_SLAVE_DATA,
        (uint8_t)(interrupt_mask >> 8U)
    );
}

static void remap_pic(void) {
    write_port8(SCENE_PIC_MASTER_COMMAND, 0x11U);
    io_wait();
    write_port8(SCENE_PIC_SLAVE_COMMAND, 0x11U);
    io_wait();
    write_port8(SCENE_PIC_MASTER_DATA, SCENE_PIC_IRQ_BASE);
    io_wait();
    write_port8(
        SCENE_PIC_SLAVE_DATA,
        SCENE_PIC_IRQ_BASE + 8U
    );
    io_wait();
    write_port8(SCENE_PIC_MASTER_DATA, 1U << 2U);
    io_wait();
    write_port8(SCENE_PIC_SLAVE_DATA, 2U);
    io_wait();
    write_port8(SCENE_PIC_MASTER_DATA, 0x01U);
    io_wait();
    write_port8(SCENE_PIC_SLAVE_DATA, 0x01U);
    io_wait();
    write_masks();
}

static void end_of_interrupt(uint8_t irq) {
    if (irq >= 8U) {
        write_port8(SCENE_PIC_SLAVE_COMMAND, SCENE_PIC_EOI);
    }
    write_port8(SCENE_PIC_MASTER_COMMAND, SCENE_PIC_EOI);
}

bool scene_arch_irq_enable(uint32_t irq) {
    if (irq >= SCENE_PIC_IRQ_COUNT) {
        return false;
    }
    if (scene_arch_apic_ready()) {
        return scene_arch_apic_set_irq(
            (uint8_t)irq,
            (uint8_t)(SCENE_PIC_IRQ_BASE + irq),
            false
        );
    }
    interrupt_mask &= (uint16_t)~(UINT16_C(1) << irq);
    if (irq >= 8U) {
        interrupt_mask &= (uint16_t)~(UINT16_C(1) << 2U);
    }
    write_masks();
    return true;
}

bool scene_arch_irq_disable(uint32_t irq) {
    if (irq >= SCENE_PIC_IRQ_COUNT) {
        return false;
    }
    if (scene_arch_apic_ready()) {
        return scene_arch_apic_set_irq(
            (uint8_t)irq,
            (uint8_t)(SCENE_PIC_IRQ_BASE + irq),
            true
        );
    }
    interrupt_mask |= (uint16_t)(UINT16_C(1) << irq);
    if (irq >= 8U && (interrupt_mask & UINT16_C(0xff00)) ==
        UINT16_C(0xff00)) {
        interrupt_mask |= UINT16_C(1) << 2U;
    }
    write_masks();
    return true;
}

void scene_arch_interrupts_init(void) {
    scene_arch_interrupts_disable();
    for (uint8_t irq = 0; irq < SCENE_PIC_IRQ_COUNT; ++irq) {
        scene_arch_idt_set_gate(
            (uint8_t)(SCENE_PIC_IRQ_BASE + irq),
            scene_interrupt_stubs[irq],
            0
        );
    }
    scene_arch_idt_set_gate(
        UINT8_MAX,
        (uintptr_t)scene_apic_spurious_stub,
        0
    );
    interrupt_mask = UINT16_MAX;
    remap_pic();
}

bool scene_arch_interrupts_activate_apic(
    const struct scene_boot_info *boot_info
) {
    return scene_arch_apic_init(boot_info);
}

const char *scene_arch_interrupt_controller(void) {
    return scene_arch_apic_ready() ? "Local APIC + IOAPIC" : "8259A PIC";
}

uint32_t scene_arch_local_apic_id(void) {
    return scene_arch_apic_id();
}

uint32_t scene_arch_timer_gsi(void) {
    return scene_arch_apic_timer_gsi();
}

void scene_arch_interrupts_enable(void) {
    __asm__ volatile ("sti" : : : "memory");
}

void scene_arch_interrupts_disable(void) {
    __asm__ volatile ("cli" : : : "memory");
}

void scene_arch_interrupt_dispatch(
    const struct scene_interrupt_frame *frame
) {
    if (frame == 0 || frame->vector == UINT8_MAX) {
        return;
    }
    if (
        frame->vector < SCENE_PIC_IRQ_BASE ||
        frame->vector >= SCENE_PIC_IRQ_BASE + SCENE_PIC_IRQ_COUNT) {
        return;
    }

    const uint8_t irq = (uint8_t)(frame->vector - SCENE_PIC_IRQ_BASE);
    (void)scene_irq_dispatch(irq);
    if (scene_arch_apic_ready()) {
        scene_arch_apic_eoi();
    } else {
        end_of_interrupt(irq);
    }
}
