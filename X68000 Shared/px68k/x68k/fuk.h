// ---------------------------------------------------------------------------------------
//  FUK.H - FLOPPY Ukun (ふろっぴU君, TNB製作所) USB FDC unit
// ---------------------------------------------------------------------------------------

#ifndef _winx68k_fuk_h
#define _winx68k_fuk_h

#ifdef __cplusplus
extern "C" {
#endif

// The unit enumerates as one USB device (VID 0x04D8 / PID 0xE6B1) with two
// CDC-ACM serial ports. Port 1 answers 'f' to a 0x00 byte and carries register
// access; port 2 answers 'F' and carries asynchronous events.
#define FUK_VENDOR_ID  0x04d8
#define FUK_PRODUCT_ID 0xe6b1

// Finds the unit, opens both ports, identifies them and closes them again.
// Returns 1 when port 1 and port 2 were both identified, 0 otherwise.
// A human-readable report of every step is left in FUK_GetStatus().
int FUK_Probe(void);
const char* FUK_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif
