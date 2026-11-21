/* Host-side tests for the MFP GPIP horizontal-sync input. */
#include <stdio.h>

#include "common.h"
#include "crtc.h"
#include "irqh.h"
#include "keyboard.h"
#include "mfp.h"
#include "winx68k.h"

/* ---- minimal link dependencies for mfp.c ---- */
BYTE CRTC_Regs[48];
WORD CRTC_VSTART = 0;
WORD CRTC_VEND = 0;
WORD CRTC_IntLine = 0;
WORD VLINE_TOTAL = 0;
DWORD VLINE = 0;
DWORD vline = 0;
int HSYNC_CLK = 1000;
int hclk_line = 0;

BYTE traceflag = 0;
BYTE KeyBuf[KeyBufSize];
BYTE KeyBufWP = 0;
BYTE KeyBufRP = 0;
BYTE KeyIntFlag = 0;

void Error(const char *message) { (void)message; }
void IRQH_IRQCallBack(BYTE irq) { (void)irq; }
void IRQH_Int(BYTE irq, void *handler) { (void)irq; (void)handler; }

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) { \
        printf("PASS: %s\n", name); \
    } else { \
        printf("FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

static int hsync_level_at(int position)
{
    hclk_line = position;
    return (MFP_Read(0xe88001) >> 7) & 1;
}

/* What Keyboard_Int() does when it hands the next byte to the MFP. */
static void deliver_key(BYTE code)
{
    LastKey = code;
    KeyIntFlag = 1;
}

static int rsr_buffer_full(void)
{
    return (MFP_Read(0xe8802b) >> 7) & 1;
}

static void test_rsr_buffer_full(void)
{
    KeyBufRP = KeyBufWP = 0;
    KeyIntFlag = 0;
    CHECK(rsr_buffer_full() == 0, "RSR BF: clear while nothing was received");

    deliver_key(0x21);
    CHECK(rsr_buffer_full() == 1, "RSR BF: set while a byte waits in UDR");
    CHECK(MFP_Read(0xe8802f) == 0x21, "UDR: returns the received byte");
    CHECK(rsr_buffer_full() == 0, "RSR BF: clear after UDR is read");

    /* Two more keys queued; the first is handed over, one stays queued. */
    KeyBuf[0] = 0x22; KeyBuf[1] = 0x23; KeyBufWP = 2;
    deliver_key(KeyBuf[KeyBufRP++]);
    CHECK(rsr_buffer_full() == 1, "RSR BF: set while a byte waits, with more queued");
}

int main(void)
{
    MFP_Init();

    /* 100 columns per raster, with a 10-column sync pulse. R02/R03 define
     * a different display window and must not affect the GPIP7 pulse. */
    CRTC_Regs[1] = 99;  /* R00: total is R00+1 = 100 columns */
    CRTC_Regs[3] = 9;   /* R01: sync is R01+1 = 10 columns */
    CRTC_Regs[5] = 19;  /* R02: back porch ends at column 19 */
    CRTC_Regs[7] = 79;  /* R03: display ends at column 79 */

    CHECK(hsync_level_at(0) == 1, "GPIP7 rises at raster start");
    CHECK(hsync_level_at(99) == 1, "GPIP7 stays high through sync pulse");
    CHECK(hsync_level_at(100) == 0, "GPIP7 falls after R01+1 columns");
    CHECK(hsync_level_at(500) == 0, "GPIP7 stays low during display window");
    CHECK(hsync_level_at(999) == 0, "GPIP7 stays low through front porch");

    test_rsr_buffer_full();

    if (failures != 0) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
