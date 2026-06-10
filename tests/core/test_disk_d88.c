/*
 * Unit tests for the D88 floppy image parser (x68k/disk_d88.c).
 *
 * D88 images are untrusted user input, so the parser must reject
 * malformed headers instead of overrunning buffers or allocating
 * unbounded memory. These tests craft images on disk and feed them
 * through D88_SetFD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "fdd.h"
#include "disk_d88.h"

extern void stub_reset_read_only(void);

#define D88_HEADER_SIZE 0x2B0
#define D88_SECTOR_HDR_SIZE 16

static int g_failures = 0;

#define CHECK(cond, name) do { \
	if (cond) { \
		printf("PASS: %s\n", name); \
	} else { \
		printf("FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); \
		g_failures++; \
	} \
} while (0)

/* Serialize a D88 header + one track with `sectors` sector entries of
 * `sector_size` bytes each into `path`. Field offsets follow the D88
 * on-disk layout (name[17] res[9] protect type fd_size trackp[164]). */
static void write_image(const char *path, WORD sectors, WORD sector_size,
                        DWORD track0_ptr)
{
	BYTE header[D88_HEADER_SIZE];
	FILE *fp = fopen(path, "wb");
	DWORD fd_size;
	WORD s;

	if (!fp) {
		perror("fopen");
		exit(1);
	}

	fd_size = D88_HEADER_SIZE +
	          (DWORD)sectors * (D88_SECTOR_HDR_SIZE + sector_size);

	memset(header, 0, sizeof(header));
	memcpy(header, "TESTDISK", 8);            /* fd_name */
	/* offset 26: protect, 27: fd_type, 28: fd_size, 32: trackp[164] */
	memcpy(header + 28, &fd_size, 4);
	memcpy(header + 32, &track0_ptr, 4);      /* trackp[0] */
	fwrite(header, 1, sizeof(header), fp);

	for (s = 0; s < sectors; s++) {
		BYTE sect_hdr[D88_SECTOR_HDR_SIZE];
		BYTE *data = calloc(1, sector_size);

		memset(sect_hdr, 0, sizeof(sect_hdr));
		sect_hdr[0] = 0;                  /* c */
		sect_hdr[1] = 0;                  /* h */
		sect_hdr[2] = (BYTE)(s + 1);      /* r */
		sect_hdr[3] = 1;                  /* n (256 bytes) */
		memcpy(sect_hdr + 4, &sectors, 2);     /* sectors per track */
		memcpy(sect_hdr + 14, &sector_size, 2);/* sector size */
		fwrite(sect_hdr, 1, sizeof(sect_hdr), fp);
		fwrite(data, 1, sector_size, fp);
		free(data);
	}
	fclose(fp);
}

static int load_image(const char *path)
{
	int result;

	stub_reset_read_only();
	D88_Init();
	result = D88_SetFD(0, (char *)path);
	D88_Eject(0);
	return result;
}

int main(void)
{
	const char *path = "_test_image.d88";

	/* 1. Well-formed image: 8 sectors x 256 bytes on track 0 */
	write_image(path, 8, 256, D88_HEADER_SIZE);
	CHECK(load_image(path) == TRUE, "valid image accepted");

	/* 2. Sector size beyond MAX_SECTOR_SIZE (8192) must be rejected */
	write_image(path, 1, 16384, D88_HEADER_SIZE);
	CHECK(load_image(path) == FALSE, "oversized sector rejected");

	/* 3. Sector count of zero must be rejected */
	write_image(path, 0, 256, D88_HEADER_SIZE);
	/* with sectors=0 no sector data is emitted, so craft one header
	 * manually claiming 0 sectors */
	{
		FILE *fp = fopen(path, "r+b");
		BYTE sect_hdr[D88_SECTOR_HDR_SIZE];
		WORD zero = 0, size = 256;
		DWORD fd_size = D88_HEADER_SIZE + D88_SECTOR_HDR_SIZE + 256;
		memset(sect_hdr, 0, sizeof(sect_hdr));
		memcpy(sect_hdr + 4, &zero, 2);
		memcpy(sect_hdr + 14, &size, 2);
		fseek(fp, 28, SEEK_SET);
		fwrite(&fd_size, 1, 4, fp);
		fseek(fp, D88_HEADER_SIZE, SEEK_SET);
		fwrite(sect_hdr, 1, sizeof(sect_hdr), fp);
		fclose(fp);
	}
	CHECK(load_image(path) == FALSE, "zero sector count rejected");

	/* 4. Sector count above MAX_SECTORS_PER_TRACK (64) must be rejected */
	write_image(path, 100, 256, D88_HEADER_SIZE);
	CHECK(load_image(path) == FALSE, "excessive sector count rejected");

	/* 5. Track pointer outside the image: track is skipped, disk loads */
	write_image(path, 8, 256, 0x7FFFFFFF);
	CHECK(load_image(path) == TRUE, "out-of-range track pointer skipped");

	/* 6. Truncated image: header promises sectors the file doesn't have */
	write_image(path, 8, 256, D88_HEADER_SIZE);
	{
		FILE *fp = fopen(path, "r+b");
		DWORD fd_size = D88_HEADER_SIZE + 8 * (D88_SECTOR_HDR_SIZE + 256);
		fseek(fp, 28, SEEK_SET);
		fwrite(&fd_size, 1, 4, fp);
		fclose(fp);
		truncate(path, D88_HEADER_SIZE + 2 * (D88_SECTOR_HDR_SIZE + 256));
	}
	CHECK(load_image(path) == FALSE, "truncated image rejected");

	remove(path);

	if (g_failures) {
		printf("\n%d test(s) FAILED\n", g_failures);
		return 1;
	}
	printf("\nAll tests passed\n");
	return 0;
}
