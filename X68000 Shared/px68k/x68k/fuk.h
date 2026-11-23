// ---------------------------------------------------------------------------------------
//  FUK.H - FLOPPY Ukun (ふろっぴU君, TNB製作所) USB FDC unit
//
//  While connected, the unit takes the place of the emulated FDC: register
//  accesses to $E94001-$E94007, the I/O controller ($E9C001/$E9C003) and
//  OPM CT2 (forced ready) are relayed to it, and it answers with register
//  values, interrupts and sector data. The protocol follows FUK.java in
//  XEiJ+t (Copyright TNB製作所).
// ---------------------------------------------------------------------------------------

#ifndef _winx68k_fuk_h
#define _winx68k_fuk_h

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The unit enumerates as one USB device (VID 0x04D8 / PID 0xE6B1) with two
// CDC-ACM serial ports. Port 1 answers 'f' to a 0x00 byte and carries register
// access; port 2 answers 'F' and carries asynchronous events and data.
#define FUK_VENDOR_ID  0x04d8
#define FUK_PRODUCT_ID 0xe6b1

// ---- Transport (implemented by the host side, or by a test) ----

typedef struct {
	// Non-blocking read from port 1 or 2. Returns the number of bytes read
	// (0 when nothing is waiting) or -1 when the link is lost.
	int (*read)(int port, BYTE* buf, int max);
	// Writes all bytes to port 1 or 2. Returns 0, or -1 when the link is lost.
	int (*write)(int port, const BYTE* buf, int len);
	// Called repeatedly while waiting for an answer on port 1.
	void (*wait)(void);
	// Monotonic wall clock in milliseconds (bounds the wait above).
	long long (*clock_ms)(void);
	// The core gave up on the unit (no answer or I/O error). The core has
	// already detached; the host should release the ports.
	void (*lost)(const char* reason);
} FUKTransport;

// Starts a session on an identified unit. The unit is initialised on the
// next FUK_Reset(), as XEiJ+t does on the reset after connecting.
void FUK_Attach(const FUKTransport* transport);
void FUK_Detach(void);
int  FUK_IsActive(void);

// ---- Emulator hooks (only call while FUK_IsActive()) ----

BYTE FUK_Read(BYTE reg);              // $E94001-$E94007: reg = address & 0x0F
void FUK_Write(BYTE reg, BYTE data);
BYTE FUK_IocRead(BYTE localStatus);   // $E9C001
void FUK_IocWrite(BYTE reg, BYTE data); // $E9C00x: reg = address & 0x0F
void FUK_SetForceReady(int ready);    // OPM register $1B bit 6 (CT2)
BYTE FUK_FilterDmaCsr(BYTE csr);      // DMAC channel 0 CSR as read by the CPU

// Advances emulated time (10 MHz units, like usedclk) and services events.
void FUK_Tick(int clk10m);
// Machine reset.
void FUK_Reset(void);

// ---- Host side (fuk_host.c, macOS) ----

// Finds the unit, opens both ports, identifies them and closes them again.
// Returns 1 when port 1 and port 2 were both identified, 0 otherwise.
int FUK_Probe(void);
// Finds the unit and attaches it. Returns 1 on success. The emulated machine
// should be reset afterwards.
int FUK_Connect(void);
void FUK_Disconnect(void);
// Human-readable report of the last probe, connect or disconnect.
const char* FUK_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif
