#include "clock.h"

#include "../arch/x86_64/tsc.h"
#include "../core/clock_math.h"

#include <stddef.h>

bool scene_clock_self_test(void) {
    static const struct {
        uint32_t maximum_leaf;
        uint32_t denominator;
        uint32_t numerator;
        uint32_t crystal_hz;
        uint64_t expected_hz;
    } cpuid_cases[] = {
        {0x14U, 2U, 250U, 24000000U, 0},
        {0x15U, 0U, 250U, 24000000U, 0},
        {0x15U, 2U, 0U, 24000000U, 0},
        {0x15U, 2U, 250U, 0U, 0},
        {0x15U, 2U, 250U, 24000000U, UINT64_C(3000000000)},
        {0x15U, 3U, 2U, 25000000U, UINT64_C(16666666)},
        {0x15U, UINT32_MAX, 1U, 1U, 0},
        {0x15U, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX},
        {0x15U, 1U, UINT32_MAX, UINT32_MAX,
            UINT64_C(18446744065119617025)}
    };
    for (size_t index = 0;
        index < sizeof(cpuid_cases) / sizeof(cpuid_cases[0]); ++index) {
        if (scene_x86_tsc_frequency_from_cpuid(
                cpuid_cases[index].maximum_leaf,
                cpuid_cases[index].denominator,
                cpuid_cases[index].numerator,
                cpuid_cases[index].crystal_hz
            ) != cpuid_cases[index].expected_hz) {
            return false;
        }
    }

    static const struct {
        uint64_t reported_hz;
        uint64_t measured_hz;
        enum scene_clock_tsc_check expected_check;
        uint64_t expected_ppm;
    } comparison_cases[] = {
        {0, 0, SCENE_CLOCK_TSC_UNAVAILABLE, 0},
        {0, UINT64_C(3000000000), SCENE_CLOCK_TSC_UNAVAILABLE, 0},
        {UINT64_C(3000000000), 0, SCENE_CLOCK_TSC_UNMEASURED, 0},
        {UINT64_C(3000000000), UINT64_C(3000000000),
            SCENE_CLOCK_TSC_MATCH, 0},
        {UINT64_C(3000000000), UINT64_C(3150000000),
            SCENE_CLOCK_TSC_MATCH, 50000U},
        {UINT64_C(3000000000), UINT64_C(3150000001),
            SCENE_CLOCK_TSC_MISMATCH, 50000U},
        {UINT64_C(3000000000), UINT64_C(2850000000),
            SCENE_CLOCK_TSC_MATCH, 50000U},
        {UINT64_C(3000000000), UINT64_C(2849999999),
            SCENE_CLOCK_TSC_MISMATCH, 50000U},
        {UINT64_C(10000000000), UINT64_C(1000000),
            SCENE_CLOCK_TSC_MISMATCH, 999900U},
        {UINT64_C(1000000), UINT64_C(10000000000),
            SCENE_CLOCK_TSC_MISMATCH, UINT64_C(9999000000)},
        {999999U, UINT64_C(3000000000), SCENE_CLOCK_TSC_INVALID, 0},
        {UINT64_MAX, UINT64_C(3000000000), SCENE_CLOCK_TSC_INVALID, 0},
        {UINT64_C(3000000000), UINT64_MAX, SCENE_CLOCK_TSC_INVALID, 0}
    };
    for (size_t index = 0;
        index < sizeof(comparison_cases) / sizeof(comparison_cases[0]); ++index) {
        struct scene_clock_diagnostics diagnostics = {
            .tsc_reported_hz = comparison_cases[index].reported_hz,
            .tsc_measured_hz = comparison_cases[index].measured_hz,
            .tsc_deviation_ppm = UINT64_MAX
        };
        scene_clock_evaluate_tsc(&diagnostics);
        if (diagnostics.tsc_check != comparison_cases[index].expected_check ||
            diagnostics.tsc_deviation_ppm !=
                comparison_cases[index].expected_ppm) {
            return false;
        }
    }

    struct scene_clock_diagnostics actual;
    return scene_clock_get_diagnostics(&actual) &&
        !scene_clock_get_diagnostics(0);
}
