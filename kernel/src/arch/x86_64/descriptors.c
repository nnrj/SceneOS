#include <scene/descriptors.h>

#include <stddef.h>
#include <stdint.h>

#define SCENE_RING0_STACK_SIZE (32U * 1024U)
#define SCENE_IST_STACK_SIZE (16U * 1024U)
#define SCENE_TSS_SELECTOR 0x18U

struct __attribute__((packed)) descriptor_table_pointer {
    uint16_t limit;
    uint64_t base;
};

struct __attribute__((packed)) task_state_segment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

_Static_assert(sizeof(struct task_state_segment) == 104U, "invalid x86-64 TSS");

static uint64_t global_descriptor_table[5];
static struct task_state_segment kernel_tss;
static uint8_t ring0_stack[SCENE_RING0_STACK_SIZE]
    __attribute__((aligned(16)));
static uint8_t double_fault_stack[SCENE_IST_STACK_SIZE]
    __attribute__((aligned(16)));
static uint8_t nmi_stack[SCENE_IST_STACK_SIZE]
    __attribute__((aligned(16)));
static uint8_t machine_check_stack[SCENE_IST_STACK_SIZE]
    __attribute__((aligned(16)));
static uint8_t diagnostic_stack[SCENE_IST_STACK_SIZE]
    __attribute__((aligned(16)));

extern void scene_arch_load_gdt(
    const struct descriptor_table_pointer *pointer,
    uint16_t tss_selector
);

static uint64_t stack_top(uint8_t *stack, size_t size) {
    return (uint64_t)(uintptr_t)(stack + size);
}

static void install_tss_descriptor(void) {
    const uint64_t base = (uint64_t)(uintptr_t)&kernel_tss;
    const uint64_t limit = sizeof(kernel_tss) - 1U;
    global_descriptor_table[3] =
        (limit & UINT64_C(0xffff)) |
        ((base & UINT64_C(0xffff)) << 16U) |
        (((base >> 16U) & UINT64_C(0xff)) << 32U) |
        (UINT64_C(0x89) << 40U) |
        (((limit >> 16U) & UINT64_C(0x0f)) << 48U) |
        (((base >> 24U) & UINT64_C(0xff)) << 56U);
    global_descriptor_table[4] = base >> 32U;
}

void scene_arch_descriptors_init(void) {
    global_descriptor_table[0] = UINT64_C(0x0000000000000000);
    global_descriptor_table[1] = UINT64_C(0x00af9b000000ffff);
    global_descriptor_table[2] = UINT64_C(0x00cf93000000ffff);

    kernel_tss.rsp0 = stack_top(ring0_stack, sizeof(ring0_stack));
    kernel_tss.ist1 = stack_top(double_fault_stack, sizeof(double_fault_stack));
    kernel_tss.ist2 = stack_top(nmi_stack, sizeof(nmi_stack));
    kernel_tss.ist3 = stack_top(machine_check_stack, sizeof(machine_check_stack));
    kernel_tss.ist4 = stack_top(diagnostic_stack, sizeof(diagnostic_stack));
    kernel_tss.io_map_base = (uint16_t)sizeof(kernel_tss);
    install_tss_descriptor();

    const struct descriptor_table_pointer pointer = {
        .limit = (uint16_t)(sizeof(global_descriptor_table) - 1U),
        .base = (uint64_t)(uintptr_t)global_descriptor_table
    };
    scene_arch_load_gdt(&pointer, SCENE_TSS_SELECTOR);
}

uint8_t scene_arch_ist_index(uintptr_t stack_address) {
    const struct {
        const uint8_t *base;
        size_t size;
    } stacks[] = {
        {double_fault_stack, sizeof(double_fault_stack)},
        {nmi_stack, sizeof(nmi_stack)},
        {machine_check_stack, sizeof(machine_check_stack)},
        {diagnostic_stack, sizeof(diagnostic_stack)}
    };

    for (size_t index = 0; index < sizeof(stacks) / sizeof(stacks[0]); ++index) {
        const uintptr_t base = (uintptr_t)stacks[index].base;
        if (stack_address >= base &&
            stack_address - base < stacks[index].size) {
            return (uint8_t)(index + 1U);
        }
    }
    return 0;
}
