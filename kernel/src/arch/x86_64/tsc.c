#include <scene/arch.h>

#include "tsc.h"

struct cpuid_result {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

static struct cpuid_result read_cpuid(uint32_t leaf) {
    struct cpuid_result result;
    __asm__ volatile (
        "cpuid"
        : "=a"(result.eax), "=b"(result.ebx),
          "=c"(result.ecx), "=d"(result.edx)
        : "a"(leaf), "c"(0U)
    );
    return result;
}

uint64_t scene_arch_timestamp_counter(void) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high) : : "memory");
    return ((uint64_t)high << 32U) | low;
}

bool scene_arch_timestamp_counter_invariant(void) {
    if (read_cpuid(0x80000000U).eax < 0x80000007U) {
        return false;
    }
    return (read_cpuid(0x80000007U).edx & (1U << 8U)) != 0;
}

uint64_t scene_arch_timestamp_counter_frequency_hz(void) {
    const uint32_t maximum_leaf = read_cpuid(0).eax;
    if (maximum_leaf < 0x15U) {
        return 0;
    }
    const struct cpuid_result ratio = read_cpuid(0x15U);
    return scene_x86_tsc_frequency_from_cpuid(
        maximum_leaf, ratio.eax, ratio.ebx, ratio.ecx
    );
}
