/*
 * FDC READ ID on a sector with a deleted data mark. READ ID reads only the
 * ID field, so ST2 Control Mark (bit6) must stay clear (uPD72065: CM is
 * reported by READ DATA, READ DIAGNOSTIC, SCAN and READ DELETED DATA).
 * Links the real fdc.c; the FDD layer is stubbed.
 */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "fdc.h"
#include "fdd.h"
#include "ioc.h"

BYTE IOC_IntStat;
BYTE IOC_IntVect;

void IRQH_IRQCallBack(BYTE irq) { (void)irq; }
void IRQH_Int(BYTE irq, void *handler) { (void)irq; (void)handler; }
int SCSI_HasBootActivity(void) { return 0; }
int X68000_GetStorageBusMode(void) { return 0; }
int X68000_IsSCSIBootPending(void) { return 0; }
int X68000_SCSI_IsMounted(int host, int id) { (void)host; (void)id; return 0; }

static const FDCID kSectorID = { 2, 1, 5, 3 };
static int g_readid_result;

int FDD_IsReady(int drv) { (void)drv; return 1; }
int FDD_IsReadOnly(int drv) { (void)drv; return 0; }
int FDD_Seek(int drv, int trk, FDCID *id) { (void)drv; (void)trk; (void)id; return 1; }
int FDD_ReadID(int drv, FDCID *id) { (void)drv; *id = kSectorID; return g_readid_result; }
int FDD_GetCurrentID(int drv, FDCID *id) { (void)drv; *id = kSectorID; return 1; }
int FDD_Read(int drv, FDCID *id, unsigned char *buf) { (void)drv; (void)id; (void)buf; return 0; }
int FDD_ReadDiag(int drv, FDCID *id, FDCID *retid, unsigned char *buf) { (void)drv; (void)id; (void)retid; (void)buf; return 0; }
int FDD_Write(int drv, FDCID *id, unsigned char *buf, int del) { (void)drv; (void)id; (void)buf; (void)del; return 0; }
int FDD_WriteID(int drv, int trk, unsigned char *buf, int num) { (void)drv; (void)trk; (void)buf; (void)num; return 0; }
void FDD_EjectFD(int drive) { (void)drive; }
void FDD_SetAccess(int drv) { (void)drv; }
void FDD_SetBlink(int drv, int blink) { (void)drv; (void)blink; }
void FDD_SetEMask(int drv, int emask) { (void)drv; (void)emask; }

static int failures;

#define CHECK_EQ(actual, expected, name) do { \
    unsigned a_ = (unsigned)(actual), e_ = (unsigned)(expected); \
    if (a_ == e_) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s (got $%02X, expected $%02X)\n", name, a_, e_); failures++; } \
} while (0)

/* Issues READ ID (MF=1, head 0, drive 0) and reads the 7 result bytes. */
static void read_id(BYTE result[7])
{
    int i;
    FDC_Write(0xe94007, 0x80);      /* access drive 0 */
    FDC_Write(0xe94003, 0x4a);      /* READ ID, MF */
    FDC_Write(0xe94003, 0x00);      /* HD=0, US=0 */
    for (i = 0; i < 7; i++) result[i] = FDC_Read(0xe94003);
}

int main(void)
{
    BYTE r[7];

    FDC_Init();

    g_readid_result = 1;            /* normal data mark */
    read_id(r);
    CHECK_EQ(r[0] & 0xc0, 0x00, "READ ID: normal termination");
    CHECK_EQ(r[2], 0x00, "READ ID: ST2 clear for a normal sector");
    CHECK_EQ(r[5], kSectorID.r, "READ ID: returns the sector ID");

    g_readid_result = 1 | 2;        /* deleted data mark */
    read_id(r);
    CHECK_EQ(r[0] & 0xc0, 0x00, "READ ID on a deleted sector: normal termination");
    CHECK_EQ(r[2] & 0x40, 0x00, "READ ID on a deleted sector: ST2 CM stays clear");

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
