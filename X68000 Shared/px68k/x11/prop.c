/* 
 * Copyright (c) 2003 NONAKA Kimihiro
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* -------------------------------------------------------------------------- *
 *  PROP.C - 各種設定用プロパティシートと設定値管理                           *
 * -------------------------------------------------------------------------- */

#include <sys/stat.h>

#include "common.h"
#include "winx68k.h"
#include "keyboard.h"
#include "fileio.h"
#include "prop.h"

BYTE	LastCode = 0;
BYTE	KEYCONFFILE[] = "xkeyconf.dat";

int	CurrentHDDNo = 0;

BYTE ini_title[] = "WinX68k";

static const char MIDI_TYPE_NAME[4][3] = {
	"LA", "GM", "GS", "XG"
};

BYTE KeyTableBk[512];

Win68Conf Config;
Win68Conf ConfBk;

#ifndef MAX_BUTTON
#define MAX_BUTTON 32
#endif

//extern char filepath[MAX_PATH];
char    filepath[MAX_PATH] = ".";

extern char winx68k_ini[MAX_PATH];
extern int winx, winy;
extern char joyname[2][MAX_PATH];
extern char joybtnname[2][MAX_BUTTON][MAX_PATH];
extern BYTE joybtnnum[2];

#define CFGLEN MAX_PATH

#if 0
static long solveHEX(char *str) {

	long	ret;
	int		i;
	char	c;

	ret = 0;
	for (i=0; i<8; i++) {
		c = *str++;
		if ((c >= '0') && (c <= '9')) {
			c -= '0';
		}
		else if ((c >= 'A') && (c <= 'F')) {
			c -= '7';
		}
		else if ((c >= 'a') && (c <= 'f')) {
			c -= 'W';
		}
		else {
			break;
		}
		ret <<= 4;
		ret += (long) c;
	}
	return(ret);
}
#endif

static char *makeBOOL(BYTE value) {

	if (value) {
		return("true");
	}
	return("false");
}

static BYTE Aacmp(char *cmp, char *str) {

	char	p;

	while(*str) {
		p = *cmp++;
		if (!p) {
			break;
		}
		if (p >= 'a' && p <= 'z') {
			p -= 0x20;
		}
		if (p != *str++) {
			return(-1);
		}
	}
	return(0);
}

static BYTE solveBOOL(char *str) {

	if ((!Aacmp(str, "TRUE")) || (!Aacmp(str, "ON")) ||
		(!Aacmp(str, "+")) || (!Aacmp(str, "1")) ||
		(!Aacmp(str, "ENABLE"))) {
		return(1);
	}
	return(0);
}

int
set_modulepath(char *path, size_t len)
{
	struct stat sb;
	char *homepath;

#ifdef ANDROID
	const char *extpath;
	char *p, ep_buf[MAX_PATH], p6dir_buf[MAX_PATH];
	FILE *fp;

	p6dir_buf[0] = '\0';
	extpath = SDL_AndroidGetExternalStoragePath();
	p6logd("extpath:%s", extpath);

	// get the Android external path (.ex /storage/emulated/0)
	strcpy(p6dir_buf, extpath);
	p = strstr(p6dir_buf, "/Android/data/com.fc2.blog45.hissorii/files");
	if (p != NULL) {
		*p = '\0';
		p6logd("extpath2:%s", p6dir_buf);

		strcat(p6dir_buf, "/px68k");
		p6logd("p6dir_buf:%s", p6dir_buf);
		// if there is the px68k directory ...
		if (stat(p6dir_buf, &sb) == 0 && S_ISDIR(sb.st_mode)) {
			goto set_dir;
		}
	}

	if (extpath != NULL) {
		// read Android/data/com.fc2.blog45.hissorii/files/dir.txt
		sprintf(ep_buf, "%s/dir.txt", extpath);
		if ((fp = fopen(ep_buf, "r")) != NULL) {
			fgets(p6dir_buf, MAX_PATH - 1, fp);
			p6logd("p6dir:%s", p6dir_buf);
			fclose(fp);
		}
	}

	// if everything failed, try /sdcard/px68k directory
	if (p6dir_buf[0] == '\0') {
		strcpy(p6dir_buf, "/sdcard/px68k");
	}

set_dir:
	strcpy(path, p6dir_buf);
	p6logd("path:%s", path);

	sprintf(winx68k_ini, "%s/config", p6dir_buf);
	p6logd("config:%s", winx68k_ini);

	return 0;
#endif
#if TARGET_OS_IPHONE && TARGET_IPHONE_SIMULATOR == 0
        puts("iPhone...");
        sprintf(path, "ipm");
        sprintf(winx68k_ini, "%s/config",path);
        return 0;
#endif

	homepath = getenv("HOME");
	if (homepath == 0)
		homepath = ".";

	snprintf(path, len, "%s/%s", homepath, ".keropi");
	if (stat(path, &sb) < 0) {
		if (mkdir(path, 0700) < 0) {
			perror(path);
			return 1;
		}
	} else {
		if ((sb.st_mode & S_IFDIR) == 0) {
			fprintf(stderr, "%s isn't directory.\n", path);
			return 1;
		}
	}
	snprintf(winx68k_ini, sizeof(winx68k_ini), "%s/%s", path, "config");
	if (stat(winx68k_ini, &sb) >= 0) {
		if (sb.st_mode & S_IFDIR) {
			fprintf(stderr, "%s is directory.\n", winx68k_ini);
			return 1;
		}
	}

	return 0;
}

