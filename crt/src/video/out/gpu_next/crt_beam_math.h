/*
 * Linear-light interval core: Copyright 2024 Mark Rejhon (@BlurBusters)
 * and Timothy Lottes (@NOTimothyLottes), MIT. See crt/LICENSE-BlurBusters.txt.
 * Host clock additions: Copyright 2026 mpv-winbuild contributors, MIT.
 */
#ifndef MPV_CRT_BEAM_MATH_H
#define MPV_CRT_BEAM_MATH_H

#include <math.h>
#include <stdint.h>

struct mp_crt_clock {
    uint64_t present;
    uint64_t cycle;
    double phase;               // bounded native-refresh units
    double ratio;
};

static inline void mp_crt_clock_tick(struct mp_crt_clock *clock)
{
    clock->present++;
    clock->phase += 1.0;
    while (clock->phase >= clock->ratio) {
        clock->phase -= clock->ratio;
        clock->cycle++;
    }
}

static inline double mp_crt_overlap(double start, double length, double exposure)
{
    return fmax(0.0, fmin(start + length, exposure + 1.0)
                       - fmax(start, exposure));
}

static inline double mp_crt_light(double phase, double ratio, double gain,
                                  double row, const double linear[3])
{
    double sum = 0.0;
    for (int i = 0; i < 3; i++) {
        double start = (row + i - 1) * ratio;
        sum += mp_crt_overlap(start, linear[i] * ratio * gain, phase);
    }
    return sum;
}

#endif
