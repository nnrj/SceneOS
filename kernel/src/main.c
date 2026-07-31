#include <scene/arch.h>
#include <scene/boot.h>
#include <scene/clock.h>
#include <scene/descriptors.h>
#include <scene/exceptions.h>
#include <scene/framebuffer.h>
#include <scene/heap.h>
#include <scene/interrupts.h>
#include <scene/log.h>
#include <scene/paging.h>
#include <scene/physical.h>
#include <scene/timer.h>
#include <scene/timer_queue.h>

static uint32_t timer_callback_results[2];
static size_t timer_callback_count;
static const uint32_t timer_marker_first = 1U;
static const uint32_t timer_marker_second = 2U;
static const uint32_t timer_marker_cancelled = 3U;

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
            "APIC discovery unavailable; using 8259A compatibility path"
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
    scene_log_u64("PIT timer frequency: ", scene_timer_frequency());
    scene_arch_interrupts_enable();
    if (!scene_timer_wait_ticks(3U)) {
        scene_panic("Hardware timer interrupt watchdog expired");
    }
    scene_log_u64("Hardware timer ticks observed: ", scene_timer_ticks());
    if (!scene_clock_init(8U)) {
        scene_panic("TSC clock calibration failed");
    }
    scene_log_line(
        scene_clock_source_invariant()
            ? "Invariant TSC: yes"
            : "Invariant TSC: no"
    );
    scene_log_u64(
        "Calibrated TSC frequency Hz: ",
        scene_clock_source_frequency_hz()
    );

    scene_timer_queue_init();
    timer_callback_count = 0;
    scene_timer_id later_timer;
    scene_timer_id earlier_timer;
    scene_timer_id cancelled_timer;
    const uint64_t monotonic_before = scene_clock_monotonic_ns();
    if (!scene_timer_schedule_after(
            UINT64_C(20000000),
            record_timer_callback,
            (void *)&timer_marker_second,
            &later_timer
        ) ||
        !scene_timer_schedule_after(
            UINT64_C(10000000),
            record_timer_callback,
            (void *)&timer_marker_first,
            &earlier_timer
        ) ||
        !scene_timer_schedule_after(
            UINT64_C(15000000),
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
    if (scene_timer_run_due() != 2U ||
        scene_timer_pending() != 0U ||
        timer_callback_count != 2U ||
        timer_callback_results[0] != timer_marker_first ||
        timer_callback_results[1] != timer_marker_second ||
        monotonic_after <= monotonic_before) {
        scene_panic("Timer queue ordering/cancellation test failed");
    }
    (void)later_timer;
    (void)earlier_timer;
    scene_log_u64(
        "Monotonic clock advance ns: ",
        monotonic_after - monotonic_before
    );
    scene_log_line("Timer queue ordering/cancellation: verified");
    scene_log_u64("Timer queue pending events: ", scene_timer_pending());

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

    if (boot_info.framebuffer != 0) {
        scene_log_u64("Framebuffer width: ", boot_info.framebuffer->width);
        scene_log_u64("Framebuffer height: ", boot_info.framebuffer->height);
        scene_framebuffer_draw_boot_scene(boot_info.framebuffer);
    } else {
        scene_log_line("Framebuffer: unavailable; continuing in headless mode");
    }

    scene_log_line("Bootstrap complete. Halting the bootstrap processor.");
    scene_arch_halt_forever();
}
