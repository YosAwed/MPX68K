// ---------------------------------------------------------------------------------------
//  SYSPORT.C - X68k System Port
// ---------------------------------------------------------------------------------------

#include "common.h"
#include "prop.h"
#include "sysport.h"
#include "palette.h"
#include "crtc.h"

BYTE	SysPort[7];

// CPU clock the frame loop runs at, for the $E8E00B machine-class read.
static long SysPort_ClockMHz = 10;

// Guest software power-off: writing $00, $0F, $0F to $E8E00F asks the power
// supply to switch off (XM6/XEiJ behaviour). Once fired, the machine stays
// off until SysPort_ResetPowerOff(), which every reset path goes through, so
// the front end can treat SysPort_IsPoweredOff() as the one source of truth.
static int SysPort_PowerOffStep = 0;

// -----------------------------------------------------------------------
//   初期化
// -----------------------------------------------------------------------
void SysPort_Init(void)
{
	int i;
	for (i=0; i<7; i++) SysPort[i]=0;
	SysPort_ResetPowerOff();
}

void SysPort_SetClockMHz(long mhz)
{
	SysPort_ClockMHz = mhz;
}

void SysPort_ResetPowerOff(void)
{
	SysPort_PowerOffStep = 0;
}

int SysPort_IsPoweredOff(void)
{
	return SysPort_PowerOffStep == 3;
}

static void SysPort_TrackPowerOff(BYTE data)
{
	if (SysPort_PowerOffStep == 3) return;
	if (SysPort_PowerOffStep == 0 && data == 0x00) {
		SysPort_PowerOffStep = 1;
	} else if (SysPort_PowerOffStep == 1 && data == 0x0f) {
		SysPort_PowerOffStep = 2;
	} else if (SysPort_PowerOffStep == 2 && data == 0x0f) {
		SysPort_PowerOffStep = 3;
	} else {
		SysPort_PowerOffStep = 0;
	}
}


// -----------------------------------------------------------------------
//   らいと
// -----------------------------------------------------------------------
void FASTCALL SysPort_Write(DWORD adr, BYTE data)
{
	switch(adr)
	{
	case 0xe8e001:
		if (SysPort[1]!=(data&15))
		{
			SysPort[1] = data & 15;
			Pal_ChangeContrast(SysPort[1]);
		}
		break;
	case 0xe8e003:
		SysPort[2] = data & 0x0b;
		break;
	case 0xe8e005:
		SysPort[3] = data & 0x1f;
		break;
	case 0xe8e007:
		if ((SysPort[4] ^ data) & 0x02) {
			SysPort[4] = data & 0x0e;
			CRTC_UpdateHSyncClock();
		} else {
			SysPort[4] = data & 0x0e;
		}
		break;
	case 0xe8e00d:
		SysPort[5] = data;
		break;
	case 0xe8e00f:
		SysPort[6] = data & 15;
		SysPort_TrackPowerOff(SysPort[6]);
		break;
	}
}


// -----------------------------------------------------------------------
//   りーど
// -----------------------------------------------------------------------
BYTE FASTCALL SysPort_Read(DWORD adr)
{
	BYTE ret=0xff;

	switch(adr)
	{
	case 0xe8e001:
		ret = SysPort[1];
		break;
	case 0xe8e003:
		ret = SysPort[2];
		break;
	case 0xe8e005:
		ret = SysPort[3];
		break;
	case 0xe8e007:
		ret = SysPort[4];
		break;
	case 0xe8e00b:		// 10MHz:0xff、16MHz:0xfe、030(25MHz):0xdcをそれぞれ返すらしい
		// Derive the class from the running clock. 16 MHz is the stock
		// SUPER/XVI/Compact and 24 MHz the RedZone (modified XVI); every other
		// clock is a 10 MHz-class machine. 0xdc (030) is never returned since
		// the emulated CPU is a 68000.
		switch(SysPort_ClockMHz)
		{
		case 16:
		case 24:
			ret = 0xfe;
			break;
		default:
			ret = 0xff;
			break;
		}
		break;
	case 0xe8e00d:
		ret = SysPort[5];
		break;
	case 0xe8e00f:
		ret = SysPort[6];
		break;
	}

	return ret;
}
