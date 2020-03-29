//
//  Use this file to import your target's public headers that you would like to expose to Swift.
//

void X68000_Init();
void X68000_Update( unsigned char* data );
void X68000_AudioCallBack(void* buffer, const unsigned int sample);
