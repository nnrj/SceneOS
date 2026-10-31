cmake_minimum_required(VERSION 3.25)

project(SceneOS VERSION 0.1.0 LANGUAGES C ASM_NASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(SCENEOS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(SCENEOS_EXCEPTION_SELF_TEST "Trigger int3 after installing the IDT" OFF)
option(SCENEOS_PAGE_FAULT_SELF_TEST "Trigger a write to an unmapped page" OFF)
option(SCENEOS_FORCE_TSC_CLOCK "Disable HPET and test the TSC clock fallback" OFF)
option(SCENEOS_CLOCK_SELF_TEST "Validate clock diagnostic edge cases" OFF)
option(SCENEOS_LOCAL_TIMER_SELF_TEST "Validate BSP local timer delivery" OFF)
option(SCENEOS_FORCE_PIC "Keep the legacy PIC path for compatibility tests" OFF)
option(SCENEOS_KEYBOARD_SELF_TEST "Wait for an injected PS/2 A key" OFF)
option(SCENEOS_TEXT_INPUT_SELF_TEST "Validate key-to-text translation and UTF-8" OFF)
option(SCENEOS_MOUSE_SELF_TEST "Wait for injected PS/2 mouse motion" OFF)

add_subdirectory(kernel)