void LoadConfig(void)
{
	int	i, j;
	char	buf[CFGLEN];
	FILEH fp;

	winx = GetPrivateProfileInt(ini_title, "WinPosX", 0, winx68k_ini);
	winy = GetPrivateProfileInt(ini_title, "WinPosY", 0, winx68k_ini);

	Config.FrameRate = (BYTE)GetPrivateProfileInt(ini_title, "FrameRate", 7, winx68k_ini);
	if (!Config.FrameRate) Config.FrameRate = 7;
	GetPrivateProfileString(ini_title, "StartDir", "", buf, MAX_PATH, winx68k_ini);
	if (buf[0] != 0)
		strncpy(filepath, buf, sizeof(filepath));
	else
		filepath[0] = 0;

	Config.OPM_VOL = GetPrivateProfileInt(ini_title, "OPM_Volume", 12, winx68k_ini);
	Config.PCM_VOL = GetPrivateProfileInt(ini_title, "PCM_Volume", 15, winx68k_ini);
	Config.MCR_VOL = GetPrivateProfileInt(ini_title, "MCR_Volume", 13, winx68k_ini);
	Config.SampleRate = GetPrivateProfileInt(ini_title, "SampleRate", 44100/2, winx68k_ini);
	Config.BufferSize = GetPrivateProfileInt(ini_title, "BufferSize", 50, winx68k_ini);

	Config.MouseSpeed = GetPrivateProfileInt(ini_title, "MouseSpeed", 10, winx68k_ini);

	GetPrivateProfileString(ini_title, "FDDStatWin", "1", buf, CFGLEN, winx68k_ini);
	Config.WindowFDDStat = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "FDDStatFullScr", "1", buf, CFGLEN, winx68k_ini);
	Config.FullScrFDDStat = solveBOOL(buf);

	GetPrivateProfileString(ini_title, "DSAlert", "1", buf, CFGLEN, winx68k_ini);
	Config.DSAlert = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "SoundLPF", "1", buf, CFGLEN, winx68k_ini);
	Config.Sound_LPF = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "UseRomeo", "0", buf, CFGLEN, winx68k_ini);
	Config.SoundROMEO = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "MIDI_SW", "1", buf, CFGLEN, winx68k_ini);
	Config.MIDI_SW = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "MIDI_Reset", "0", buf, CFGLEN, winx68k_ini);
	Config.MIDI_Reset = solveBOOL(buf);
	Config.MIDI_Type = GetPrivateProfileInt(ini_title, "MIDI_Type", 1, winx68k_ini);

	GetPrivateProfileString(ini_title, "JoySwap", "0", buf, CFGLEN, winx68k_ini);
	Config.JoySwap = solveBOOL(buf);

	GetPrivateProfileString(ini_title, "JoyKey", "0", buf, CFGLEN, winx68k_ini);
	Config.JoyKey = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "JoyKeyReverse", "0", buf, CFGLEN, winx68k_ini);
	Config.JoyKeyReverse = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "JoyKeyJoy2", "0", buf, CFGLEN, winx68k_ini);
	Config.JoyKeyJoy2 = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "SRAMBootWarning", "1", buf, CFGLEN, winx68k_ini);
	Config.SRAMWarning = solveBOOL(buf);

	GetPrivateProfileString(ini_title, "WinDrvLFN", "1", buf, CFGLEN, winx68k_ini);
	Config.LongFileName = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "WinDrvFDD", "1", buf, CFGLEN, winx68k_ini);
	Config.WinDrvFD = solveBOOL(buf);

	Config.WinStrech = GetPrivateProfileInt(ini_title, "WinStretch", 1, winx68k_ini);

	GetPrivateProfileString(ini_title, "DSMixing", "0", buf, CFGLEN, winx68k_ini);
	Config.DSMixing = solveBOOL(buf);

	Config.XVIMode = (BYTE)GetPrivateProfileInt(ini_title, "XVIMode", 3, winx68k_ini);  // 2: 24MHz

	GetPrivateProfileString(ini_title, "CDROM_ASPI", "1", buf, CFGLEN, winx68k_ini);
	Config.CDROM_ASPI = solveBOOL(buf);
	Config.CDROM_SCSIID = (BYTE)GetPrivateProfileInt(ini_title, "CDROM_SCSIID", 6, winx68k_ini);
	Config.CDROM_ASPI_Drive = (BYTE)GetPrivateProfileInt(ini_title, "CDROM_ASPIDrv", 0, winx68k_ini);
	Config.CDROM_IOCTRL_Drive = (BYTE)GetPrivateProfileInt(ini_title, "CDROM_CTRLDrv", 16, winx68k_ini);
	GetPrivateProfileString(ini_title, "CDROM_Enable", "1", buf, CFGLEN, winx68k_ini);
	Config.CDROM_Enable = solveBOOL(buf);

	GetPrivateProfileString(ini_title, "SSTP_Enable", "0", buf, CFGLEN, winx68k_ini);
	Config.SSTP_Enable = solveBOOL(buf);
	Config.SSTP_Port = GetPrivateProfileInt(ini_title, "SSTP_Port", 11000, winx68k_ini);

	GetPrivateProfileString(ini_title, "ToneMapping", "0", buf, CFGLEN, winx68k_ini);
	Config.ToneMap = solveBOOL(buf);
	GetPrivateProfileString(ini_title, "ToneMapFile", "", buf, MAX_PATH, winx68k_ini);
	if (buf[0] != 0)
		strcpy(Config.ToneMapFile, buf);
	else
		Config.ToneMapFile[0] = 0;

	Config.MIDIDelay = GetPrivateProfileInt(ini_title, "MIDIDelay", Config.BufferSize*5, winx68k_ini);
	Config.MIDIAutoDelay = GetPrivateProfileInt(ini_title, "MIDIAutoDelay", 1, winx68k_ini);

	Config.VkeyScale = GetPrivateProfileInt(ini_title, "VkeyScale", 4, winx68k_ini);

	Config.VbtnSwap = GetPrivateProfileInt(ini_title, "VbtnSwap", 0, winx68k_ini);

	Config.JoyOrMouse = GetPrivateProfileInt(ini_title, "JoyOrMouse", 0, winx68k_ini);

	Config.HwJoyAxis[0] = GetPrivateProfileInt(ini_title, "HwJoyAxis0", 0, winx68k_ini);

	Config.HwJoyAxis[1] = GetPrivateProfileInt(ini_title, "HwJoyAxis1", 1, winx68k_ini);

	Config.HwJoyHat = GetPrivateProfileInt(ini_title, "HwJoyHat", 0, winx68k_ini);

	for (i = 0; i < 8; i++) {
		sprintf(buf, "HwJoyBtn%d", i);
		Config.HwJoyBtn[i] = GetPrivateProfileInt(ini_title, buf, i, winx68k_ini);
	}

	Config.NoWaitMode = GetPrivateProfileInt(ini_title, "NoWaitMode", 0, winx68k_ini);

	for (i=0; i<2; i++)
	{
		for (j=0; j<8; j++)
		{
			sprintf(buf, "Joy%dButton%d", i+1, j+1);
			Config.JOY_BTN[i][j] = GetPrivateProfileInt(ini_title, buf, j, winx68k_ini);
		}
	}

	for (i = 0; i < 2; i++) {
		sprintf(buf, "FDD%d", i);
		GetPrivateProfileString(ini_title, buf, "", Config.FDDImage[i], MAX_PATH, winx68k_ini);
	}

	for (i=0; i<16; i++)
	{
		sprintf(buf, "HDD%d", i);
		GetPrivateProfileString(ini_title, buf, "", Config.HDImage[i], MAX_PATH, winx68k_ini);
	}

