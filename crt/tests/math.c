#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "video/out/gpu_next/crt_beam_math.h"

static double source(int k)
{
    static const double values[] = {0, .1, 1, .25, .7, 0, .9};
    return values[(k % 7 + 7) % 7];
}

int main(void)
{
    const double ratios[] = {2, 2.5, 4, 5, 2.4, 4.8};
    double worst = 0;
    int checks = 0;
    for (unsigned r = 0; r < sizeof(ratios) / sizeof(ratios[0]); r++) {
        double ratio = ratios[r];
        struct mp_crt_clock c = {.ratio = ratio};
        for (int n = 0; n < 4096; n++) {
            assert(c.present == (uint64_t)n);
            assert(c.phase >= 0 && c.phase < ratio);
            for (int y = 0; y <= 8; y++) {
                double row = y / 8.0;
                double light[] = {source((int)c.cycle - 2), source((int)c.cycle - 1), source((int)c.cycle)};
                double got = mp_crt_light(c.phase, ratio, .5, row, light);
                double expected = 0;
                for (int k = (int)c.cycle - 5; k <= (int)c.cycle + 2; k++) {
                    double start = (k + 1 + row) * ratio;
                    expected += mp_crt_overlap(start, source(k) * ratio * .5, n);
                }
                double error = fabs(got - expected);
                worst = fmax(worst, error);
                assert(error < 1e-8);
                assert(got >= -1e-9 && got <= 1.0 + 1e-9);
                checks++;
            }
            mp_crt_clock_tick(&c);
        }
        // Counters may be large; the GPU only sees bounded phase, never them.
        c.present = UINT64_C(1) << 50;
        c.cycle = UINT64_C(1) << 48;
        mp_crt_clock_tick(&c);
        assert(c.phase >= 0 && c.phase < ratio);
    }
    printf("CRT math: %d comparisons, max error %.3g; bounded clock passed.\n", checks, worst);
}
