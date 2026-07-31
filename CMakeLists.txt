cmake_minimum_required(VERSION 3.25)

project(SceneOS VERSION 0.1.0 LANGUAGES C ASM_NASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(SCENEOS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(SCENEOS_EXCEPTION_SELF_TEST "Trigger int3 after installing the IDT" OFF)
option(SCENEOS_PAGE_FAULT_SELF_TEST "Trigger a write to an unmapped page" OFF)

add_subdirectory(kernel)
