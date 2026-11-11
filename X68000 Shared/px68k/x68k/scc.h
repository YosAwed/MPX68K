#ifndef _winx68k_scc
#define _winx68k_scc

#include "common.h"

void SCC_IntCheck(void);
void SCC_Init(void);
BYTE FASTCALL SCC_Read(DWORD adr);
void FASTCALL SCC_Write(DWORD adr, BYTE data);

// Enhanced serial bridge (C signatures)
int SCC_SetMode(int mode, const char* config);
void SCC_CloseSerial(void);
const char* SCC_GetSlavePath(void);

// RS-232C (channel A) timing: call with the 10 MHz base clocks just executed.
void SCC_Timer(int clk);

// RS-232C link to another emulator, wired like a cross cable (TxD-RxD,
// RTS-CTS, DTR-DSR/DCD). The host moves the wire bytes between the two
// machines over any ordered, reliable transport. Callable from any thread.
void SCC_LinkSetConnected(int connected);                // the peer appeared / went away
void SCC_LinkFeed(const unsigned char *data, int len);   // wire bytes from the peer
int SCC_LinkDrain(unsigned char *buf, int max);          // wire bytes for the peer
void SCC_LinkSetTxNotify(void (*notify)(void));          // called when there is something to drain
int SCC_LinkGetInfo(int item);                           // SCC_LINK_INFO_*, emulation thread

enum {
    SCC_LINK_INFO_ONLINE = 0,
    SCC_LINK_INFO_BAUD,          // bits per second
    SCC_LINK_INFO_FORMAT,        // data bits << 16 | parity (0 none, 1 odd, 2 even) << 8 | stop bits * 2
    SCC_LINK_INFO_PEER_BAUD,     // 0 until the peer has said
    SCC_LINK_INFO_PEER_FORMAT,
    SCC_LINK_INFO_LINES,         // our outputs: 1 RTS, 2 DTR, 4 break
    SCC_LINK_INFO_PEER_LINES,    // the peer's outputs (our CTS, DSR/DCD, break)
    SCC_LINK_INFO_TX_CHARS,
    SCC_LINK_INFO_RX_CHARS,
    SCC_LINK_INFO_MISMATCHES     // characters received with different line settings
};

// Compatibility toggle: emulate original px68k mouse SCC behavior
void SCC_SetCompatMode(int enable);
int SCC_GetCompatMode(void);

// Latch mouse status edges for robust delivery on RTS rising
void SCC_LatchMouseStatus(unsigned char st, signed char x, signed char y);

extern signed char MouseX;
extern signed char MouseY;
extern BYTE MouseSt;

#endif
