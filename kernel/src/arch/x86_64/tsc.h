#ifndef SCENE_X86_64_TSC_H
#define SCENE_X86_64_TSC_H

#include <stdint.h>

/* CPUID.15H: ECX is the crystal frequency; EBX/EAX is the TSC ratio. */
static inline uint64_t scene_x86_tsc_frequency_from_cpuid(
    uint32_t maximum_leaf,
    uint32_t denominator,
    uint32_t numerator,
    uint32_t crystal_hz
) {
    if (maximum_leaf < 0x15U || denominator == 0 ||
        numerator == 0 || crystal_hz == 0) {
        return 0;
    }
    /* The product of two uint32_t values fits in uint64_t. */
    return (uint64_t)crystal_hz * numerator / denominator;
}

#endif
