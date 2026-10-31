#include <scene/arch.h>
#include <scene/boot.h>
#include <scene/clock.h>
#include <scene/descriptors.h>
#include <scene/exceptions.h>
#include <scene/framebuffer.h>
#include <scene/heap.h>
#include <scene/interrupts.h>
#include <scene/input.h>
#include <scene/irq.h>
#include <scene/log.h>
#include <scene/paging.h>
#include <scene/physical.h>
#include <scene/ps2_controller.h>
#include <scene/ps2_keyboard.h>
#include <scene/ps2_mouse.h>
#include <scene/timer.h>
#include <scene/timer_queue.h>

#if defined(SCENEOS_CLOCK_SELF_TEST)
#include "tests/clock.h"
#endif
#if defined(SCENEOS_LOCAL_TIMER_SELF_TEST)
#include <scene/local_timer.h>
#include "tests/local_timer.h"
#endif
#if defined(SCENEOS_TEXT_INPUT_SELF_TEST)
#include "tests/text_input.h"
#endif
#if defined(SCENEOS_MOUSE_PROTOCOL_SELF_TEST)
#include "tests/ps2_mouse.h"
#endif

static uint32_t timer_callback_results[2];
static size_t timer_callback_count;
static const uint32_t timer_marker_first = 1U;
static const uint32_t timer_marker_second = 2U;
static const uint32_t timer_marker_cancelled = 3U;
static uint32_t irq_probe_count;

static void record_timer_callback(void *context) {
    if (context != 0 &&
        timer_callback_count <
            sizeof(timer_callback_results) /
                sizeof(timer_callback_results[0])) {
        timer_callback_results[timer_callback_count] =
            *(const uint32_t *)context;
        ++timer_callback_count;
    }
}

static void record_irq_probe(uint32_t irq, void *context) {
    (void)irq;
    if (context != 0) {
        ++*(uint32_t *)context;
    }
}

#if defined(SCENEOS_KEYBOARD_SELF_TEST)
static bool wait_for_keyboard_test(void) {
    const uint64_t start = scene_clock_monotonic_ns();
    const uint64_t timeout_ns = UINT64_C(3000000000);
    struct scene_input_event event;
    while (scene_clock_monotonic_ns() - start < timeout_ns) {
        while (scene_input_pop(&event)) {
            if (event.type == SCENE_INPUT_EVENT_KEY &&
                event.key.code == SCENE_KEY_A && event.key.pressed) {
                scene_log_u64("Injected input sequence: ", event.sequence);
                scene_log_u64("Injected key code: ", event.key.code);
                scene_log_u64(
                    "PS/2 scancodes received: ",
                    scene_ps2_keyboard_scancodes()
                );
                scene_log_u64(
                    "Input events dropped: ",
                    scene_input_dropped()
                );
                return scene_input_dropped() == 0;
            }
        }
        __asm__ volatile ("pause" : : : "memory");
    }
    return false;
}
#endif

#if defined(SCENEOS_MOUSE_SELF_TEST)
static bool wait_for_mouse_test(void) {
    const uint64_t start = scene_clock_monotonic_ns();
    const uint64_t timeout_ns = UINT64_C(3000000000);
    struct scene_input_event event;
    while (scene_clock_monotonic_ns() - start < timeout_ns) {
        while (scene_input_pop(&event)) {
            if (event.type == SCENE_INPUT_EVENT_POINTER &&
                (event.pointer.delta_x != 0 ||
                event.pointer.delta_y != 0)) {
                scene_log_u64("Injected pointer sequence: ", event.sequence);
                scene_log_u64(
                    "Injected pointer delta X: ",
                    (uint64_t)(int64_t)event.pointer.delta_x
                );
                scene_log_u64(
                    "Injected pointer delta Y: ",
                    (uint64_t)(int64_t)event.pointer.delta_y
                );
                scene_log_u64(
                    "PS/2 mouse packets received: ",
                    scene_ps2_mouse_packets()
                );
                scene_log_u64("Input events dropped: ", scene_input_dropped());
                return scene_input_dropped() == 0;
            }
        }
        __asm__ volatile ("pause" : : : "memory");
    }
    return false;
}
#endif

