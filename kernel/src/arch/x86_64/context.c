#include <scene/arch.h>

bool scene_arch_context_prepare(
    struct scene_arch_context *context,
    uintptr_t stack_bottom,
    size_t stack_size,
    void (*entry)(void)
) {
    if (context == 0 || entry == 0 || stack_bottom == 0 ||
        (stack_bottom & 15U) != 0 || (stack_size & 15U) != 0 ||
        stack_size < 64U || stack_bottom > UINTPTR_MAX - stack_size) {
        return false;
    }
    /* context.asm pops six callee-saved GPRs, then RET enters the trampoline.
     * The final unused slot leaves RSP + 8 aligned to 16 at C function entry. */
    uint64_t *frame = (uint64_t *)(stack_bottom + stack_size - 64U);
    for (size_t index = 0; index < 8U; ++index) {
        frame[index] = 0;
    }
    frame[6] = (uint64_t)(uintptr_t)entry;
    context->saved_stack = (uintptr_t)frame;
    return true;
}
