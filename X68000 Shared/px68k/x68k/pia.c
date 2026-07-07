// ---------------------------------------------------------------------------------------
//  PIA.C - uPD8255（必要最低限のみ）
// ---------------------------------------------------------------------------------------

#include "common.h"
#include "joystick.h"
#include "pia.h"
#include "ppi.h"
#include "adpcm.h"
#include "m68000.h"

typedef struct {
	BYTE PortA;
	BYTE PortB;
	BYTE PortC;
	BYTE Ctrl;
} PIA;

static PIA pia;

// -----------------------------------------------------------------------
//   初期化
// -----------------------------------------------------------------------
void PIA_Init(void)
{
	pia.PortA = 0xff;
	pia.PortB = 0xff;
	pia.PortC = 0x0b;
	pia.Ctrl = 0;
}


// -----------------------------------------------------------------------
//   I/O Write
// -----------------------------------------------------------------------
void FASTCALL PIA_Write(DWORD adr, BYTE data)
{
	BYTE mask, bit, portc = pia.PortC;

	// JoyportU command mode: forward joyport register writes to the
	// device (ppi.c converts them to the serial protocol). Notify mode
	// is intentionally excluded — its data path is the receive thread
	// feeding X68000_Joystick_Set(), and its send path blocks waiting
	// for an ack, which must not stall high-frequency port C writes
	// (ADPCM pan). Internal side effects are still handled below.
	if (PPI_JoyportU_InCommandMode()) {
		PPI_Write(adr, data);
	}

	if ( adr==0xe9a005 ) {
		portc = pia.PortC;
		pia.PortC = data;
		if ( (portc&0x0f)!=(pia.PortC&0x0f) ) ADPCM_SetPan(pia.PortC&0x0f);
		if ( (portc&0x10)!=(pia.PortC&0x10) ) Joystick_Write(0, (BYTE)((data&0x10)?0xff:0x00));
		if ( (portc&0x20)!=(pia.PortC&0x20) ) Joystick_Write(1, (BYTE)((data&0x20)?0xff:0x00));
	} else if ( adr==0xe9a007 ) {
		if ( !(data&0x80) ) {
			portc = pia.PortC;
			bit = (data>>1)&7;
			mask = 1<<bit;
			if ( data&1 )
				pia.PortC |= mask;
			else
				pia.PortC &= ~mask;
			if ( (portc&0x0f)!=(pia.PortC&0x0f) ) ADPCM_SetPan(pia.PortC&0x0f);
			if ( (portc&0x10)!=(pia.PortC&0x10) ) Joystick_Write(0, (BYTE)((data&1)?0xff:0x00));
			if ( (portc&0x20)!=(pia.PortC&0x20) ) Joystick_Write(1, (BYTE)((data&1)?0xff:0x00));
		}
	} else if ( adr==0xe9a001 ) {
		Joystick_Write(0, data);
	} else if (adr == 0xe9a003) {
		Joystick_Write(1, data);
	}
}


// -----------------------------------------------------------------------
//   I/O Read
// -----------------------------------------------------------------------
// Reads are NOT redirected to ppi.c: port C is an output latch on the
// X68000 (control word $92), so readback must reflect internal PIA state
// (e.g. ADPCM pan read-modify-write). Ports A/B keep using Joystick_Read()
// — in notify mode the JoyportU receive thread already feeds it via
// X68000_Joystick_Set(), and command-mode read queries (0x3A/0x3B) are
// not implemented in ppi.c.
BYTE FASTCALL PIA_Read(DWORD adr)
{
	if ( adr == 0xe9a001 ) return Joystick_Read(0);
	if ( adr == 0xe9a003 ) return Joystick_Read(1);
	if ( adr == 0xe9a005 ) return pia.PortC;
	return 0xff;
}
