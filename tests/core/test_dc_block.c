/* The DC blocking filter on the OPM + ADPCM mix (x11/dc_block.h) removes a
 * constant offset and leaves the audible band alone.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../X68000 Shared/px68k/x11/dc_block.h"

static int g_failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); g_failures++; } \
} while (0)

#define RATE 48000

/* Peak of the output over the last second of `seconds` of a sine (or DC). */
static double run(DCBlock *f, double amplitude, double hz, int offset, int seconds, double *mean)
{
    int n, total = RATE * seconds;
    double peak = 0.0, sum = 0.0;
    for (n = 0; n < total; n++) {
        int x = offset + (int)lrint(amplitude * sin(2.0 * M_PI * hz * n / RATE));
        int y = DCBlock_Process(f, 0, x);
        DCBlock_Process(f, 1, x);
        if (n >= total - RATE) {
            if (fabs((double)y) > peak) peak = fabs((double)y);
            sum += y;
        }
    }
    if (mean) *mean = sum / RATE;
    return peak;
}

int main(void)
{
    DCBlock f;
    double mean, peak;

    DCBlock_Init(&f, RATE);
    peak = run(&f, 0.0, 0.0, 4000, 2, &mean);
    CHECK(peak < 2.0, "a constant offset decays to zero within 2 seconds");

    DCBlock_Init(&f, RATE);
    peak = run(&f, 10000.0, 1000.0, 3000, 2, &mean);
    CHECK(fabs(mean) < 2.0, "1 kHz with an offset: the offset is removed");
    CHECK(fabs(peak - 10000.0) < 30.0, "1 kHz passes at full level");

    DCBlock_Init(&f, RATE);
    peak = run(&f, 10000.0, 50.0, 0, 2, NULL);
    CHECK(peak > 9700.0, "50 Hz loses less than 0.3 dB");

    DCBlock_Init(&f, RATE);
    peak = run(&f, 32767.0, 1000.0, 0, 1, NULL);
    CHECK(peak <= 32768.0, "output stays within 16 bits");

    /* The mix of two full-scale sources reaches +-65535; check that both
     * clipping branches saturate instead of wrapping around. */
    DCBlock_Init(&f, RATE);
    CHECK(DCBlock_Process(&f, 0, 65534) == 32767, "positive overflow clips to 32767");
    DCBlock_Init(&f, RATE);
    CHECK(DCBlock_Process(&f, 0, -65536) == -32768, "negative overflow clips to -32768");

    DCBlock_Init(&f, 44100);
    CHECK(f.r > 0.998f && f.r < 0.9988f, "coefficient for 10 Hz at 44.1 kHz");

    if (g_failures) {
        printf("%d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
