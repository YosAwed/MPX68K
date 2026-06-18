/*
 * Minimal host-side stubs so that emulation-core translation units can be
 * unit-tested off-device (Linux/macOS CI) without the rest of the emulator.
 */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "fileio.h"

/* ---- fdd.h ---- */
static int s_read_only[4];

void FDD_SetReadOnly(int drv)
{
    if (drv >= 0 && drv < 4)
        s_read_only[drv] = 1;
}

int FDD_IsReadOnly(int drv)
{
    if (drv >= 0 && drv < 4)
        return s_read_only[drv];
    return 0;
}

void stub_reset_read_only(void)
{
    memset(s_read_only, 0, sizeof(s_read_only));
}

/* ---- dosio.h (file_* used via the File_* macros in fileio.h) ---- */
FILEH file_open(LPSTR filename)
{
    return (FILEH)fopen((const char *)filename, "r+b");
}

DWORD file_seek(FILEH handle, long pointer, short mode)
{
    int whence = (mode == FSEEK_SET) ? SEEK_SET
               : (mode == FSEEK_CUR) ? SEEK_CUR
               : SEEK_END;
    if (fseek((FILE *)handle, pointer, whence) != 0)
        return (DWORD)-1;
    return (DWORD)ftell((FILE *)handle);
}

DWORD file_lread(FILEH handle, void *data, DWORD length)
{
    return (DWORD)fread(data, 1, length, (FILE *)handle);
}

DWORD file_lwrite(FILEH handle, void *data, DWORD length)
{
    return (DWORD)fwrite(data, 1, length, (FILE *)handle);
}

short file_close(FILEH handle)
{
    return (short)fclose((FILE *)handle);
}
