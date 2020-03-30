//
//  Use this file to import your target's public headers that you would like to expose to Swift.
//

void X68000_Init();
void X68000_Update();
void X68000_GetImage( unsigned char* data );
void X68000_AudioCallBack(void* buffer, const unsigned int sample);
void X68000_Key_Down( unsigned int vkcode );
void X68000_Key_Up( unsigned int vkcode );
const int X68000_GetScreenWidth();
const int X68000_GetScreenHeight();


void X68000_Joystick_Set(BYTE num, BYTE data);