#if 0
	fp = File_OpenCurDir(KEYCONFFILE);
	if (fp)
	{
		File_Read(fp, KeyTable, 512);
		File_Close(fp);
	}
#endif
}


void SaveConfig(void)
{
	int	i, j;
	char	buf[CFGLEN], buf2[CFGLEN];
	FILEH fp;

	wsprintf(buf, "%d", winx);
	WritePrivateProfileString(ini_title, "WinPosX", buf, winx68k_ini);
	wsprintf(buf, "%d", winy);
	WritePrivateProfileString(ini_title, "WinPosY", buf, winx68k_ini);
	wsprintf(buf, "%d", Config.FrameRate);
	WritePrivateProfileString(ini_title, "FrameRate", buf, winx68k_ini);
	WritePrivateProfileString(ini_title, "StartDir", filepath, winx68k_ini);

	wsprintf(buf, "%d", Config.OPM_VOL);
	WritePrivateProfileString(ini_title, "OPM_Volume", buf, winx68k_ini);
	wsprintf(buf, "%d", Config.PCM_VOL);
	WritePrivateProfileString(ini_title, "PCM_Volume", buf, winx68k_ini);
	wsprintf(buf, "%d", Config.MCR_VOL);
	WritePrivateProfileString(ini_title, "MCR_Volume", buf, winx68k_ini);
	wsprintf(buf, "%d", Config.SampleRate);
	WritePrivateProfileString(ini_title, "SampleRate", buf, winx68k_ini);
	wsprintf(buf, "%d", Config.BufferSize);
	WritePrivateProfileString(ini_title, "BufferSize", buf, winx68k_ini);

	wsprintf(buf, "%d", Config.MouseSpeed);
	WritePrivateProfileString(ini_title, "MouseSpeed", buf, winx68k_ini);

	WritePrivateProfileString(ini_title, "FDDStatWin", makeBOOL((BYTE)Config.WindowFDDStat), winx68k_ini);
	WritePrivateProfileString(ini_title, "FDDStatFullScr", makeBOOL((BYTE)Config.FullScrFDDStat), winx68k_ini);

	WritePrivateProfileString(ini_title, "DSAlert", makeBOOL((BYTE)Config.DSAlert), winx68k_ini);
	WritePrivateProfileString(ini_title, "SoundLPF", makeBOOL((BYTE)Config.Sound_LPF), winx68k_ini);
	WritePrivateProfileString(ini_title, "UseRomeo", makeBOOL((BYTE)Config.SoundROMEO), winx68k_ini);
	WritePrivateProfileString(ini_title, "MIDI_SW", makeBOOL((BYTE)Config.MIDI_SW), winx68k_ini);
	WritePrivateProfileString(ini_title, "MIDI_Reset", makeBOOL((BYTE)Config.MIDI_Reset), winx68k_ini);
	wsprintf(buf, "%d", Config.MIDI_Type);
	WritePrivateProfileString(ini_title, "MIDI_Type", buf, winx68k_ini);

	WritePrivateProfileString(ini_title, "JoySwap", makeBOOL((BYTE)Config.JoySwap), winx68k_ini);

	WritePrivateProfileString(ini_title, "JoyKey", makeBOOL((BYTE)Config.JoyKey), winx68k_ini);
	WritePrivateProfileString(ini_title, "JoyKeyReverse", makeBOOL((BYTE)Config.JoyKeyReverse), winx68k_ini);
	WritePrivateProfileString(ini_title, "JoyKeyJoy2", makeBOOL((BYTE)Config.JoyKeyJoy2), winx68k_ini);
	WritePrivateProfileString(ini_title, "SRAMBootWarning", makeBOOL((BYTE)Config.SRAMWarning), winx68k_ini);

	WritePrivateProfileString(ini_title, "WinDrvLFN", makeBOOL((BYTE)Config.LongFileName), winx68k_ini);
	WritePrivateProfileString(ini_title, "WinDrvFDD", makeBOOL((BYTE)Config.WinDrvFD), winx68k_ini);

	wsprintf(buf, "%d", Config.WinStrech);
	WritePrivateProfileString(ini_title, "WinStretch", buf, winx68k_ini);

	WritePrivateProfileString(ini_title, "DSMixing", makeBOOL((BYTE)Config.DSMixing), winx68k_ini);

	wsprintf(buf, "%d", Config.XVIMode);
	WritePrivateProfileString(ini_title, "XVIMode", buf, winx68k_ini);

	WritePrivateProfileString(ini_title, "CDROM_ASPI", makeBOOL((BYTE)Config.CDROM_ASPI), winx68k_ini);
	wsprintf(buf, "%d", Config.CDROM_SCSIID);
	WritePrivateProfileString(ini_title, "CDROM_SCSIID", buf, winx68k_ini);
	wsprintf(buf, "%d", Config.CDROM_ASPI_Drive);
	WritePrivateProfileString(ini_title, "CDROM_ASPIDrv", buf, winx68k_ini);
	wsprintf(buf, "%d", Config.CDROM_IOCTRL_Drive);
	WritePrivateProfileString(ini_title, "CDROM_CTRLDrv", buf, winx68k_ini);
	WritePrivateProfileString(ini_title, "CDROM_Enable", makeBOOL((BYTE)Config.CDROM_Enable), winx68k_ini);

	WritePrivateProfileString(ini_title, "SSTP_Enable", makeBOOL((BYTE)Config.SSTP_Enable), winx68k_ini);
	wsprintf(buf, "%d", Config.SSTP_Port);
	WritePrivateProfileString(ini_title, "SSTP_Port", buf, winx68k_ini);

	WritePrivateProfileString(ini_title, "ToneMapping", makeBOOL((BYTE)Config.ToneMap), winx68k_ini);
	WritePrivateProfileString(ini_title, "ToneMapFile", Config.ToneMapFile, winx68k_ini);

	wsprintf(buf, "%d", Config.MIDIDelay);
	WritePrivateProfileString(ini_title, "MIDIDelay", buf, winx68k_ini);
	WritePrivateProfileString(ini_title, "MIDIAutoDelay", makeBOOL((BYTE)Config.MIDIAutoDelay), winx68k_ini);

	wsprintf(buf, "%d", Config.VkeyScale);
	WritePrivateProfileString(ini_title, "VkeyScale", buf, winx68k_ini);

	wsprintf(buf, "%d", Config.VbtnSwap);
	WritePrivateProfileString(ini_title, "VbtnSwap", buf, winx68k_ini);

	wsprintf(buf, "%d", Config.JoyOrMouse);
	WritePrivateProfileString(ini_title, "JoyOrMouse", buf, winx68k_ini);

	wsprintf(buf, "%d", Config.HwJoyAxis[0]);
	WritePrivateProfileString(ini_title, "HwJoyAxis0", buf, winx68k_ini);

	wsprintf(buf, "%d", Config.HwJoyAxis[1]);
	WritePrivateProfileString(ini_title, "HwJoyAxis1", buf, winx68k_ini);

	wsprintf(buf, "%d", Config.HwJoyHat);
	WritePrivateProfileString(ini_title, "HwJoyHat", buf, winx68k_ini);

	for (i = 0; i < 8; i++) {
		sprintf(buf, "HwJoyBtn%d", i);
		sprintf(buf2, "%d", Config.HwJoyBtn[i]);
		WritePrivateProfileString(ini_title, buf, buf2, winx68k_ini);
	}

	wsprintf(buf, "%d", Config.NoWaitMode);
	WritePrivateProfileString(ini_title, "NoWaitMode", buf, winx68k_ini);

	for (i=0; i<2; i++)
	{
		for (j=0; j<8; j++)
		{
			sprintf(buf, "Joy%dButton%d", i+1, j+1);
			wsprintf(buf2, "%d", Config.JOY_BTN[i][j]);
			WritePrivateProfileString(ini_title, buf, buf2, winx68k_ini);
		}
	}

	for (i = 0; i < 2; i++)
	{
		printf("i: %d", i);
		sprintf(buf, "FDD%d", i);
		WritePrivateProfileString(ini_title, buf, Config.FDDImage[i], winx68k_ini);
	}

	for (i=0; i<16; i++)
	{
		sprintf(buf, "HDD%d", i);
		WritePrivateProfileString(ini_title, buf, Config.HDImage[i], winx68k_ini);
	}

#if 0
	fp = File_OpenCurDir(KEYCONFFILE);
	if (!fp)
		fp = File_CreateCurDir(KEYCONFFILE, FTYPE_TEXT);
	if (fp)
	{
		File_Write(fp, KeyTable, 512);
		File_Close(fp);
	}
#endif
}


/* --------------- */

