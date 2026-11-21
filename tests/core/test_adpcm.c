/* MSM6258 (x68k/adpcm.c): PLAY ST resets the decoder (output to the centre,
 * step size to the minimum) also while it is already playing. PCM8A writes
 * PLAY ST without SP when it resumes and resets its own encoder; a decoder
 * that kept its state would then decode with a DC offset.
 */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "prop.h"
#include "adpcm.h"
#include "dmac.h"

Win68Conf Config;
dmac_ch DMA[4];
int FASTCALL DMA_Exec(int ch) { (void)ch; return 0; }

static int g_failures = 0;
#define CHECK_EQ(actual, expected, name) do { \
    long long _a = (long long)(actual), _e = (long long)(expected); \
    if (_a == _e) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s: got %lld, want %lld (%s:%d)\n", name, _a, _e, __FILE__, __LINE__); g_failures++; } \
} while (0)

static ADPCMMonitorState state(void)
{
    ADPCMMonitorState s;
    memset(&s, 0, sizeof s);
    ADPCM_GetMonitorState(&s);
    return s;
}

int main(void)
{
    int i;
    ADPCMMonitorState s;

    ADPCM_Init(44100);
    ADPCM_Write(0xe92001, 0x02);              /* PLAY ST */
    s = state();
    CHECK_EQ(s.playing, 1, "PLAY ST starts playback");
    CHECK_EQ(s.output, 0, "playback starts at the centre");
    CHECK_EQ(s.step, 0, "playback starts at the minimum step");

    /* Rising nibbles (sign 0, magnitude 7): the output and the step grow. */
    for (i = 0; i < 16; i++) ADPCM_Write(0xe92003, 0x77);
    s = state();
    CHECK_EQ(s.output > 0, 1, "decoding moved the output");
    CHECK_EQ(s.step > 0, 1, "decoding raised the step size");

    /* PLAY ST again without SP, as PCM8A does when it resumes. */
    ADPCM_Write(0xe92001, 0x02);
    s = state();
    CHECK_EQ(s.playing, 1, "still playing after PLAY ST while playing");
    CHECK_EQ(s.output, 0, "PLAY ST while playing returns the output to the centre");
    CHECK_EQ(s.step, 0, "PLAY ST while playing returns the step to the minimum");

    /* The same data after the restart decodes as from a fresh start. */
    ADPCM_Write(0xe92003, 0x77);
    {
        ADPCMMonitorState restarted = state();
        ADPCM_Write(0xe92001, 0x01);          /* SP */
        ADPCM_Write(0xe92001, 0x02);          /* PLAY ST */
        ADPCM_Write(0xe92003, 0x77);
        s = state();
        CHECK_EQ(restarted.output, s.output, "restart decodes like a fresh start (output)");
        CHECK_EQ(restarted.step, s.step, "restart decodes like a fresh start (step)");
    }

    if (g_failures) {
        printf("%d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
