//
//  Use this file to import your target's public headers that you would like to expose to Swift.
//

void X68000_Init( const long samplingrate);
void X68000_Update(const long clockMHz, const long vsync );
void X68000_LoadFDD( const long drive, const char* filename );
void X68000_LoadHDD( const char* filename );
unsigned char* X68000_GetDiskImageBufferPointer( const long drive, const long size );
void X68000_Reset();
void X68000_Quit();
void X68000_GetImage( unsigned char* data );
const int X68000_IsFrameDirty(void);
void X68000_AudioCallBack(void* buffer, const unsigned int sample);
void X68000_Key_Down( unsigned int vkcode );
void X68000_Key_Up( unsigned int vkcode );
const int X68000_GetScreenWidth();
const int X68000_GetScreenHeight();

void X68000_Mouse_Set( float x, float y, long button );
void X68000_Mouse_SetDirect( float x, float y, long button );
void X68000_Mouse_StartCapture(int flag);
void X68000_Mouse_Event(int param, float dx, float dy);
void X68000_Mouse_ResetState(void);
void X68000_Mouse_SetAbsolute(float x, float y);
void X68000_Mouse_SetDoubleClickInProgress(int flag);

unsigned char* X68000_GetSRAMPointer();
unsigned char* X68000_GetCGROMPointer(); // added by Awed 2023/10/7
unsigned char* X68000_GetIPLROMPointer(); // added by Awed 2023/10/7
unsigned char* X68000_GetSCSIIPLPointer();
unsigned char* X68000_GetSASI_IPLROMPointer();


void X68000_Joystick_Set( unsigned char num, unsigned char data);

void X68000_EjectFDD( const long drive );
const int X68000_IsFDDReady( const long drive );
const char* X68000_GetFDDFilename( const long drive );

void X68000_EjectHDD();
const int X68000_IsHDDReady();
const char* X68000_GetHDDFilename();
void X68000_SaveHDD();
const int X68000_IsHDDDirty();

int X68000_GetStorageBusMode();
void X68000_SetStorageBusMode(int mode);
int X68000_SCSI_IsMounted(int host, int id);
const char* X68000_SCSI_GetImagePath(int host, int id);
int X68000_SCSI_Mount(int host, int id, const char* path, int flags);
int X68000_SCSI_Eject(int host, int id);
void Memory_SetSCSIMode(void);
void Memory_ClearSCSIMode(void);

const long X68000_GetMIDIBufferSize();
unsigned char* X68000_GetMIDIBuffer();

// PPI (JoyportU) functions
void PPI_SetJoyportUMode(int mode);
int PPI_GetJoyportUMode(void);

// SCC compatibility toggle (original px68k mouse behavior)
void SCC_SetCompatMode(int enable);
int SCC_GetCompatMode(void);

// SASI mode: update desired RAM size from current SRAM (call after saveSRAM)
void WinX68k_UpdateSASIRamSize(void);
