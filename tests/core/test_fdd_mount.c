/* Mount state must be observable before the emulated insertion delay ends.
 * Parser stubs isolate that contract; malformed D88 parsing is tested separately.
 */
#include <assert.h>
#include <stdio.h>
#include "common.h"
#include "fdd.h"
#include "disk_xdf.h"
#include "disk_dim.h"
#include "disk_d88.h"
#include "irqh.h"
#include "ioc.h"

BYTE IOC_IntStat;
BYTE IOC_IntVect;
static int acceptsImage = 1;
void IRQH_IRQCallBack(BYTE irq) { (void)irq; }
void IRQH_Int(BYTE irq, void *handler) { (void)irq; (void)handler; }

#define DISK_STUBS(prefix) \
    void prefix##_Init(void) {} \
    void prefix##_Cleanup(void) {} \
    int prefix##_SetFD(int drive, char *name) { return acceptsImage; } \
    int prefix##_Eject(int drive) { return 1; } \
    int prefix##_Seek(int drive, int track, FDCID *id) { return 0; } \
    int prefix##_ReadID(int drive, FDCID *id) { return 0; } \
    int prefix##_WriteID(int drive, int track, unsigned char *buf, int num) { return 0; } \
    int prefix##_Read(int drive, FDCID *id, unsigned char *buf) { return 0; } \
    int prefix##_ReadDiag(int drive, FDCID *id, FDCID *ret, unsigned char *buf) { return 0; } \
    int prefix##_Write(int drive, FDCID *id, unsigned char *buf, int del) { return 0; } \
    int prefix##_GetCurrentID(int drive, FDCID *id) { return 0; }
DISK_STUBS(XDF)
DISK_STUBS(DIM)
DISK_STUBS(D88)

int main(void)
{
    FDD_Init();
    assert(!FDD_IsMounted(-1));
    assert(!FDD_IsMounted(4));
    assert(!FDD_IsMounted(0));
    FDD_SetFD(0, "valid.xdf", 0);
    assert(FDD_IsMounted(0));
    assert(!FDD_IsReady(0));
    FDD_Reset();
    assert(FDD_IsMounted(0));
    for (int i = 0; i < 3; i++) FDD_SetFDInt();
    assert(FDD_IsReady(0));
    FDD_EjectFD(0);
    assert(!FDD_IsMounted(0));
    assert(!FDD_IsReady(0));

    FDD_SetFD(1, "valid.d88", 0);
    assert(FDD_IsMounted(1));
    acceptsImage = 0;
    FDD_SetFD(1, "invalid.d88", 0);
    assert(!FDD_IsMounted(1));
    for (int i = 0; i < 3; i++) FDD_SetFDInt();
    assert(!FDD_IsReady(1));
    FDD_Cleanup();
    puts("PASS: mount, delayed readiness, reset, eject and rejected replacement");
    return 0;
}