void kmain(void) {
    static struct scene_boot_info boot_info;
    uint64_t first_page;

    scene_log_init();
    scene_log_line("SceneOS kernel bootstrap");
    scene_log_line("------------------------");

    if (!scene_boot_protocol_supported()) {
        scene_panic("Unsupported Limine base revision");
    }

    if (!scene_boot_collect(&boot_info)) {
        scene_panic("Unable to collect boot information");
    }

    scene_log_line("Installing kernel GDT");
    scene_arch_descriptors_init();
    scene_log_line("Kernel GDT and TSS: ready");
    scene_arch_exceptions_init();
    scene_log_line("Exception IDT: ready");
#if defined(SCENEOS_EXCEPTION_SELF_TEST)
    scene_log_line("Triggering exception self-test");
    __asm__ volatile ("int3");
#endif

    scene_arch_interrupts_init();
    scene_log_line("8259A PIC compatibility path: initialized");

    scene_log_write("Bootloader: ");
    scene_log_write(boot_info.bootloader_name != 0
        ? boot_info.bootloader_name
        : "unknown");
    scene_log_write(" ");
    scene_log_line(boot_info.bootloader_version != 0
        ? boot_info.bootloader_version
        : "");

    scene_log_u64("Memory map entries: ", boot_info.memory_map_entries);
    scene_log_u64("Usable memory bytes: ", boot_info.usable_memory_bytes);
    scene_log_hex("HHDM offset: ", boot_info.hhdm_offset);
    scene_log_u64("Usable memory regions: ", boot_info.usable_region_count);
    if (boot_info.usable_regions_truncated) {
        scene_log_line("Warning: usable memory region list was truncated");
    }

    if (!scene_physical_init(&boot_info)) {
        scene_panic("Physical page allocator initialization failed");
    }
    scene_log_u64(
        "Physical allocator metadata pages: ",
        scene_physical_metadata_pages()
    );
    scene_log_u64("Free physical pages: ", scene_physical_free_pages());
    const uint64_t free_pages_before_probe = scene_physical_free_pages();
    if (!scene_physical_allocate(&first_page)) {
        scene_panic("Physical page allocation smoke test failed");
    }
    scene_log_hex("First allocated physical page: ", first_page);
    if (!scene_physical_free(first_page) ||
        scene_physical_free_pages() != free_pages_before_probe) {
        scene_panic("Physical page release smoke test failed");
    }
    uint64_t reclaimed_page;
    if (!scene_physical_allocate(&reclaimed_page) ||
        reclaimed_page != first_page) {
        scene_panic("Physical page reuse smoke test failed");
    }
    scene_log_hex("Reclaimed physical page: ", reclaimed_page);

    if (!scene_paging_init(boot_info.hhdm_offset)) {
        scene_panic("Paging initialization failed");
    }
    const uint64_t boot_page_table_root = scene_paging_root();
    scene_log_hex("Boot page-table root: ", boot_page_table_root);
    if (!scene_paging_take_ownership() ||
        !scene_paging_is_owned() ||
        scene_paging_root() == boot_page_table_root) {
        scene_panic("SceneOS page-table ownership transition failed");
    }
    scene_log_hex("SceneOS page-table root: ", scene_paging_root());
    scene_log_u64(
        "SceneOS-owned page tables: ",
        scene_paging_owned_table_count()
    );

    if (!scene_arch_interrupts_activate_apic(&boot_info)) {
        scene_log_line(
            "APIC routing inactive; using 8259A compatibility path"
        );
    }
    scene_log_write("Interrupt controller: ");
    scene_log_line(scene_arch_interrupt_controller());
    if (scene_arch_local_apic_id() != 0U ||
        scene_arch_timer_gsi() != 0U) {
        scene_log_u64("Bootstrap Local APIC ID: ", scene_arch_local_apic_id());
        scene_log_u64("PIT interrupt GSI: ", scene_arch_timer_gsi());
    }
    if (!scene_timer_init(100U)) {
        scene_panic("PIT timer initialization failed");
    }
    irq_probe_count = 0;
    if (scene_irq_registered_count() != 1U ||
        !scene_irq_attach(15U, record_irq_probe, &irq_probe_count) ||
        scene_irq_attach(15U, record_irq_probe, &irq_probe_count) ||
        scene_irq_detach(15U, record_irq_probe, 0) ||
        !scene_irq_detach(15U, record_irq_probe, &irq_probe_count) ||
        scene_irq_attach(
            SCENE_IRQ_LINE_COUNT,
            record_irq_probe,
            &irq_probe_count
        ) ||
        scene_irq_registered_count() != 1U || irq_probe_count != 0U) {
        scene_panic("IRQ registration lifecycle test failed");
    }
    scene_log_line("IRQ registration lifecycle: verified");
    scene_log_u64(
        "Registered IRQ handlers: ",
        scene_irq_registered_count()
    );
    scene_input_init();
    if (!scene_ps2_controller_init()) {
        scene_panic("PS/2 controller initialization failed");
    }
    if (!scene_ps2_keyboard_init()) {
        scene_panic("PS/2 keyboard initialization failed");
    }
    scene_log_line("PS/2 keyboard: ready");
    if (!scene_ps2_mouse_init()) {
        scene_panic("PS/2 mouse initialization failed");
    }
    scene_log_line("PS/2 mouse: ready");
    scene_log_line(scene_ps2_mouse_has_wheel()
        ? "PS/2 mouse protocol: IntelliMouse (4 bytes)"
        : "PS/2 mouse protocol: standard (3 bytes)");
    scene_log_u64(
        "Registered IRQ handlers after input: ",
        scene_irq_registered_count()
    );
    scene_log_u64("PIT timer frequency: ", scene_timer_frequency());
    scene_arch_interrupts_enable();
    if (!scene_timer_wait_ticks(3U)) {
        scene_panic("Hardware timer interrupt watchdog expired");
    }
    scene_log_u64("Hardware timer ticks observed: ", scene_timer_ticks());
    if (!scene_clock_init(&boot_info, 8U)) {
        scene_panic("Monotonic clock initialization failed");
    }
    scene_log_write("Clock source: ");
    scene_log_line(scene_clock_source_name());
    scene_log_line(scene_clock_source_stable()
        ? "Clock source stable: yes"
        : "Clock source stable: no");
    scene_log_u64(
        "Clock source frequency Hz: ",
        scene_clock_source_frequency_hz()
    );

    struct scene_clock_diagnostics clock_diagnostics;
    if (!scene_clock_get_diagnostics(&clock_diagnostics)) {
        scene_panic("Clock diagnostics unavailable after initialization");
    }
    scene_log_line("TSC calibration reference: PIT");
    scene_log_u64("TSC measured frequency Hz: ",
        clock_diagnostics.tsc_measured_hz);
    scene_log_u64("TSC CPUID frequency Hz: ",
        clock_diagnostics.tsc_reported_hz);
    scene_log_line(clock_diagnostics.tsc_invariant
        ? "TSC invariant: yes" : "TSC invariant: no");
    scene_log_write("TSC frequency check: ");
    scene_log_line(scene_clock_tsc_check_name());
    scene_log_u64("TSC frequency deviation ppm: ",
        clock_diagnostics.tsc_deviation_ppm);
    scene_log_u64("TSC frequency tolerance ppm: ",
        SCENE_CLOCK_TSC_TOLERANCE_PPM);
    if (clock_diagnostics.tsc_check == SCENE_CLOCK_TSC_MISMATCH ||
        clock_diagnostics.tsc_check == SCENE_CLOCK_TSC_INVALID) {
        scene_log_line("Warning: CPUID TSC frequency is not trusted");
    }
#if defined(SCENEOS_CLOCK_SELF_TEST)
    if (!scene_clock_self_test()) {
        scene_panic("Clock diagnostic edge-case test failed");
    }
    scene_log_line("Clock diagnostic edge cases: verified");
#endif

#if defined(SCENEOS_LOCAL_TIMER_SELF_TEST)
    const bool local_timer_verified = scene_local_timer_self_test();
    scene_local_timer_stop();
    if (!local_timer_verified) {
        scene_panic("Local timer self-test failed");
    }
#endif

    scene_timer_queue_init();
    timer_callback_count = 0;
    scene_timer_id later_timer;
    scene_timer_id earlier_timer;
    scene_timer_id cancelled_timer;
    const uint64_t monotonic_before = scene_clock_monotonic_ns();
    if (!scene_timer_schedule_after(
            UINT64_C(10000000),
            record_timer_callback,
            (void *)&timer_marker_second,
            &later_timer
        ) ||
        !scene_timer_schedule_after(
            UINT64_C(5000000),
            record_timer_callback,
            (void *)&timer_marker_first,
            &earlier_timer
        ) ||
        !scene_timer_schedule_after(
            UINT64_C(7500000),
            record_timer_callback,
            (void *)&timer_marker_cancelled,
            &cancelled_timer
        ) ||
        !scene_timer_cancel(cancelled_timer) ||
        scene_timer_cancel(cancelled_timer) ||
        !scene_timer_wait_ticks(3U)) {
        scene_panic("Timer queue setup or wait failed");
    }
    const uint64_t monotonic_after = scene_clock_monotonic_ns();
    const size_t timer_callbacks_run = scene_timer_run_due();
    scene_log_u64("Timer callbacks run: ", timer_callbacks_run);
    scene_log_u64("Timer callbacks recorded: ", timer_callback_count);
    scene_log_u64("Timer callback result 0: ", timer_callback_results[0]);
    scene_log_u64("Timer callback result 1: ", timer_callback_results[1]);
    scene_log_u64("Timer queue pending events: ", scene_timer_pending());
    scene_log_u64(
        "Monotonic clock advance ns: ",
        monotonic_after - monotonic_before
    );
    if (timer_callbacks_run != 2U || scene_timer_pending() != 0U ||
        timer_callback_count != 2U ||
        timer_callback_results[0] != timer_marker_first ||
        timer_callback_results[1] != timer_marker_second ||
        monotonic_after <= monotonic_before) {
        scene_panic("Timer queue ordering/cancellation test failed");
    }
    (void)later_timer;
    (void)earlier_timer;
    scene_log_line("Timer queue ordering/cancellation: verified");

    const uint64_t scratch_virtual_address =
        UINT64_C(0xffffb00000000000);
    if (!scene_paging_map_new(
        scratch_virtual_address,
        SCENE_PAGE_WRITABLE
    )) {
        scene_panic("Scratch page mapping failed");
    }
    volatile uint64_t *scratch =
        (volatile uint64_t *)(uintptr_t)scratch_virtual_address;
    *scratch = UINT64_C(0x5343454e4f53504d);
    uint64_t scratch_physical_address;
    if (!scene_paging_translate(
        scratch_virtual_address,
        &scratch_physical_address
    ) || *scratch != UINT64_C(0x5343454e4f53504d)) {
        scene_panic("Scratch page mapping verification failed");
    }
    uint64_t unmapped_physical_address;
    if (!scene_paging_unmap(
        scratch_virtual_address,
        &unmapped_physical_address
    ) ||
        unmapped_physical_address != scratch_physical_address ||
        scene_paging_translate(
            scratch_virtual_address,
            &scratch_physical_address
        ) ||
        !scene_physical_free(unmapped_physical_address)) {
        scene_panic("Scratch page unmap/release verification failed");
    }
    scene_log_hex(
        "Released mapped physical page: ",
        unmapped_physical_address
    );

    if (!scene_heap_init()) {
        scene_panic("Kernel heap initialization failed");
    }
    uint64_t *heap_probe = scene_heap_allocate(
        sizeof(*heap_probe) * 8U,
        _Alignof(uint64_t)
    );
    if (heap_probe == 0) {
        scene_panic("Kernel heap allocation smoke test failed");
    }
    for (uint64_t index = 0; index < 8U; ++index) {
        heap_probe[index] = UINT64_C(0x5343454e45000000) | index;
    }
    uint64_t heap_physical_address;
    if (!scene_paging_translate(
        (uint64_t)(uintptr_t)heap_probe,
        &heap_physical_address
    ) || heap_probe[7] != UINT64_C(0x5343454e45000007)) {
        scene_panic("Kernel heap mapping verification failed");
    }
    scene_log_hex(
        "Kernel heap virtual address: ",
        (uint64_t)(uintptr_t)heap_probe
    );
    scene_log_hex("Kernel heap physical page: ", heap_physical_address);

    const size_t heap_span_size = 8209U;
    uint8_t *heap_span = scene_heap_allocate(
        heap_span_size,
        (size_t)SCENE_PAGE_SIZE
    );
    if (heap_span == 0 ||
        ((uint64_t)(uintptr_t)heap_span & (SCENE_PAGE_SIZE - 1U)) != 0) {
        scene_panic("Multi-page heap allocation failed");
    }
    heap_span[0] = 0x5a;
    heap_span[heap_span_size - 1U] = 0xa5;
    if (heap_span[0] != 0x5a || heap_span[heap_span_size - 1U] != 0xa5) {
        scene_panic("Multi-page heap verification failed");
    }

    void *merge_probe_a = scene_heap_allocate(256U, 16U);
    void *merge_probe_b = scene_heap_allocate(384U, 16U);
    if (merge_probe_a == 0 || merge_probe_b == 0) {
        scene_panic("Heap merge probe allocation failed");
    }
    const size_t committed_before_reuse = scene_heap_committed_pages();
    if (!scene_heap_free(merge_probe_a) ||
        !scene_heap_free(merge_probe_b) ||
        scene_heap_free(merge_probe_a)) {
        scene_panic("Heap free/double-free verification failed");
    }
    void *merged_reuse = scene_heap_allocate(512U, 16U);
    if (merged_reuse == 0 ||
        scene_heap_committed_pages() != committed_before_reuse) {
        scene_panic("Heap coalescing/reuse verification failed");
    }
    if (!scene_heap_free(merged_reuse) ||
        !scene_heap_free(heap_span) ||
        !scene_heap_free(heap_probe) ||
        scene_heap_allocated_bytes() != 0) {
        scene_panic("Heap final release verification failed");
    }
    scene_log_line("Kernel heap split/coalesce/reuse: verified");
    scene_log_u64("Kernel heap committed pages: ", scene_heap_committed_pages());
    scene_log_u64("Kernel heap allocated bytes: ", scene_heap_allocated_bytes());
#if defined(SCENEOS_PAGE_FAULT_SELF_TEST)
    scene_log_line("Triggering page-fault self-test");
    *(volatile uint64_t *)(uintptr_t)UINT64_C(0xffffd00000000000) =
        UINT64_C(0xdeadbeef);
#endif

#if defined(SCENEOS_MOUSE_SELF_TEST)
    scene_log_line("Waiting for injected PS/2 mouse motion");
    if (!wait_for_mouse_test()) {
        scene_panic("PS/2 mouse input self-test timed out");
    }
    scene_log_line("PS/2 mouse motion: verified");
#endif

    if (boot_info.framebuffer != 0) {
        scene_log_u64("Framebuffer width: ", boot_info.framebuffer->width);
        scene_log_u64("Framebuffer height: ", boot_info.framebuffer->height);
        scene_framebuffer_draw_boot_scene(boot_info.framebuffer);
    } else {
        scene_log_line("Framebuffer: unavailable; continuing in headless mode");
    }

#if defined(SCENEOS_KEYBOARD_SELF_TEST)
    scene_log_line("Waiting for injected PS/2 key A");
    if (!wait_for_keyboard_test()) {
        scene_panic("PS/2 keyboard input self-test timed out");
    }
    scene_log_line("PS/2 key A press: verified");
#endif

#if defined(SCENEOS_TEXT_INPUT_SELF_TEST)
    if (!scene_text_input_self_test()) {
        scene_panic("Text input self-test failed");
    }
#endif

#if defined(SCENEOS_MOUSE_PROTOCOL_SELF_TEST)
    if (!scene_ps2_mouse_self_test()) {
        scene_panic("PS/2 mouse protocol self-test failed");
    }
#endif

    scene_log_line("Bootstrap complete. Halting the bootstrap processor.");
    scene_arch_halt_forever();
}
