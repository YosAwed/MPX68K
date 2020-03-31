// ciscタンノエロガゾウキボンヌを強引にけろぴーに繋ぐための
// extern "C" の入れ方がきちゃなくてステキ（ぉ

// readme.txtに従って、改変点：
//  - opna.cppにYMF288用のクラス追加してます。OPNAそのまんまだけどね（ほんとは正しくないがまあいいや）
//  - 多分他は弄ってないはず……

extern "C" {

#include "common.h"
#include "winx68k.h"
#include "dswin.h"
#include "prop.h"
//#include "juliet.h"
#include "mfp.h"
#include "adpcm.h"
//#include "mercury.h"
#include "fdc.h"
#include "fmg_wrap.h"

#include "opm.h"
//#include "opna.h"

/*
#define RMBUFSIZE (256*1024)

typedef struct {
	unsigned int time;
	int reg;
	BYTE data;
} RMDATA;
*/
};
/*
static RMDATA RMData[RMBUFSIZE];
static int RMPtrW;
static int RMPtrR;
*/
class MyOPM : public FM::OPM
{
public:
	MyOPM();
	virtual ~MyOPM() {}
	void WriteIO(DWORD adr, BYTE data);
	void Count2(DWORD clock);
private:
	virtual void Intr(bool);
	int CurReg;
	DWORD CurCount;
};


MyOPM::MyOPM()
{
	CurReg = 0;
}

void MyOPM::WriteIO(DWORD adr, BYTE data)
{
	if( adr&1 ) {
		if ( CurReg==0x1b ) {
			::ADPCM_SetClock((data>>5)&4);
			::FDC_SetForceReady((data>>6)&1);
		}
		SetReg((int)CurReg, (int)data);
#if 0
		if ( (juliet_YM2151IsEnable())&&(Config.SoundROMEO) ) {
			int newptr = (RMPtrW+1)%RMBUFSIZE;
			if ( newptr!=RMPtrR ) {
				OPM_RomeoOut(Config.BufferSize*5);
			}
			RMData[RMPtrW].time = timeGetTime();
			RMData[RMPtrW].reg  = CurReg;
if ( CurReg==0x14 ) data &= 0xf3;	// Int Enableはマスクする
			RMData[RMPtrW].data = data;
			RMPtrW = newptr;
		}
#endif
        
    } else {
		CurReg = (int)data;
	}
}

void MyOPM::Intr(bool f)
{
	if ( f ) ::MFP_Int(12);
}


void MyOPM::Count2(DWORD clock)
{
	CurCount += clock;
	Count(CurCount/10);
	CurCount %= 10;
}


static MyOPM* opm = NULL;

int OPM_Init(int clock, int rate)
{
/*
	juliet_load();
	juliet_prepare();

	RMPtrW = RMPtrR = 0;
	memset(RMData, 0, sizeof(RMData));
*/
	opm = new MyOPM();
	if ( !opm ) return FALSE;
	if ( !opm->Init(clock, rate, TRUE) ) {
		delete opm;
		opm = NULL;
		return FALSE;
	}
	return TRUE;
}


void OPM_Cleanup(void)
{
/*
    juliet_YM2151Reset();
	juliet_unload();
*/
    delete opm;
	opm = NULL;
}


void OPM_SetRate(int clock, int rate)
{
	if ( opm ) opm->SetRate(clock, rate, TRUE);
}


void OPM_Reset(void)
{
/*
	RMPtrW = RMPtrR = 0;
	memset(RMData, 0, sizeof(RMData));
*/
	if ( opm ) opm->Reset();
/*
 juliet_YM2151Reset();
 */
}


BYTE FASTCALL OPM_Read(WORD adr)
{
	BYTE ret = 0;
	(void)adr;
	if ( opm ) ret = opm->ReadStatus();
/*
	if ( (juliet_YM2151IsEnable())&&(Config.SoundROMEO) ) {
		int newptr = (RMPtrW+1)%RMBUFSIZE;
		ret = (ret&0x7f)|((newptr==RMPtrR)?0x80:0x00);
	}
*/
    return ret;
}


void FASTCALL OPM_Write(DWORD adr, BYTE data)
{
	if ( opm ) opm->WriteIO(adr, data);
}


void OPM_Update(short *buffer, int length, int rate, BYTE *pbsp, BYTE *pbep)
{
//@	if ( (!juliet_YM2151IsEnable())||(!Config.SoundROMEO) )
		if ( opm ) opm->Mix((FM::Sample*)buffer, length, rate, pbsp, pbep);
}


void FASTCALL OPM_Timer(DWORD step)
{
	if ( opm ) opm->Count2(step);
}


void OPM_SetVolume(BYTE vol)
{
	int v = (vol)?((16-vol)*4):192;		// このくらいかなぁ
	if ( opm ) opm->SetVolume(-v);
}
