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
void X68000_AudioCallBack(void* buffer, const unsigned int sample);
void X68000_Key_Down( unsigned int vkcode );
void X68000_Key_Up( unsigned int vkcode );
const int X68000_GetScreenWidth();
const int X68000_GetScreenHeight();

void X68000_Mouse_Set( float x, float y, long button );
void X68000_Mouse_SetDirect( float x, float y, long button );

unsigned char* X68000_GetSRAMPointer();
unsigned char* X68000_GetCGROMPointer(); // added by Awed 2023/10/7
unsigned char* X68000_GetIPLROMPointer(); // added by Awed 2023/10/7


void X68000_Joystick_Set( unsigned char num, unsigned char data);

void X68000_EjectFDD( const long drive );
const int X68000_IsFDDReady( const long drive );
const char* X68000_GetFDDFilename( const long drive );

void X68000_EjectHDD();
const int X68000_IsHDDReady();
const char* X68000_GetHDDFilename();

const long X68000_GetMIDIBufferSize();
unsigned char* X68000_GetMIDIBuffer();

