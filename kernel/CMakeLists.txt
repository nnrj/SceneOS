set(SCENEOS_KERNEL_SOURCES
    src/main.c
    src/boot/limine_boot.c
    src/core/clock.c
    src/core/input.c
    src/core/irq.c
    src/core/log.c
    src/core/timer_queue.c
    src/memory/heap.c
    src/memory/physical.c
    src/graphics/framebuffer.c
    src/drivers/input/ps2_keyboard.c
    src/drivers/input/ps2_controller.c
    src/drivers/input/ps2_mouse.c
    src/arch/x86_64/descriptors.c
    src/arch/x86_64/exceptions.c
    src/arch/x86_64/acpi.c
    src/arch/x86_64/apic.c
    src/arch/x86_64/hpet.c
    src/arch/x86_64/interrupts.c
    src/arch/x86_64/paging.c
    src/arch/x86_64/serial.c
    src/arch/x86_64/timer.c
    src/arch/x86_64/tsc.c
    src/arch/x86_64/descriptors.asm
    src/arch/x86_64/exceptions.asm
    src/arch/x86_64/halt.asm
    src/arch/x86_64/interrupts.asm
)

add_executable(scene-kernel ${SCENEOS_KERNEL_SOURCES})

target_include_directories(scene-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/third_party/limine"
)

set_target_properties(scene-kernel PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(SCENEOS_EXCEPTION_SELF_TEST)
    target_compile_definitions(scene-kernel PRIVATE
        SCENEOS_EXCEPTION_SELF_TEST=1
    )
endif()

if(SCENEOS_PAGE_FAULT_SELF_TEST)
    target_compile_definitions(scene-kernel PRIVATE
        SCENEOS_PAGE_FAULT_SELF_TEST=1
    )
endif()

if(SCENEOS_FORCE_TSC_CLOCK)
    target_compile_definitions(scene-kernel PRIVATE
        SCENEOS_FORCE_TSC_CLOCK=1
    )
endif()

if(SCENEOS_CLOCK_SELF_TEST)
    target_compile_definitions(scene-kernel PRIVATE SCENEOS_CLOCK_SELF_TEST=1)
    target_sources(scene-kernel PRIVATE src/tests/clock.c)
endif()

if(SCENEOS_KEYBOARD_SELF_TEST)
    target_compile_definitions(scene-kernel PRIVATE
        SCENEOS_KEYBOARD_SELF_TEST=1
    )
endif()

if(SCENEOS_MOUSE_SELF_TEST)
    target_compile_definitions(scene-kernel PRIVATE
        SCENEOS_MOUSE_SELF_TEST=1
    )
endif()

target_compile_options(scene-kernel PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-ffreestanding>
    $<$<COMPILE_LANGUAGE:C>:-fno-builtin>
    $<$<COMPILE_LANGUAGE:C>:-fno-stack-protector>
    $<$<COMPILE_LANGUAGE:C>:-fno-pic>
    $<$<COMPILE_LANGUAGE:C>:-fno-pie>
    $<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer>
    $<$<COMPILE_LANGUAGE:C>:-m64>
    $<$<COMPILE_LANGUAGE:C>:-mcmodel=kernel>
    $<$<COMPILE_LANGUAGE:C>:-mno-red-zone>
    $<$<COMPILE_LANGUAGE:C>:-mno-mmx>
    $<$<COMPILE_LANGUAGE:C>:-mno-sse>
    $<$<COMPILE_LANGUAGE:C>:-mno-sse2>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Wpedantic>
    $<$<COMPILE_LANGUAGE:C>:-Wconversion>
    $<$<COMPILE_LANGUAGE:C>:-Wshadow>
    $<$<AND:$<COMPILE_LANGUAGE:C>,$<BOOL:${SCENEOS_WARNINGS_AS_ERRORS}>>:-Werror>
)

target_link_options(scene-kernel PRIVATE
    -static
    --build-id=none
    -z
    max-page-size=0x1000
    -T
    "${CMAKE_CURRENT_SOURCE_DIR}/linker/x86_64.ld"
)

set_target_properties(scene-kernel PROPERTIES
    OUTPUT_NAME "scene-kernel"
    SUFFIX ".elf"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linker/x86_64.ld"
)
