// ---------------------------------------------------------------------------------------
//  SCSI.C - 外付けSCSIボード (CZ-6BS1) + SCSI IOCS エミュレーション
//    SCSI IOCSをトラップする形で対応（SPCはエミュレートしない）
//    ブートおよびIOCSコールは 0xE9F800 へのトラップ書き込みでC側で処理
// ---------------------------------------------------------------------------------------

#include	"common.h"
#include	"fileio.h"
#include	"winx68k.h"
#include	"m68000.h"
#include	"scsi.h"
#include	"sasi.h"
#include	"x68kmemory.h"
#include	"crtc.h"
#include	"palette.h"
#include	"tvram.h"
#include	"../x11/keyboard.h"
#include	<stdio.h>
#include	<stdarg.h>
#include	<stdlib.h>
#include	<sys/stat.h>
#include	<sys/types.h>
#include	<string.h>
#include	<limits.h>

#ifdef __APPLE__
#include	<unistd.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<pthread.h>
#include	<sys/termios.h>
#include	<IOKit/IOKitLib.h>
#include	<IOKit/usb/IOUSBLib.h>
#include	<IOKit/serial/IOSerialKeys.h>
#include	<CoreFoundation/CoreFoundation.h>
#endif

#if defined(HAVE_C68K)
#include	"../m68000/c68k/c68k.h"
extern c68k_struc C68K;
#endif

	BYTE	SCSIIPL[0x2000];
	BYTE	SCSIROM_DAT[0x2000];

extern BYTE* s_disk_image_buffer[5];
extern long s_disk_image_buffer_size[5];
static int s_spc_access_log_count = 0;
static int s_iocs_data_offset_cache_valid = 0;
static DWORD s_iocs_data_offset_cache = 0;
static int s_last_iocs_sig_valid = 0;
static DWORD s_last_iocs_sig_addr = 0;
static BYTE s_last_iocs_sig_cmd = 0;
static DWORD s_last_iocs_sig_d2 = 0;
static DWORD s_last_iocs_sig_d3 = 0;
static DWORD s_last_iocs_sig_d4 = 0;
static DWORD s_last_iocs_sig_d5 = 0;
static DWORD s_last_iocs_sig_a1 = 0;
static int s_scsi_log_total = 0;        // global log line counter
#define SCSI_LOG_LIMIT 50000             // stop logging after this many lines

#ifdef __APPLE__
#define SCSIU_VENDOR_ID  0x04d8
#define SCSIU_PRODUCT_ID 0xE6B2
#define SCSIU_MAX_PORTS  8
#define SCSIU_PATH_LEN   256

typedef struct {
	int control_fd;
	int interrupt_fd;
	pthread_t interrupt_thread;
	pthread_mutex_t mutex;
	int connected;
	int active;
	unsigned int interrupt_count;
	char control_path[SCSIU_PATH_LEN];
	char interrupt_path[SCSIU_PATH_LEN];
	char status[128];
} SCSIU_BridgeState;

static SCSIU_BridgeState s_scsi_u_bridge = {
	-1, -1, 0, PTHREAD_MUTEX_INITIALIZER, 0, 0, 0, "", "", "Disconnected"
};
#endif

// Runtime file logging is disabled by default — fopen/fprintf/fclose on every
// IOCS trap and SPC register poll stalls the main thread. Enable with
// -DMPX68K_ENABLE_RUNTIME_FILE_LOGS=1 only when debugging SCSI boot.
#ifndef MPX68K_ENABLE_RUNTIME_FILE_LOGS
#define MPX68K_ENABLE_RUNTIME_FILE_LOGS 0
#endif

// Forward declarations
void SCSI_LogText(const char* text);

#ifdef __APPLE__
static void SCSIU_SetStatusLocked(const char* fmt, ...);
static int SCSIU_FindCandidatePorts(char paths[SCSIU_MAX_PORTS][SCSIU_PATH_LEN], int maxPorts);
static int SCSIU_OpenPort(const char* path);
static int SCSIU_ProbePortRole(int fd, char* outRole);
static void* SCSIU_InterruptThread(void* arg);
static BYTE SCSIU_ExecSPCReg(BYTE regAddr);
static void SCSIU_SendSPCReg(BYTE regAddr, BYTE val);
static int SCSIU_ExecDregBurst(BYTE* buf, DWORD maxLen);
static int SCSIU_ReadBlocks(DWORD lba, DWORD count, DWORD blockSize, BYTE* buf);
static int SCSIU_WriteBlocks(DWORD lba, DWORD count, DWORD blockSize, const BYTE* buf);
static int SCSIU_EnsureBootCache(void);
static int SCSIU_ReadCapacity(DWORD* outLastLBA, DWORD* outBlockSize);
static void SCSIU_ResetBootCache(void);
static void SCSIU_InvalidateBootCacheRange(DWORD lba, DWORD count, DWORD blockSize);
#endif
static BYTE* SCSI_ImgBuf(void);
static long  SCSI_ImgSize(void);


// Minimal SPC (MB89352) register state for driver initialization.
// Human68k's SCSI driver writes to SCTL/BDID and reads back to verify
// the SPC chip exists.  Without this, the driver assumes "no SPC" and
// disables SCSI disk access entirely.
static BYTE s_spc_regs[0x20];  // mirrors for SPC register writes

// Low-level SCSI IOCS state machine.
// Tracks the command sequence: _S_SELECT → _S_CMDOUT → _S_DATAIN/OUT
// → _S_STSIN → _S_MSGIN.  Data transfers use pSCSI-style immediate
// memory copy (no DMA emulation).
static BYTE  s_spc_cdb[16];         // Command Descriptor Block buffer
static DWORD s_spc_cdb_len;         // bytes received in CDB
static BYTE  s_spc_target_id;       // selected target ID
static BYTE  s_spc_status_byte;     // status for _S_STSIN (0x00=GOOD)
static BYTE  s_spc_message_byte;    // message for _S_MSGIN (0x00=Command Complete)
static int   s_spc_cmd_valid;       // 1 if CDB has been received and parsed

// Block device driver state
static DWORD s_scsi_dev_reqpkt = 0;   // saved A5 from strategy call
static int s_scsi_dev_linked = 0;     // device chain linked flag
static int s_scsi_boot_done = 0;      // boot already executed this session
static DWORD s_scsi_partition_byte_offset = 0; // partition start in image (bytes)
static DWORD s_scsi_sector_size = 1024;        // logical sector size from BPB
static DWORD s_scsi_bpb_ram_addr = 0;
static DWORD s_scsi_bpbptr_ram_addr = 0;
static BYTE s_scsi_drvchk_state = 0x00;        // Fixed disk: no changeable media flag
static BYTE s_scsi_devhdr_next[4] = { 0xFF, 0xFF, 0xFF, 0xFF }; // Writable shadow of $EA0100-$EA0103
static DWORD s_scsi_root_dir_start_sector = 0;
static DWORD s_scsi_root_dir_sector_count = 0;
// -1: unknown, 0: partition-relative sectors, 1: absolute sectors
static int s_scsi_dev_absolute_sectors = -1;
static DWORD s_scsi_fat_start_sector = 0;      // FAT start sector (from BPB)
static DWORD s_scsi_fat_sectors = 0;            // sectors per FAT
static DWORD s_scsi_fat_count = 0;              // number of FATs
static DWORD s_scsi_total_sectors = 0;          // partition sectors (BPB nsize/huge)
static int s_scsi_need_fat16to12 = 0;           // FAT16→FAT12 conversion needed
static int s_scsi_fat16_fixed = 0;              // FAT16 DPB fix applied
static DWORD s_scsi_config_sector = 0;          // CONFIG.SYS sector number
static DWORD s_scsi_config_size = 0;            // CONFIG.SYS file size (bytes)
static int s_scsi_config_read_count = 0;        // CONFIG.SYS read counter
static int s_scsi_config_boot_phase = 1;        // 1=boot phase, 0=normal
static int s_scsi_media_check_count = 0;        // MEDIA CHECK call counter
static DWORD s_scsi_data_start_sector = 0;      // first data sector (partition-relative)
static BYTE s_scsi_sec_per_clus = 1;            // sectors per cluster
static DWORD s_scsi_dpb_addr = 0;               // cached DPB address for our device
static int s_scsi_boot_activity = 0;            // observed boot/IOCS trap traffic
static int s_scsi_driver_activity = 0;          // observed block-driver trap traffic
static int s_scsi_deferred_boot_pending = 0;    // commit boot jump after C68K slice returns
static DWORD s_scsi_deferred_boot_addr = 0;     // pending boot entry address
static DWORD s_scsi_deferred_d5 = 0;            // pending sector-size code

#define SCSI_BPB_RAM_ADDR    0x000C00
#define SCSI_BPBPTR_RAM_ADDR 0x000C30

static void SCSI_LogInit(void);
static int SCSI_HasExternalROM(void);
static void SCSI_HandleBoot(void);
static void SCSI_HandleIOCS(BYTE cmd);
static void SCSI_HandleDeviceCommand(void);
static void SCSI_LogDevicePacket(DWORD reqpkt, BYTE len);
static DWORD SCSI_FindPartitionBootOffset(void);
static DWORD SCSI_DetectBootBlockSize(DWORD bootOffset);
static int SCSI_ReadBPBFromImage(BYTE* outBpb, DWORD* outPartOffset);
void SCSI_LogText(const char* text);
static void SCSI_LogSPCAccess(const char* op, DWORD adr, BYTE data);
static DWORD SCSI_GetPayloadOffset(void);
static DWORD SCSI_GetBlockSizeFromCode(DWORD sizeCode);
static DWORD SCSI_GetImageBlockSize(void);
static DWORD SCSI_GetIocsDataOffset(void);
static DWORD SCSI_GetTransferLBA(BYTE cmd, DWORD d2, DWORD d4);
static int SCSI_ResolveTransfer(DWORD lba, DWORD blocks, DWORD blockSize,
                                DWORD* imageOffset, DWORD* transferBytes);
static int SCSI_IsLinearRamRange(DWORD addr, DWORD len);
static DWORD SCSI_Mask24(DWORD addr);
static void SCSI_NormalizeRootShortNames(DWORD bufAddr, DWORD startSec,
                                         DWORD count, DWORD secSize);
static void SCSI_LogKernelQueueState(const char* tag);

#ifdef __APPLE__
static void
SCSIU_SetStatusLocked(const char* fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(s_scsi_u_bridge.status, sizeof(s_scsi_u_bridge.status), fmt, ap);
	va_end(ap);
}

static int
SCSIU_CopyDialinPath(io_object_t service, char* outPath, int outSize)
{
	CFTypeRef devicePathRef;
	Boolean ok;

	devicePathRef = IORegistryEntryCreateCFProperty(service, CFSTR(kIODialinDeviceKey), kCFAllocatorDefault, 0);
	if (devicePathRef == NULL) {
		return 0;
	}
	ok = CFGetTypeID(devicePathRef) == CFStringGetTypeID() &&
	     CFStringGetCString((CFStringRef)devicePathRef, outPath, outSize, kCFStringEncodingUTF8);
	CFRelease(devicePathRef);
	return ok ? 1 : 0;
}

static int
SCSIU_FindCandidatePorts(char paths[SCSIU_MAX_PORTS][SCSIU_PATH_LEN], int maxPorts)
{
	CFMutableDictionaryRef matchDict;
	CFNumberRef vendorRef;
	CFNumberRef productRef;
	SInt32 vendorId;
	SInt32 productId;
	io_iterator_t iterator;
	io_object_t usbDevice;
	int count;

	matchDict = IOServiceMatching(kIOUSBDeviceClassName);
	if (matchDict == NULL) {
		return 0;
	}

	vendorId = SCSIU_VENDOR_ID;
	productId = SCSIU_PRODUCT_ID;
	vendorRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vendorId);
	productRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &productId);
	if (vendorRef == NULL || productRef == NULL) {
		if (vendorRef != NULL) CFRelease(vendorRef);
		if (productRef != NULL) CFRelease(productRef);
		return 0;
	}

	CFDictionarySetValue(matchDict, CFSTR(kUSBVendorID), vendorRef);
	CFDictionarySetValue(matchDict, CFSTR(kUSBProductID), productRef);
	CFRelease(vendorRef);
	CFRelease(productRef);

	if (IOServiceGetMatchingServices(kIOMainPortDefault, matchDict, &iterator) != KERN_SUCCESS) {
		return 0;
	}

	count = 0;
	while ((usbDevice = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
		io_iterator_t childIterator;
		if (IORegistryEntryCreateIterator(usbDevice, kIOServicePlane, kIORegistryIterateRecursively, &childIterator) == KERN_SUCCESS) {
			io_object_t childService;
			while ((childService = IOIteratorNext(childIterator)) != IO_OBJECT_NULL) {
				char devicePath[SCSIU_PATH_LEN];
				int duplicate;
				int i;

				if (!SCSIU_CopyDialinPath(childService, devicePath, sizeof(devicePath))) {
					IOObjectRelease(childService);
					continue;
				}

				duplicate = 0;
				for (i = 0; i < count; ++i) {
					if (strncmp(paths[i], devicePath, SCSIU_PATH_LEN) == 0) {
						duplicate = 1;
						break;
					}
				}
				if (!duplicate && count < maxPorts) {
					strncpy(paths[count], devicePath, SCSIU_PATH_LEN - 1);
					paths[count][SCSIU_PATH_LEN - 1] = '\0';
					++count;
				}
				IOObjectRelease(childService);
			}
			IOObjectRelease(childIterator);
		}
		IOObjectRelease(usbDevice);
	}

	IOObjectRelease(iterator);
	return count;
}

static int
SCSIU_OpenPort(const char* path)
{
	int fd;
	struct termios tty;

	fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		return -1;
	}
	if (tcgetattr(fd, &tty) < 0) {
		close(fd);
		return -1;
	}

	cfmakeraw(&tty);
	cfsetispeed(&tty, B230400);
	cfsetospeed(&tty, B230400);
	tty.c_cflag &= ~PARENB;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CSIZE;
	tty.c_cflag |= CS8;
	tty.c_cflag |= CREAD | CLOCAL;
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW, &tty) < 0) {
		close(fd);
		return -1;
	}

	tcflush(fd, TCIOFLUSH);
	return fd;
}

static int
SCSIU_ProbePortRole(int fd, char* outRole)
{
	unsigned char request;
	unsigned char response;
	int i;

	request = 0x00;
	response = 0x00;
	tcflush(fd, TCIOFLUSH);
	if (write(fd, &request, 1) != 1) {
		return 0;
	}

	for (i = 0; i < 50; ++i) {
		ssize_t bytesRead = read(fd, &response, 1);
		if (bytesRead == 1) {
			if (response == 's' || response == 'S') {
				*outRole = (char)response;
				return 1;
			}
		} else if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			return 0;
		}
		usleep(10000);
	}

	return 0;
}

static void*
SCSIU_InterruptThread(void* arg)
{
	(void)arg;
	while (s_scsi_u_bridge.active) {
		unsigned char data;
		ssize_t bytesRead = read(s_scsi_u_bridge.interrupt_fd, &data, 1);
		if (bytesRead == 1) {
			if (data == 'I') {
				pthread_mutex_lock(&s_scsi_u_bridge.mutex);
				s_scsi_u_bridge.interrupt_count += 1;
				SCSIU_SetStatusLocked("Connected (%u IRQ)", s_scsi_u_bridge.interrupt_count);
				pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
			}
		} else if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			pthread_mutex_lock(&s_scsi_u_bridge.mutex);
			SCSIU_SetStatusLocked("Interrupt port read error");
			pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
			break;
		}
		usleep(1000);
	}
	return NULL;
}
#endif

int
SCSIU_InitBridge(void)
{
#ifdef __APPLE__
	char paths[SCSIU_MAX_PORTS][SCSIU_PATH_LEN];
	int portCount;
	int i;
	int controlFd;
	int interruptFd;

	pthread_mutex_lock(&s_scsi_u_bridge.mutex);
	if (s_scsi_u_bridge.connected) {
		SCSIU_SetStatusLocked("Connected");
		pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
		return 1;
	}
	s_scsi_u_bridge.interrupt_count = 0;
	SCSIU_SetStatusLocked("Searching for SCSI-U");
	pthread_mutex_unlock(&s_scsi_u_bridge.mutex);

	memset(paths, 0, sizeof(paths));
	portCount = SCSIU_FindCandidatePorts(paths, SCSIU_MAX_PORTS);
	if (portCount < 2) {
		pthread_mutex_lock(&s_scsi_u_bridge.mutex);
		SCSIU_SetStatusLocked("SCSI-U serial ports not found");
		pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
		return 0;
	}

	controlFd = -1;
	interruptFd = -1;
	for (i = 0; i < portCount; ++i) {
		int fd;
		char role;

		fd = SCSIU_OpenPort(paths[i]);
		if (fd < 0) {
			continue;
		}
		role = '\0';
		if (!SCSIU_ProbePortRole(fd, &role)) {
			close(fd);
			continue;
		}
		if (role == 's' && controlFd < 0) {
			controlFd = fd;
			pthread_mutex_lock(&s_scsi_u_bridge.mutex);
			strncpy(s_scsi_u_bridge.control_path, paths[i], sizeof(s_scsi_u_bridge.control_path) - 1);
			s_scsi_u_bridge.control_path[sizeof(s_scsi_u_bridge.control_path) - 1] = '\0';
			pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
			continue;
		}
		if (role == 'S' && interruptFd < 0) {
			interruptFd = fd;
			pthread_mutex_lock(&s_scsi_u_bridge.mutex);
			strncpy(s_scsi_u_bridge.interrupt_path, paths[i], sizeof(s_scsi_u_bridge.interrupt_path) - 1);
			s_scsi_u_bridge.interrupt_path[sizeof(s_scsi_u_bridge.interrupt_path) - 1] = '\0';
			pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
			continue;
		}
		close(fd);
	}

	if (controlFd < 0 || interruptFd < 0) {
		if (controlFd >= 0) close(controlFd);
		if (interruptFd >= 0) close(interruptFd);
		pthread_mutex_lock(&s_scsi_u_bridge.mutex);
		s_scsi_u_bridge.control_path[0] = '\0';
		s_scsi_u_bridge.interrupt_path[0] = '\0';
		SCSIU_SetStatusLocked("Handshake failed");
		pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
		return 0;
	}

	pthread_mutex_lock(&s_scsi_u_bridge.mutex);
	s_scsi_u_bridge.control_fd = controlFd;
	s_scsi_u_bridge.interrupt_fd = interruptFd;
	s_scsi_u_bridge.active = 1;
	s_scsi_u_bridge.connected = 1;
	SCSIU_SetStatusLocked("Connected");
	pthread_mutex_unlock(&s_scsi_u_bridge.mutex);

	if (pthread_create(&s_scsi_u_bridge.interrupt_thread, NULL, SCSIU_InterruptThread, NULL) != 0) {
		SCSIU_StopBridge();
		pthread_mutex_lock(&s_scsi_u_bridge.mutex);
		SCSIU_SetStatusLocked("Interrupt thread start failed");
		pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
		return 0;
	}

	return 1;
#else
	return 0;
#endif
}

void
SCSIU_StopBridge(void)
{
#ifdef __APPLE__
	int controlFd;
	int interruptFd;
	pthread_t thread;
	int shouldJoin;

	pthread_mutex_lock(&s_scsi_u_bridge.mutex);
	controlFd = s_scsi_u_bridge.control_fd;
	interruptFd = s_scsi_u_bridge.interrupt_fd;
	thread = s_scsi_u_bridge.interrupt_thread;
	shouldJoin = (s_scsi_u_bridge.connected && thread != 0);
	s_scsi_u_bridge.active = 0;
	s_scsi_u_bridge.connected = 0;
	s_scsi_u_bridge.control_fd = -1;
	s_scsi_u_bridge.interrupt_fd = -1;
	s_scsi_u_bridge.interrupt_thread = 0;
	s_scsi_u_bridge.interrupt_count = 0;
	s_scsi_u_bridge.control_path[0] = '\0';
	s_scsi_u_bridge.interrupt_path[0] = '\0';
	SCSIU_SetStatusLocked("Disconnected");
	pthread_mutex_unlock(&s_scsi_u_bridge.mutex);

	if (shouldJoin) {
		pthread_join(thread, NULL);
	}
	if (controlFd >= 0) {
		close(controlFd);
	}
	if (interruptFd >= 0) {
		close(interruptFd);
	}
	SCSIU_ResetBootCache();
#endif
}

int
SCSIU_IsConnected(void)
{
#ifdef __APPLE__
	int connected;

	pthread_mutex_lock(&s_scsi_u_bridge.mutex);
	connected = s_scsi_u_bridge.connected;
	pthread_mutex_unlock(&s_scsi_u_bridge.mutex);
	return connected;
#else
	return 0;
#endif
}

const char*
SCSIU_GetStatus(void)
{
#ifdef __APPLE__
	return s_scsi_u_bridge.status;
#else
	return "Unavailable on this platform";
#endif
}

// ---------------------------------------------------------------------------------------
//  SCSI-U SPC (MB89352) レジスタ操作 — USB serial 経由
//  コントロールポートへのプロトコル:
//    読出し: cmd バイトを送信 → 1バイト受信
//    書込み: cmd|0x80 バイトを送信 → val バイトを送信
//    DREG-EX (0x41): 0x41 を送信 → 2バイト length → length バイトのデータ
// ---------------------------------------------------------------------------------------
#ifdef __APPLE__

/* SPC レジスタ読出し。タイムアウト時は 0xFF を返す。 */
static BYTE
SCSIU_ExecSPCReg(BYTE regAddr)
{
	unsigned char buf;
	int i;

	if (s_scsi_u_bridge.control_fd < 0) {
		return 0xFF;
	}
	if (write(s_scsi_u_bridge.control_fd, &regAddr, 1) != 1) {
		return 0xFF;
	}
	/* 200ms ポーリング (20000 × 10μs) */
	for (i = 0; i < 20000; ++i) {
		ssize_t n = read(s_scsi_u_bridge.control_fd, &buf, 1);
		if (n == 1) {
			return buf;
		} else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			return 0xFF;
		}
		usleep(10);
	}
	return 0xFF;
}

/* SPC レジスタ書込み。cmd|0x80, val の 2バイトを送信する。 */
static void
SCSIU_SendSPCReg(BYTE regAddr, BYTE val)
{
	unsigned char cmd[2];

	if (s_scsi_u_bridge.control_fd < 0) {
		return;
	}
	cmd[0] = (unsigned char)(regAddr | 0x80);
	cmd[1] = val;
	(void)write(s_scsi_u_bridge.control_fd, cmd, 2);
}

/*
 * DREG-EX (0x41) による一括データ受信。
 * spc_bridger の m_ExecEx() 相当。
 * 戻り値: 受信バイト数 (≥0)、エラー時 -1。
 */
static int
SCSIU_ExecDregBurst(BYTE* buf, DWORD maxLen)
{
	BYTE cmd = 0x41;
	unsigned char lenBuf[2];
	DWORD dataLen;
	DWORD received;
	int i;

	if (s_scsi_u_bridge.control_fd < 0) {
		return -1;
	}
	tcflush(s_scsi_u_bridge.control_fd, TCIOFLUSH);
	if (write(s_scsi_u_bridge.control_fd, &cmd, 1) != 1) {
		return -1;
	}

	/* 2バイトの length (little-endian) を受信 */
	received = 0;
	for (i = 0; i < 2000 && received < 2; ++i) {
		ssize_t n = read(s_scsi_u_bridge.control_fd, lenBuf + received, 2 - received);
		if (n > 0) {
			received += (DWORD)n;
		} else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			return -1;
		}
		if (received < 2) {
			usleep(1000);
		}
	}
	if (received < 2) {
		return -1; /* タイムアウト */
	}

	dataLen = (DWORD)lenBuf[0] | ((DWORD)lenBuf[1] << 8);
	if (dataLen > maxLen) {
		dataLen = maxLen;
	}

	/* データ本体を受信 */
	received = 0;
	for (i = 0; i < 100000 && received < dataLen; ++i) {
		ssize_t n = read(s_scsi_u_bridge.control_fd, buf + received, dataLen - received);
		if (n > 0) {
			received += (DWORD)n;
		} else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			return -1;
		}
		if (received < dataLen) {
			usleep(100);
		}
	}
	return (int)received;
}

/*
 * INTS レジスタをポーリングして指定ビットが立つまで待つ。
 * timeoutUs: マイクロ秒単位のタイムアウト。
 * 戻り値: 1=成功, 0=タイムアウト。
 */
static int
SCSIU_WaitINTS(BYTE mask, int timeoutUs)
{
	int elapsed = 0;

	while (elapsed < timeoutUs) {
		BYTE ints = SCSIU_ExecSPCReg(0x09/*INTS*/);
		if (ints & mask) {
			return 1;
		}
		usleep(200);
		elapsed += 200;
		/* 1回の ExecSPCReg は USB ラウンドトリップ (~1ms) を含むため
		 * 実際のタイムアウトは timeoutUs より長くなる場合がある */
	}
	return 0;
}

/*
 * SPC (MB89352) で SCSI READ(10) を実行して count ブロックを buf に読み込む。
 * lba: 論理ブロックアドレス, count: ブロック数, blockSize: ブロックサイズ(bytes)
 * 戻り値: 1=成功, 0=失敗
 */
static int
SCSIU_ReadBlocks(DWORD lba, DWORD count, DWORD blockSize, BYTE* buf)
{
	DWORD totalBytes;
	BYTE cdb[10];
	int received;

	if (!s_scsi_u_bridge.connected || s_scsi_u_bridge.control_fd < 0) {
		return 0;
	}
	if (count == 0 || blockSize == 0) {
		return 0;
	}
	totalBytes = count * blockSize;

	/* --- 1. SPC 初期化 --- */
	SCSIU_SendSPCReg(0x03/*SCTL*/, 0x14); /* Arb enable + initiator mode */
	SCSIU_SendSPCReg(0x01/*BDID*/, 0x40); /* Host ID = 6 */

	/* --- 2. ARBITRATION --- */
	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x00);
	SCSIU_SendSPCReg(0x17/*TEMP*/, 0x01); /* Target ID = 0 */
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x04); /* ARBITRATION コマンド */
	/* 仲裁完了: INTS bit4 (RESEL) または短いウェイト */
	usleep(2000);

	/* --- 3. SELECTION --- */
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x28); /* SELECTION + ATN */
	/* COMMAND フェーズ遷移待ち: INTS bit3 (CMD_COMP) */
	if (!SCSIU_WaitINTS(0x08, 2000000)) {
		return 0;
	}

	/* --- 4. COMMAND PHASE: READ(10) CDB 送信 --- */
	cdb[0] = 0x28; /* READ(10) */
	cdb[1] = 0x00;
	cdb[2] = (BYTE)((lba >> 24) & 0xFF);
	cdb[3] = (BYTE)((lba >> 16) & 0xFF);
	cdb[4] = (BYTE)((lba >> 8) & 0xFF);
	cdb[5] = (BYTE)(lba & 0xFF);
	cdb[6] = 0x00;
	cdb[7] = (BYTE)((count >> 8) & 0xFF);
	cdb[8] = (BYTE)(count & 0xFF);
	cdb[9] = 0x00;

	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x02); /* Command phase */
	{
		int k;
		for (k = 0; k < 10; ++k) {
			SCSIU_SendSPCReg(0x15/*DREG*/, cdb[k]);
		}
	}

	/* --- 5. DATA IN フェーズ待ち --- */
	if (!SCSIU_WaitINTS(0x08, 5000000)) {
		return 0;
	}

	/* --- 6. TC 設定 + Initiator Receive --- */
	SCSIU_SendSPCReg(0x19/*TCH*/, (BYTE)((totalBytes >> 16) & 0xFF));
	SCSIU_SendSPCReg(0x1B/*TCM*/, (BYTE)((totalBytes >>  8) & 0xFF));
	SCSIU_SendSPCReg(0x1D/*TCL*/, (BYTE)(totalBytes & 0xFF));
	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x01); /* Data In phase */
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x84); /* Initiator Receive */

	/* --- 7. データ受信 (DREG-EX で一括) --- */
	received = SCSIU_ExecDregBurst(buf, totalBytes);
	if (received < 0 || (DWORD)received < totalBytes) {
		return 0;
	}

	/* --- 8. STATUS + MESSAGE フェーズ (完了処理) --- */
	SCSIU_WaitINTS(0x08, 2000000);
	SCSIU_ExecSPCReg(0x15/*DREG*/); /* status byte */
	SCSIU_WaitINTS(0x08, 1000000);
	SCSIU_ExecSPCReg(0x15/*DREG*/); /* message byte */
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x28); /* Message Accept */

	return 1;
}

/*
 * SPC (MB89352) で SCSI WRITE(10) を実行して count ブロックを buf から書き込む。
 * 戻り値: 1=成功, 0=失敗
 */
static int
SCSIU_WriteBlocks(DWORD lba, DWORD count, DWORD blockSize, const BYTE* buf)
{
	DWORD totalBytes;
	BYTE cdb[10];

	if (!s_scsi_u_bridge.connected || s_scsi_u_bridge.control_fd < 0) {
		return 0;
	}
	if (count == 0 || blockSize == 0) {
		return 0;
	}
	totalBytes = count * blockSize;

	/* --- 1. SPC 初期化 --- */
	SCSIU_SendSPCReg(0x03/*SCTL*/, 0x14);
	SCSIU_SendSPCReg(0x01/*BDID*/, 0x40);

	/* --- 2. ARBITRATION --- */
	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x00);
	SCSIU_SendSPCReg(0x17/*TEMP*/, 0x01);
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x04);
	usleep(2000);

	/* --- 3. SELECTION --- */
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x28);
	if (!SCSIU_WaitINTS(0x08, 2000000)) {
		return 0;
	}

	/* --- 4. COMMAND PHASE: WRITE(10) CDB 送信 --- */
	cdb[0] = 0x2A; /* WRITE(10) */
	cdb[1] = 0x00;
	cdb[2] = (BYTE)((lba >> 24) & 0xFF);
	cdb[3] = (BYTE)((lba >> 16) & 0xFF);
	cdb[4] = (BYTE)((lba >> 8) & 0xFF);
	cdb[5] = (BYTE)(lba & 0xFF);
	cdb[6] = 0x00;
	cdb[7] = (BYTE)((count >> 8) & 0xFF);
	cdb[8] = (BYTE)(count & 0xFF);
	cdb[9] = 0x00;

	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x02);
	{
		int k;
		for (k = 0; k < 10; ++k) {
			SCSIU_SendSPCReg(0x15/*DREG*/, cdb[k]);
		}
	}

	/* --- 5. DATA OUT フェーズ待ち --- */
	if (!SCSIU_WaitINTS(0x08, 5000000)) {
		return 0;
	}

	/* --- 6. TC 設定 + Initiator Send --- */
	SCSIU_SendSPCReg(0x19/*TCH*/, (BYTE)((totalBytes >> 16) & 0xFF));
	SCSIU_SendSPCReg(0x1B/*TCM*/, (BYTE)((totalBytes >>  8) & 0xFF));
	SCSIU_SendSPCReg(0x1D/*TCL*/, (BYTE)(totalBytes & 0xFF));
	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x00); /* Data Out phase */
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0xC4); /* Initiator Send */

	/* --- 7. データ送信 (1バイトずつ DREG に書く) --- */
	{
		DWORD k;
		for (k = 0; k < totalBytes; ++k) {
			SCSIU_SendSPCReg(0x15/*DREG*/, buf[k]);
		}
	}

	/* --- 8. STATUS + MESSAGE フェーズ --- */
	SCSIU_WaitINTS(0x08, 2000000);
	SCSIU_ExecSPCReg(0x15/*DREG*/); /* status byte */
	SCSIU_WaitINTS(0x08, 1000000);
	SCSIU_ExecSPCReg(0x15/*DREG*/); /* message byte */
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x28); /* Message Accept */

	return 1;
}

/* ---------------------------------------------------------------------------------------
 * SCSI-U ブートキャッシュ
 * 物理 HDD の先頭 SCSIU_BOOT_CACHE_BLOCKS ブロックをキャッシュする。
 * これにより既存の SCSI_FindPartitionBootOffset / SCSI_ReadBPBFromImage 等を
 * image buffer モードと共通のコードで動かせる。
 * --------------------------------------------------------------------------------------- */
#define SCSIU_BOOT_CACHE_BLOCKS 8192  /* 8192 × 512B = 4MB */
#define SCSIU_READ_CHUNK_BLOCKS  128  /* max blocks per DREG-EX burst (~64KB) */
static BYTE*  s_scsiu_boot_cache      = NULL;
static long   s_scsiu_boot_cache_size = 0;

static void
SCSIU_ResetBootCache(void)
{
	if (s_scsiu_boot_cache != NULL) {
		free(s_scsiu_boot_cache);
		s_scsiu_boot_cache = NULL;
	}
	s_scsiu_boot_cache_size = 0;
}

static void
SCSIU_InvalidateBootCacheRange(DWORD lba, DWORD count, DWORD blockSize)
{
	unsigned long long writeStart;
	unsigned long long writeBytes;
	unsigned long long cacheBytes;

	if (s_scsiu_boot_cache == NULL || count == 0 || blockSize == 0) {
		return;
	}

	writeStart = (unsigned long long)lba * (unsigned long long)blockSize;
	writeBytes = (unsigned long long)count * (unsigned long long)blockSize;
	cacheBytes = (unsigned long long)s_scsiu_boot_cache_size;

	if (writeStart < cacheBytes && writeBytes > 0) {
		char cacheLog[128];
		snprintf(cacheLog, sizeof(cacheLog),
		         "SCSIU_CACHE: invalidated by write lba=%u blocks=%u blockSize=%u",
		         (unsigned int)lba,
		         (unsigned int)count,
		         (unsigned int)blockSize);
		SCSI_LogText(cacheLog);
		SCSIU_ResetBootCache();
	}
}

/*
 * ブートキャッシュを物理 HDD から読み込む。
 * DREG-EX 1 回あたり最大 SCSIU_READ_CHUNK_BLOCKS ブロック (≈64KB) に分けて転送する。
 * すでにロード済みなら即 1 を返す。
 * 戻り値: 1=成功/既ロード, 0=失敗
 */
static int
SCSIU_EnsureBootCache(void)
{
	DWORD blockSize  = 512;
	DWORD totalBlocks = SCSIU_BOOT_CACHE_BLOCKS;
	DWORD totalBytes  = totalBlocks * blockSize;
	DWORD chunksRead  = 0;
	char cacheLog[128];

	if (s_scsiu_boot_cache != NULL) {
		return 1; /* 既にロード済み */
	}
	if (!s_scsi_u_bridge.connected) {
		return 0;
	}
	s_scsiu_boot_cache = (BYTE*)malloc(totalBytes);
	if (s_scsiu_boot_cache == NULL) {
		SCSI_LogText("SCSIU_CACHE: malloc failed");
		return 0;
	}

	while (chunksRead < totalBlocks) {
		DWORD toRead = totalBlocks - chunksRead;
		if (toRead > SCSIU_READ_CHUNK_BLOCKS)
			toRead = SCSIU_READ_CHUNK_BLOCKS;
		if (!SCSIU_ReadBlocks(chunksRead, toRead, blockSize,
		                      s_scsiu_boot_cache + chunksRead * blockSize)) {
			free(s_scsiu_boot_cache);
			s_scsiu_boot_cache = NULL;
			snprintf(cacheLog, sizeof(cacheLog),
			         "SCSIU_CACHE: load failed at block %u", (unsigned int)chunksRead);
			SCSI_LogText(cacheLog);
			return 0;
		}
		chunksRead += toRead;
	}

	s_scsiu_boot_cache_size = (long)totalBytes;
	snprintf(cacheLog, sizeof(cacheLog),
	         "SCSIU_CACHE: boot cache loaded (%uMB, %u blocks)",
	         (unsigned int)(totalBytes >> 20), (unsigned int)totalBlocks);
	SCSI_LogText(cacheLog);
	return 1;
}

/*
 * SPC READ CAPACITY(10) を実行して実際のディスク容量を取得する。
 * outLastLBA: 最後の LBA (ブロック数 - 1)
 * outBlockSize: バイト/ブロック
 * 戻り値: 1=成功, 0=失敗
 */
static int
SCSIU_ReadCapacity(DWORD* outLastLBA, DWORD* outBlockSize)
{
	BYTE cdb[10];
	BYTE resp[8];
	int received;

	if (!s_scsi_u_bridge.connected || s_scsi_u_bridge.control_fd < 0) {
		return 0;
	}

	/* SPC 初期化 */
	SCSIU_SendSPCReg(0x03/*SCTL*/, 0x14);
	SCSIU_SendSPCReg(0x01/*BDID*/, 0x40);

	/* ARBITRATION + SELECTION */
	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x00);
	SCSIU_SendSPCReg(0x17/*TEMP*/, 0x01);
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x04);
	usleep(2000);
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x28);
	if (!SCSIU_WaitINTS(0x08, 2000000)) {
		return 0;
	}

	/* READ CAPACITY(10) CDB */
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x25; /* READ CAPACITY(10) */
	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x02);
	{
		int k;
		for (k = 0; k < 10; ++k) {
			SCSIU_SendSPCReg(0x15/*DREG*/, cdb[k]);
		}
	}

	/* DATA IN フェーズ待ち */
	if (!SCSIU_WaitINTS(0x08, 5000000)) {
		return 0;
	}

	/* 8バイト受信 */
	SCSIU_SendSPCReg(0x19/*TCH*/, 0x00);
	SCSIU_SendSPCReg(0x1B/*TCM*/, 0x00);
	SCSIU_SendSPCReg(0x1D/*TCL*/, 0x08);
	SCSIU_SendSPCReg(0x11/*PCTL*/, 0x01);
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x84);

	received = SCSIU_ExecDregBurst(resp, 8);
	if (received < 8) {
		return 0;
	}

	/* STATUS + MESSAGE */
	SCSIU_WaitINTS(0x08, 2000000);
	SCSIU_ExecSPCReg(0x15/*DREG*/);
	SCSIU_WaitINTS(0x08, 1000000);
	SCSIU_ExecSPCReg(0x15/*DREG*/);
	SCSIU_SendSPCReg(0x05/*SCMD*/, 0x28);

	*outLastLBA   = ((DWORD)resp[0] << 24) | ((DWORD)resp[1] << 16) |
	                ((DWORD)resp[2] <<  8) |  (DWORD)resp[3];
	*outBlockSize = ((DWORD)resp[4] << 24) | ((DWORD)resp[5] << 16) |
	                ((DWORD)resp[6] <<  8) |  (DWORD)resp[7];
	return 1;
}

#endif /* __APPLE__ */

/* ---------------------------------------------------------------------------------------
 * バスモードに応じたイメージバッファポインタ/サイズ取得ヘルパー。
 * SCSI-U モードではブートキャッシュを返す。
 * --------------------------------------------------------------------------------------- */
static BYTE*
SCSI_ImgBuf(void)
{
#ifdef __APPLE__
	if (X68000_GetStorageBusMode() == 2) {
		return s_scsiu_boot_cache;
	}
#endif
	return s_disk_image_buffer[4];
}

static long
SCSI_ImgSize(void)
{
#ifdef __APPLE__
	if (X68000_GetStorageBusMode() == 2) {
		return s_scsiu_boot_cache_size;
	}
#endif
	return s_disk_image_buffer_size[4];
}

// ---------------------------------------------------------------------------------------
//  合成SCSI ROM (外付けSCSI CZ-6BS1互換: $EA0000-$EA1FFF)
//  - $EA0024: "SCSIEX" シグネチャ
//  - $EA0068: "Human68k" シグネチャ
//  - ブート/IOCSは $E9F800 トラップで C 側へ橋渡し
// ---------------------------------------------------------------------------------------
#define BE32(v) \
	(BYTE)(((v) >> 24) & 0xff), (BYTE)(((v) >> 16) & 0xff), \
	(BYTE)(((v) >> 8) & 0xff), (BYTE)((v) & 0xff)

static BYTE SCSIIMG[] = {
	// $EA0000-$EA001F: エントリテーブル（実機互換で init へ向ける）
	BE32(SCSI_SYNTH_INIT_ENTRY), BE32(SCSI_SYNTH_INIT_ENTRY),
	BE32(SCSI_SYNTH_INIT_ENTRY), BE32(SCSI_SYNTH_INIT_ENTRY),
	BE32(SCSI_SYNTH_INIT_ENTRY), BE32(SCSI_SYNTH_INIT_ENTRY),
	BE32(SCSI_SYNTH_INIT_ENTRY), BE32(SCSI_SYNTH_INIT_ENTRY),

	// $EA0020
	BE32(SCSI_SYNTH_BOOT_ENTRY),

	// $EA0024: "SCSIEX"（外付けSCSI ROM互換）
	'S', 'C', 'S', 'I', 'E', 'X',

	// $EA002A: ボード属性 / $EA002C: トラップI/Oアドレス
	0x00, 0x03,
	0x00, 0xe9, 0xf8, 0x00,

	// $EA0030: ブートエントリ
	0x13, 0xfc, 0x00, 0xff, 0x00, 0xe9, 0xf8, 0x00, // move.b #$ff,$e9f800
	0x4a, 0x80,                                     // tst.l d0
	0x66, 0x06,                                     // bne.s fail
	0x4e, 0xb9, 0x00, 0x00, 0x20, 0x00,             // jsr $00002000
	0x70, 0xff,                                     // fail: moveq #-1,d0
	0x4e, 0x75,                                     // rts

	// $EA0046-$EA0057: reserved (18 bytes)
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,

	// $EA0058: "SCSI" / $EA005C: init / $EA0060: iocs
	// $EA0068: "Human68k"（実機互換位置）
	'S', 'C', 'S', 'I',
	BE32(SCSI_SYNTH_INIT_ENTRY),
	BE32(SCSI_SYNTH_IOCS_ENTRY),
	0x00, 0x00, 0x00, 0x00,
	'H', 'u', 'm', 'a', 'n', '6', '8', 'k',

	// $EA0070: IOCS初期化 (ID0 のみ存在を返す)
	0x23, 0xfc, 0x00, 0xea, 0x00, 0x80, 0x00, 0x00, 0x07, 0xd4, // move.l #$ea0080,$7d4.l
	0x70, 0x00,                                                   // moveq #0,d0
	0x74, 0x01,                                                   // moveq #1,d2
	0x4e, 0x75,                                                   // rts

			// $EA0080: SCSI IOCSハンドラ (trap #15経由用: RTSで復帰)
			0x13, 0xc1, 0x00, 0xe9, 0xf8, 0x00, // move.b d1,$e9f800
			0x4e, 0x75,                         // rts

			// $EA0088: trap #15 ディスパッチャ (58 bytes)
			// d0.b のファンクション番号で $400+(d0&0xFF)*4 のハンドラを JSR→RTE
			// d0/a0 を保存復帰して、呼び出し元レジスタ破壊を防ぐ
			// IOCSテーブルの bit0 メタデータを許容するため、呼び出し先は偶数化する。
			// さらに $00008000 未満の低位RAMアドレスは不正ハンドラとして拒否する。
			0x2F, 0x00,                         // move.l d0,-(sp)
			0x2F, 0x08,                         // move.l a0,-(sp)
			0x02, 0x40, 0x00, 0xFF,             // andi.w #$00FF,d0
			0xE5, 0x48,                         // lsl.w #2,d0
			0x41, 0xF8, 0x04, 0x00,             // lea $0400.w,a0
			0x20, 0x70, 0x00, 0x00,             // movea.l (a0,d0.w),a0
			0x20, 0x08,                         // move.l a0,d0
			0x67, 0x1C,                         // beq.s .unimpl
			0x02, 0x80, 0x00, 0xFF, 0xFF, 0xFE, // andi.l #$00FFFFFE,d0
			0x0C, 0x80, 0x00, 0x00, 0x80, 0x00, // cmpi.l #$00008000,d0
			0x65, 0x0E,                         // blo.s .unimpl
			0x20, 0x40,                         // movea.l d0,a0
			0x20, 0x2F, 0x00, 0x04,             // move.l 4(sp),d0
			0x4E, 0x90,                         // jsr (a0)
			0x20, 0x5F,                         // movea.l (sp)+,a0
			0x58, 0x8F,                         // addq.l #4,sp
			0x4E, 0x73,                         // rte
			0x20, 0x5F,                         // .unimpl: movea.l (sp)+,a0
			0x58, 0x8F,                         // addq.l #4,sp
			0x70, 0xFF,                         // moveq #-1,d0
			0x4E, 0x73,                         // rte

			// $EA00C2: (unused, 8 bytes)
			// Previously used for display IOCS wrapper; now dead code.
			0x13, 0xC0, 0x00, 0xE9, 0xF8, 0x04, // move.b d0,$E9F804
			0x4E, 0x75,                           // rts
			// $EA00CA-$EA00D1: パディング (8 bytes)
			0,0,0,0,0,0,0,0,

			// $EA00D2: デフォルト IOCS ハンドラ (4 bytes)
			// 未実装 IOCS を安全に失敗させる（JSR 経由で RTS で返る）
			0x70, 0xff,                         // moveq #-1,d0
			0x4e, 0x75,                         // rts

			// $EA00D6: fn=$04 互換スタブ (4 bytes)
			// ブートセクタが期待する初期IOCS呼び出しを成功扱いで通す
			0x70, 0x00,                         // moveq #0,d0
			0x4e, 0x75,                         // rts

					// $EA00DA: fn=$34 互換スタブ (4 bytes)
					// 送信可として ready(1) を返す
						0x70, 0x01,                         // moveq #1,d0
						0x4e, 0x75,                         // rts

					// $EA00DE: fn=$35 互換スタブ (4 bytes)
					// 送信対象の文字(d1.w)を返す。0固定だと一部IPLが
					// 「入力なし」と誤判定してコンソールループに滞留する。
						0x30, 0x01,                         // move.w d1,d0
						0x4e, 0x75,                         // rts

				// $EA00E2: fn=$33 互換スタブ (4 bytes)
				// _B_KEYSNS 相当: キーあり(1)として返す
					0x70, 0x01,                         // moveq #1,d0
					0x4e, 0x75,                         // rts

				// $EA00E6: fn=$32 互換スタブ (4 bytes)
				// _B_KEYINP 相当: Enter(0x0D) を返して先へ進める
					0x70, 0x0D,                         // moveq #$0D,d0
					0x4e, 0x75,                         // rts

			// $EA00EA: fn=$10 互換スタブ (4 bytes)
			// カーネル初期化が期待する成功返却を返す
			0x70, 0x00,                         // moveq #0,d0
			0x4e, 0x75,                         // rts

			// $EA00EE: fn=$AF 互換スタブ (4 bytes)
			// 初期化時の拡張IOCS呼び出しを成功扱いで通す
			0x70, 0x00,                         // moveq #0,d0
			0x4e, 0x75,                         // rts

			// $EA00F2: 直接IOCSラッパー (8 bytes, trap #15経由で RTS)
			// d0.b をそのまま $E9F800 に出して C 側 SCSI_HandleIOCS へ渡す
			0x13, 0xC0, 0x00, 0xE9, 0xF8, 0x00, // move.b d0,$E9F800
			0x4E, 0x75,                         // rts

		// $EA00FA: fn=$00 互換スタブ (4 bytes)
		// A/R/I 分岐待ちで即 Abort させないため 0 を返す。
		// 実機では fn=$00 はブロッキング待ち。0 = キー無しで
		// 呼び出し側が再ポーリングする。
		0x70, 0x00,                         // moveq #0,d0
		0x4E, 0x75,                         // rts

		// $EA00FE-$EA00FF: パディング (2 bytes, 総サイズ不変)
		0,0,                                // $FE-$FF

	// ---------------------------------------------------------------
	// $EA0100: Human68k ブロックデバイスドライバヘッダ (22バイト)
	// ---------------------------------------------------------------
	// +$00: next device pointer (チェイン終端、リンク時にRAM側を書換)
	0xff, 0xff, 0xff, 0xff,
		// +$04: attribute ($0000 = block device)
		// NOTE: $2000 is a Human68k "remote device" attribute and makes the
		// kernel issue CR_* extended requests (0x40-0x58) instead of block I/O
		// requests. Our synthetic driver implements block-device packets, so the
		// attribute must remain clear.
		0x00, 0x00,
	// +$06: strategy entry = $EA0116
	BE32(0x00ea0116),
	// +$0A: interrupt entry = $EA0120
	BE32(0x00ea0120),
	// +$0E: units(1) + name "SCSI   "
	0x01, 'S', 'C', 'S', 'I', ' ', ' ', ' ',

	// ---------------------------------------------------------------
	// $EA0116: strategy ルーチン (10バイト)
	//   A5 = リクエストパケット → C側で退避
	// ---------------------------------------------------------------
	0x13, 0xfc, 0x00, 0x02, 0x00, 0xe9, 0xf8, 0x02, // move.b #$02,$E9F802
	0x4e, 0x75,                                       // rts

	// ---------------------------------------------------------------
	// $EA0120: interrupt ルーチン (10バイト)
	//   C側でコマンド処理
	// ---------------------------------------------------------------
	0x13, 0xfc, 0x00, 0x01, 0x00, 0xe9, 0xf8, 0x02, // move.b #$01,$E9F802
	0x4e, 0x75,                                       // rts
};

#undef BE32


// -----------------------------------------------------------------------
//   初期化
// -----------------------------------------------------------------------
void SCSI_Init(void)
{
	int i;
	BYTE tmp;
	ZeroMemory(SCSIIPL, 0x2000);
	if (SCSI_HasExternalROM()) {
		p6logd("SCSI_Init: SCSIEXROM.DAT detected, will activate after kernel boot\n");
	}
	memcpy(SCSIIPL, SCSIIMG, sizeof(SCSIIMG));
	for (i=0; i<0x2000; i+=2)
	{
		tmp = SCSIIPL[i];
		SCSIIPL[i] = SCSIIPL[i+1];
		SCSIIPL[i+1] = tmp;
	}
	ZeroMemory(s_spc_regs, sizeof(s_spc_regs));
	// Real MB89352 powers up with SCTL=$00 (RD cleared).  Drivers detect the
	// chip via write-readback on SCTL/BDID, not by checking a specific initial
	// value.  Previously bit7 was seeded here ($80) which made SCTL appear to
	// be in permanent reset, trapping drivers in a poll loop.
	s_spc_regs[0x03] = 0x00;
	// SSTS power-on default: TC0 | DREG_EMPTY (no transfer in progress)
	s_spc_regs[0x0d] = 0x05;
	s_scsi_dev_reqpkt = 0;
	s_scsi_dev_linked = 0;
	s_scsi_boot_done = 0;
	s_scsi_devhdr_next[0] = 0xFF;
	s_scsi_devhdr_next[1] = 0xFF;
	s_scsi_devhdr_next[2] = 0xFF;
	s_scsi_devhdr_next[3] = 0xFF;
	s_scsi_partition_byte_offset = 0;
	s_scsi_sector_size = 1024;
	s_scsi_bpb_ram_addr = SCSI_BPB_RAM_ADDR;
	s_scsi_bpbptr_ram_addr = SCSI_BPBPTR_RAM_ADDR;
	s_scsi_drvchk_state = 0x00;
	// Low-level SCSI IOCS state
	memset(s_spc_cdb, 0, sizeof(s_spc_cdb));
	s_spc_cdb_len = 0;
	s_spc_target_id = 0;
	s_spc_status_byte = 0;
	s_spc_message_byte = 0;
	s_spc_cmd_valid = 0;
	s_scsi_root_dir_start_sector = 0;
	s_scsi_root_dir_sector_count = 0;
	s_scsi_dev_absolute_sectors = -1;
	s_scsi_fat_start_sector = 0;
	s_scsi_fat_sectors = 0;
	s_scsi_fat_count = 0;
	s_scsi_total_sectors = 0;
	s_scsi_need_fat16to12 = 0;
	s_scsi_fat16_fixed = 0;
	s_scsi_config_sector = 0;
	s_scsi_config_size = 0;
	s_scsi_config_read_count = 0;
	s_scsi_config_boot_phase = 1;
	s_scsi_media_check_count = 0;
	s_scsi_data_start_sector = 0;
	s_scsi_sec_per_clus = 1;
	s_scsi_dpb_addr = 0;
	s_scsi_boot_activity = 0;
	s_scsi_driver_activity = 0;
	s_scsi_deferred_boot_pending = 0;
	s_scsi_deferred_boot_addr = 0;
	s_scsi_deferred_d5 = 0;
	SCSIU_ResetBootCache();
	SCSI_LogInit();
	s_spc_access_log_count = 0;
	SCSI_InvalidateTransferCache();
	s_last_iocs_sig_valid = 0;
	s_scsi_log_total = 0;
	{
		char buildTag[96];
		snprintf(buildTag, sizeof(buildTag),
			         "SCSI_BUILD devdrv-partoff-v14 %s %s",
		         __DATE__, __TIME__);
		SCSI_LogText(buildTag);
	}
}


// -----------------------------------------------------------------------
//   撤収〜
// -----------------------------------------------------------------------
void SCSI_Cleanup(void)
{
	SCSIU_ResetBootCache();
}

void SCSI_InvalidateTransferCache(void)
{
	s_iocs_data_offset_cache_valid = 0;
	s_iocs_data_offset_cache = 0;
}


// -----------------------------------------------------------------------
//   ログ関連
//   All helpers below are only referenced from the gated log path; when
//   runtime logging is disabled they're defined but unused, so mark them
//   __attribute__((unused)) to silence -Wunused-function.
// -----------------------------------------------------------------------
#if MPX68K_ENABLE_RUNTIME_FILE_LOGS
#define SCSI_LOGFN_ATTR
#else
#define SCSI_LOGFN_ATTR __attribute__((unused))
#endif

static SCSI_LOGFN_ATTR void SCSI_GetLogPath(char* outPath, size_t outSize)
{
#ifdef __APPLE__
	const char* home = getenv("HOME");
	if (home && home[0] != '\0') {
		snprintf(outPath, outSize, "%s/Documents/X68000/_scsi_iocs.txt", home);
		return;
	}
#endif
	snprintf(outPath, outSize, "X68000/_scsi_iocs.txt");
}

static SCSI_LOGFN_ATTR FILE* SCSI_OpenMirrorLog(const char* mode)
{
#ifdef __APPLE__
	return fopen("/tmp/x68000_scsi_iocs.txt", mode);
#else
	(void)mode;
	return NULL;
#endif
}

static SCSI_LOGFN_ATTR void SCSI_EnsureLogDir(void)
{
#ifdef __APPLE__
	const char* home = getenv("HOME");
	if (home && home[0] != '\0') {
		char dirPath[512];
		snprintf(dirPath, sizeof(dirPath), "%s/Documents/X68000", home);
		mkdir(dirPath, 0755);
	}
#endif
}

int SCSI_IsROMPresent(void)
{
	return SCSI_HasExternalROM();
}

static int SCSI_HasExternalROM(void)
{
	size_t i;
	for (i = 0; i < sizeof(SCSIROM_DAT); i++) {
		if (SCSIROM_DAT[i] != 0) {
			return 1;
		}
	}
	return 0;
}

static void SCSI_LogInit(void)
{
#if !MPX68K_ENABLE_RUNTIME_FILE_LOGS
	return;
#else
	char logPath[512];
	FILE* mirror;
	SCSI_GetLogPath(logPath, sizeof(logPath));
	SCSI_EnsureLogDir();
	FILE *fp = fopen(logPath, "w");
	if (fp) {
		fprintf(fp, "--- SCSI log start ---\n");
		fclose(fp);
	}
	mirror = SCSI_OpenMirrorLog("w");
	if (mirror) {
		fprintf(mirror, "--- SCSI log start ---\n");
		fclose(mirror);
	}
#endif /* MPX68K_ENABLE_RUNTIME_FILE_LOGS */
}

static void SCSI_LogIO(DWORD adr, const char* op, BYTE data)
{
#if !MPX68K_ENABLE_RUNTIME_FILE_LOGS
	(void)adr; (void)op; (void)data;
	return;
#else
	char logPath[512];
	FILE* mirror;
	if (s_scsi_log_total >= SCSI_LOG_LIMIT) return;
	s_scsi_log_total++;
	SCSI_GetLogPath(logPath, sizeof(logPath));
	SCSI_EnsureLogDir();
	FILE *fp = fopen(logPath, "a");
	if (fp) {
		if (op[0] == 'R') {
			fprintf(fp, "SCSI %s @ 0x%06X\n", op, (unsigned int)adr);
		} else {
			fprintf(fp, "SCSI %s @ 0x%06X = 0x%02X\n", op, (unsigned int)adr, data);
		}
		fclose(fp);
	}
	mirror = SCSI_OpenMirrorLog("a");
	if (mirror) {
		if (op[0] == 'R') {
			fprintf(mirror, "SCSI %s @ 0x%06X\n", op, (unsigned int)adr);
		} else {
			fprintf(mirror, "SCSI %s @ 0x%06X = 0x%02X\n", op, (unsigned int)adr, data);
		}
		fclose(mirror);
	}
#endif /* MPX68K_ENABLE_RUNTIME_FILE_LOGS */
}

void SCSI_LogText(const char* text)
{
#if !MPX68K_ENABLE_RUNTIME_FILE_LOGS
	(void)text;
	return;
#else
	char logPath[512];
	FILE* mirror;
	if (s_scsi_log_total >= SCSI_LOG_LIMIT) return;
	s_scsi_log_total++;
	SCSI_GetLogPath(logPath, sizeof(logPath));
	SCSI_EnsureLogDir();
	FILE *fp = fopen(logPath, "a");
	if (fp) {
		fprintf(fp, "%s\n", text);
		fclose(fp);
	}
	mirror = SCSI_OpenMirrorLog("a");
	if (mirror) {
		fprintf(mirror, "%s\n", text);
		fclose(mirror);
	}
#endif /* MPX68K_ENABLE_RUNTIME_FILE_LOGS */
}

static void SCSI_LogSPCAccess(const char* op, DWORD adr, BYTE data)
{
#if !MPX68K_ENABLE_RUNTIME_FILE_LOGS
	(void)op; (void)adr; (void)data;
	return;
#else
	char line[96];
	if (s_spc_access_log_count >= 256) {
		return;
	}
	snprintf(line, sizeof(line), "SPC_%s adr=$%06X data=$%02X",
	         op, (unsigned int)(adr & 0x00ffffff), (unsigned int)data);
	SCSI_LogText(line);
	s_spc_access_log_count++;
#endif /* MPX68K_ENABLE_RUNTIME_FILE_LOGS */
}

static DWORD SCSI_GetPayloadOffset(void)
{
	BYTE* buf;
	long size;

	if (SCSI_ImgBuf() == NULL || SCSI_ImgSize() < 8) {
		return 0;
	}

	buf = SCSI_ImgBuf();
	size = SCSI_ImgSize();

	// Standard container header.
	if (memcmp(buf, "X68SCSI1", 8) == 0) {
		return 0x400;
	}
	// Some SxSI-formatted images store 0x00 at byte 0, followed by "68SCSI1".
	if (memcmp(buf + 1, "68SCSI1", 7) == 0) {
		return 0x400;
	}
	// Metadata strings used by common container variants.
	if (size >= 0x20 && memcmp(buf + 0x10, "Human68K SCSI", 12) == 0) {
		return 0x400;
	}
	if (size >= 0x20 && memcmp(buf + 0x10, "This SCSI-UNIT", 14) == 0) {
		return 0x400;
	}
	return 0;
}

static DWORD SCSI_GetBlockSizeFromCode(DWORD sizeCode)
{
	DWORD code16 = sizeCode & 0xffff;

	// Legacy SCSI IPLs sometimes pass 0x8000 for 512-byte blocks.
	if (code16 == 0x8000) {
		return 512;
	}

	switch (sizeCode & 0xff) {
	case 0:
		return 256;
	case 1:
		return 512;
	case 2:
		return 1024;
	default:
		return SCSI_GetImageBlockSize();
	}
}

static int SCSI_HasHumanPartitionTable(BYTE* buf, long size, DWORD physSectorSize)
{
	DWORD partTableOffset;
	DWORD i;

	if (buf == NULL || size <= 0) {
		return 0;
	}
	if (physSectorSize != 256 && physSectorSize != 512 && physSectorSize != 1024) {
		return 0;
	}
	partTableOffset = physSectorSize * 4U;
	if (partTableOffset + physSectorSize > (DWORD)size || partTableOffset + 16U > (DWORD)size) {
		return 0;
	}
	if (memcmp(buf + partTableOffset, "X68K", 4) != 0) {
		return 0;
	}
	for (i = partTableOffset + 4U;
	     i + 16U <= partTableOffset + physSectorSize && i + 16U <= (DWORD)size;
	     i += 2U) {
		if (buf[i] == 'H' && memcmp(buf + i, "Human68k", 8) == 0) {
			DWORD startSec = ((DWORD)buf[i + 8] << 24) |
			                 ((DWORD)buf[i + 9] << 16) |
			                 ((DWORD)buf[i + 10] << 8) |
			                 ((DWORD)buf[i + 11]);
			unsigned long long bootOffset = (unsigned long long)startSec * 1024ULL;
			if (startSec != 0U && bootOffset + 16ULL <= (unsigned long long)size) {
				return 1;
			}
		}
	}
	return 0;
}

static DWORD SCSI_GetImageBlockSize(void)
{
	BYTE* buf;
	DWORD headerBlockSize;
	long size;
	DWORD cand512;
	DWORD cand1024;
	int hasPart512;
	int hasPart1024;

	if (SCSI_ImgBuf() == NULL || SCSI_ImgSize() < 16) {
		return 512;
	}
	buf = SCSI_ImgBuf();
	size = SCSI_ImgSize();

	if (memcmp(buf, "X68SCSI1", 8) == 0 || memcmp(buf + 1, "68SCSI1", 7) == 0) {
		headerBlockSize = ((DWORD)buf[8] << 8) | (DWORD)buf[9];
		if (headerBlockSize == 256 || headerBlockSize == 512 || headerBlockSize == 1024) {
			return headerBlockSize;
		}
	}

	// Prefer a physically consistent partition-table decode over raw signature
	// hits. Some 1024-byte images can contain a coincidental "X68K" at 0x800.
	hasPart1024 = SCSI_HasHumanPartitionTable(buf, size, 1024);
	hasPart512 = SCSI_HasHumanPartitionTable(buf, size, 512);
	if (hasPart1024 && !hasPart512) {
		return 1024;
	}
	if (hasPart512 && !hasPart1024) {
		return 512;
	}
	if (hasPart1024 && hasPart512) {
		return 1024;
	}

	// Headerless raw images are typically 512B or 1024B sectors.
	// Prefer a signature-based guess from the partition table at LBA4.
	cand1024 = 4U * 1024U;
	if ((long)(cand1024 + 4) <= size && memcmp(buf + cand1024, "X68K", 4) == 0) {
		return 1024;
	}
	cand512 = 4U * 512U;
	if ((long)(cand512 + 4) <= size && memcmp(buf + cand512, "X68K", 4) == 0) {
		return 512;
	}

	return 512;
}

static DWORD SCSI_GetIocsDataOffset(void)
{
	// X68SCSI1/SxSI container headers occupy the first 2 sectors (0x400 bytes)
	// of the virtual SCSI disk.  The X68000 OS addresses the entire image
	// including those header sectors via IOCS LBAs (e.g. boot sector = LBA 2,
	// partition table = LBA 4 for 512-byte sectors).  Therefore IOCS LBA
	// resolution must always start from file offset 0, NOT from the payload.
	// SCSI_GetPayloadOffset() is used only by SCSI_HandleBoot to locate
	// the boot sector within the image.
	return 0;
}

static DWORD SCSI_GetTransferLBA(BYTE cmd, DWORD d2, DWORD d4)
{
	DWORD lba24 = d2 & 0x00ffffffU;
	DWORD fallbackLba;
	DWORD payloadOffset;
	DWORD imageBlockSize;
	DWORD payloadBlocks;
	// _S_READI ($2E) seen during boot can pass sign-extended 16-bit LBAs
	// (e.g. d2=$FFFF001E / $FFFFFFFF). Treat this form as 16-bit LBA.
	if (cmd == 0x2e && (d2 & 0xffff0000U) == 0xffff0000U) {
		return d2 & 0x0000ffffU;
	}
	// Human68k SCSI IOCS uses d2.l as logical block address for READ/WRITE
	// and their extended variants. d4 carries the target ID byte and can appear
	// non-zero even when LBA is 0, so using d4 as an LBA fallback causes
	// mis-addressed reads on LBA 0.
	// Some IOCS paths encode SCSI ID in the upper byte of d2 (e.g. $20xxxxxx).
	// Disk images are capped well below 24-bit LBA in this emulator, so strip
	// the upper control byte unconditionally.
	//
	// However, some _S_READEXT/_S_WRITEEXT boot paths transiently pass d2=0
	// while placing a usable coarse LBA in d4's upper byte. On SxSI container
	// images this coarse value includes the 2-sector container header, so adjust
	// by payload base blocks before issuing the read.
	if ((cmd == 0x26 || cmd == 0x27) &&
	    lba24 == 0 &&
	    (d4 & 0x0000ffffU) == 0x0000ffffU &&
	    ((d4 >> 24) & 0xffU) >= 2U) {
		fallbackLba = (d4 >> 24) & 0xffU;
		payloadOffset = SCSI_GetPayloadOffset();
		imageBlockSize = SCSI_GetImageBlockSize();
		if (payloadOffset != 0 && imageBlockSize != 0) {
			payloadBlocks = payloadOffset / imageBlockSize;
			if (payloadBlocks > 0 && fallbackLba >= payloadBlocks) {
				fallbackLba -= payloadBlocks;
			}
		}
		return fallbackLba;
	}
	return lba24;
}

static int SCSI_ResolveTransfer(DWORD lba, DWORD blocks, DWORD blockSize,
                                DWORD* imageOffset, DWORD* transferBytes)
{
	DWORD dataOffset;
	unsigned long long dataSize;
	unsigned long long start;
	unsigned long long bytes;

	if (blocks == 0 || blockSize == 0) {
		return 0;
	}

	/* SCSI-U モード: image buffer を使わず lba/blockSize から転送量を計算する */
	if (X68000_GetStorageBusMode() == 2) {
		*imageOffset = 0; /* 未使用 */
		bytes = (unsigned long long)blocks * (unsigned long long)blockSize;
		*transferBytes = (DWORD)bytes;
		return 1;
	}

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		return 0;
	}

	dataOffset = SCSI_GetIocsDataOffset();
	dataSize = (unsigned long long)(DWORD)s_disk_image_buffer_size[4];
	if (dataSize == 0 || (unsigned long long)dataOffset >= dataSize) {
		return 0;
	}
	dataSize -= (unsigned long long)dataOffset;

	// Map IOCS LBA against detected data base (header-inclusive/exclusive).
	start = (unsigned long long)lba * (unsigned long long)blockSize;
	bytes = (unsigned long long)blocks * (unsigned long long)blockSize;
	if (start + bytes > dataSize) {
		return 0;
	}

	*imageOffset = dataOffset + (DWORD)start;
	*transferBytes = (DWORD)bytes;
	return 1;
}

static int SCSI_IsLinearRamRange(DWORD addr, DWORD len)
{
	unsigned long long start;
	unsigned long long end;

	if (len == 0) {
		return 1;
	}
	start = (unsigned long long)(addr & 0x00ffffff);
	end = start + (unsigned long long)len;
	// Allow main RAM ($000000-$BFFFFF), GVRAM ($C00000-$DFFFFF),
	// and text VRAM / sprite area ($E00000-$E7FFFF).
	// Games load graphics data directly into GVRAM via DMA-style
	// block reads; Memory_WriteB dispatches these to GVRAM_Write
	// automatically.  Stop before I/O space ($E80000+).
	if (start >= 0x00e80000ULL) {
		return 0;
	}
	if (end > 0x00e80000ULL || end < start) {
		return 0;
	}
	return 1;
}

static DWORD SCSI_Mask24(DWORD addr)
{
	return addr & 0x00ffffffU;
}

static void SCSI_SetReqStatus(DWORD reqpkt, int ok, BYTE errCode)
{
	// Human68k device driver request status word at reqpkt +3/+4.
	//
	// Human68k status convention (confirmed from kernel disasm at $CBAA):
	//   Kernel does: move.b (3,a5),d0; beq success
	//   → byte+3 = $00 means SUCCESS (zero check)
	//   → byte+3 ≠ $00 means ERROR (triggers error handler at $CBB8)
	// The MS-DOS DONE flag ($80) does NOT apply to Human68k!
	if (ok) {
		Memory_WriteB(reqpkt + 3, 0x00);  // Success (must be zero)
		Memory_WriteB(reqpkt + 4, 0x00);
	} else {
		Memory_WriteB(reqpkt + 3, 0x01);  // Error flag
		Memory_WriteB(reqpkt + 4, errCode ? errCode : 0x02);
	}
}

static void SCSI_LogKernelQueueState(const char* tag)
{
	static int s_queueLogCount = 0;
	DWORD bcc;
	WORD bca;
	BYTE bc6;
	BYTE bc7;
	char line[160];

	if (s_queueLogCount >= 48) {
		return;
	}
	bcc = ((DWORD)Memory_ReadB(0x00000bcc) << 24) |
	      ((DWORD)Memory_ReadB(0x00000bcd) << 16) |
	      ((DWORD)Memory_ReadB(0x00000bce) << 8) |
	      ((DWORD)Memory_ReadB(0x00000bcf));
	bca = ((WORD)Memory_ReadB(0x00000bca) << 8) |
	      ((WORD)Memory_ReadB(0x00000bcb));
	bc6 = Memory_ReadB(0x00000bc6);
	bc7 = Memory_ReadB(0x00000bc7);
	snprintf(line, sizeof(line),
	         "%s bca=$%04X bcc=%08X bc6=$%02X bc7=$%02X",
	         (tag != NULL) ? tag : "SCSI_DEV QUE",
	         (unsigned int)bca,
	         (unsigned int)(bcc & 0x00ffffffU),
	         (unsigned int)bc6,
	         (unsigned int)bc7);
	SCSI_LogText(line);
	s_queueLogCount++;
}

static void SCSI_NormalizeRootShortNames(DWORD bufAddr, DWORD startSec,
                                         DWORD count, DWORD secSize)
{
	DWORD s;
	DWORD rootStart = s_scsi_root_dir_start_sector;
	DWORD rootCount = s_scsi_root_dir_sector_count;
	DWORD changed = 0;
	char logLine[128];

	if (rootCount == 0 || secSize < 32 || count == 0) {
		return;
	}

	for (s = 0; s < count; s++) {
		DWORD sec = startSec + s;
		DWORD secBase;
		DWORD off;
		if (sec < rootStart || sec >= rootStart + rootCount) {
			continue;
		}
		secBase = bufAddr + (s * secSize);
		if (!SCSI_IsLinearRamRange(secBase, secSize)) {
			continue;
		}
		for (off = 0; off + 32 <= secSize; off += 32) {
			DWORD ent = secBase + off;
			BYTE first = Memory_ReadB(ent + 0);
			BYTE attr;
			DWORD j;
			if (first == 0x00) {
				break;
			}
			if (first == 0xE5) {
				continue;
			}
			attr = Memory_ReadB(ent + 0x0B);
			if (attr == 0x0F) {
				continue; // VFAT long-name entry
			}
			for (j = 0; j < 11; j++) {
				BYTE ch = Memory_ReadB(ent + j);
				if (ch >= 'a' && ch <= 'z') {
					Memory_WriteB(ent + j, (BYTE)(ch - ('a' - 'A')));
					changed++;
				}
			}
		}
	}

	if (changed != 0) {
		snprintf(logLine, sizeof(logLine),
		         "SCSI_DEV READ normalized root 8.3 lowercase -> uppercase (%u chars)",
		         (unsigned int)changed);
		SCSI_LogText(logLine);
	}
}

static void SCSI_LogRootConfigEntry(DWORD bufAddr, DWORD startSec,
                                    DWORD count, DWORD secSize)
{
	DWORD s;
	DWORD rootStart = s_scsi_root_dir_start_sector;
	DWORD rootCount = s_scsi_root_dir_sector_count;
	char line[256];

	if (rootCount == 0 || secSize < 32 || count == 0) {
		return;
	}

	for (s = 0; s < count; s++) {
		DWORD sec = startSec + s;
		DWORD secBase;
		DWORD off;
		if (sec < rootStart || sec >= rootStart + rootCount) {
			continue;
		}
		secBase = bufAddr + (s * secSize);
		if (!SCSI_IsLinearRamRange(secBase, secSize)) {
			continue;
		}
		for (off = 0; off + 32 <= secSize; off += 32) {
			DWORD ent = secBase + off;
			BYTE first = Memory_ReadB(ent + 0);
			BYTE attr = Memory_ReadB(ent + 0x0B);
			WORD firstCluster;
			DWORD fileSize;

			if (first == 0x00) {
				break;
			}
			if (first == 0xE5 || attr == 0x0F) {
				continue;
			}
			if (Memory_ReadB(ent + 0) != 'C' || Memory_ReadB(ent + 1) != 'O' ||
			    Memory_ReadB(ent + 2) != 'N' || Memory_ReadB(ent + 3) != 'F' ||
			    Memory_ReadB(ent + 4) != 'I' || Memory_ReadB(ent + 5) != 'G' ||
			    Memory_ReadB(ent + 6) != ' ' || Memory_ReadB(ent + 7) != ' ' ||
			    Memory_ReadB(ent + 8) != 'S' || Memory_ReadB(ent + 9) != 'Y' ||
			    Memory_ReadB(ent + 10) != 'S') {
				continue;
			}

			snprintf(line, sizeof(line),
			         "SCSI_DEV READ CONFIG entry32=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)Memory_ReadB(ent + 0x00), (unsigned int)Memory_ReadB(ent + 0x01),
			         (unsigned int)Memory_ReadB(ent + 0x02), (unsigned int)Memory_ReadB(ent + 0x03),
			         (unsigned int)Memory_ReadB(ent + 0x04), (unsigned int)Memory_ReadB(ent + 0x05),
			         (unsigned int)Memory_ReadB(ent + 0x06), (unsigned int)Memory_ReadB(ent + 0x07),
			         (unsigned int)Memory_ReadB(ent + 0x08), (unsigned int)Memory_ReadB(ent + 0x09),
			         (unsigned int)Memory_ReadB(ent + 0x0A), (unsigned int)Memory_ReadB(ent + 0x0B),
			         (unsigned int)Memory_ReadB(ent + 0x0C), (unsigned int)Memory_ReadB(ent + 0x0D),
			         (unsigned int)Memory_ReadB(ent + 0x0E), (unsigned int)Memory_ReadB(ent + 0x0F),
			         (unsigned int)Memory_ReadB(ent + 0x10), (unsigned int)Memory_ReadB(ent + 0x11),
			         (unsigned int)Memory_ReadB(ent + 0x12), (unsigned int)Memory_ReadB(ent + 0x13),
			         (unsigned int)Memory_ReadB(ent + 0x14), (unsigned int)Memory_ReadB(ent + 0x15),
			         (unsigned int)Memory_ReadB(ent + 0x16), (unsigned int)Memory_ReadB(ent + 0x17),
			         (unsigned int)Memory_ReadB(ent + 0x18), (unsigned int)Memory_ReadB(ent + 0x19),
			         (unsigned int)Memory_ReadB(ent + 0x1A), (unsigned int)Memory_ReadB(ent + 0x1B),
			         (unsigned int)Memory_ReadB(ent + 0x1C), (unsigned int)Memory_ReadB(ent + 0x1D),
			         (unsigned int)Memory_ReadB(ent + 0x1E), (unsigned int)Memory_ReadB(ent + 0x1F));
			SCSI_LogText(line);

			firstCluster = (WORD)Memory_ReadB(ent + 0x1A) |
			               ((WORD)Memory_ReadB(ent + 0x1B) << 8);
			fileSize = (DWORD)Memory_ReadB(ent + 0x1C) |
			           ((DWORD)Memory_ReadB(ent + 0x1D) << 8) |
			           ((DWORD)Memory_ReadB(ent + 0x1E) << 16) |
			           ((DWORD)Memory_ReadB(ent + 0x1F) << 24);
			snprintf(line, sizeof(line),
			         "SCSI_DEV READ CONFIG meta attr=$%02X cluster=%u size=%u",
			         (unsigned int)attr,
			         (unsigned int)firstCluster,
			         (unsigned int)fileSize);
			SCSI_LogText(line);
			if (firstCluster >= 2 && s_scsi_sec_per_clus != 0) {
				DWORD cfgSec = s_scsi_data_start_sector +
				               ((DWORD)(firstCluster - 2) * (DWORD)s_scsi_sec_per_clus);
				s_scsi_config_sector = cfgSec;
				s_scsi_config_size = fileSize;
				snprintf(line, sizeof(line),
				         "SCSI_DEV CONFIG map sec=%u size=%u spc=%u dataStart=%u",
				         (unsigned int)s_scsi_config_sector,
				         (unsigned int)s_scsi_config_size,
				         (unsigned int)s_scsi_sec_per_clus,
				         (unsigned int)s_scsi_data_start_sector);
				SCSI_LogText(line);
			}
			return;
		}
	}
}


// -----------------------------------------------------------------------
//   SCSI ブートトラップハンドラ
//   ブートセクタ (先頭8セクタ=2048バイト) を HDD イメージから読み込み
//   M68000 メモリ $002000 にコピーして d0=0 (成功) を返す
// -----------------------------------------------------------------------
static void SCSI_HandleBoot(void)
{
#if defined(HAVE_C68K)
	DWORD i;
	DWORD bootSize;
	DWORD bootOffset = 0;
	DWORD partBootOffset = 0;
	DWORD blockSize;
	DWORD bootBlockSize = 0;
	DWORD bootBaseLBA;
	DWORD d5Exp = 0;
	DWORD destAddr = 0x002000;
	DWORD imgSize;
	BYTE* imgBuf;
	char bootLog[128];

	printf("SCSI_HandleBoot: Attempting SCSI boot...\n");

#ifdef __APPLE__
	if (X68000_GetStorageBusMode() == 2) {
		/* SCSI-U モード: 物理 HDD の先頭を USB 経由で読み込む */
		if (!SCSIU_EnsureBootCache()) {
			printf("SCSI_HandleBoot: SCSI-U boot cache load failed\n");
			SCSI_LogText("SCSIU_BOOT: boot cache load failed");
			C68k_Set_DReg(&C68K, 0, 0xFFFFFFFF);
			return;
		}
	}
#endif

	if (SCSI_ImgBuf() == NULL || SCSI_ImgSize() <= 0) {
		printf("SCSI_HandleBoot: No HDD image loaded\n");
		SCSI_LogText("SCSI_BOOT: no HDD image");
		C68k_Set_DReg(&C68K, 0, 0xFFFFFFFF);
		return;
	}
	imgBuf = SCSI_ImgBuf();
	imgSize = (DWORD)SCSI_ImgSize();

	blockSize = SCSI_GetImageBlockSize();
	if (blockSize == 0) {
		blockSize = 512;
	}
	// SCSI HDD boot code lives at LBA2.  X68SCSI1 containers place the payload
	// (virtual LBA2) at 0x400; raw images need an explicit +2 sector offset.
	bootOffset = SCSI_GetPayloadOffset();
	if (bootOffset == 0) {
		bootOffset = blockSize * 2;
	}
	partBootOffset = SCSI_FindPartitionBootOffset();

	// Prefer the actual partition boot sector when available.
	// Many SxSI container IPL stubs at LBA2 are menu/loader code that expects
	// firmware behaviors not fully emulated here.
	if (partBootOffset != 0 &&
	    partBootOffset + 4 <= imgSize &&
	    (imgBuf[partBootOffset] & 0xF0) == 0x60 &&
	    partBootOffset != bootOffset) {
		snprintf(bootLog, sizeof(bootLog),
		         "SCSI_BOOT prefer partBoot=0x%X over lba2=0x%X",
		         (unsigned int)partBootOffset, (unsigned int)bootOffset);
		SCSI_LogText(bootLog);
		bootOffset = partBootOffset;
	}
	// SxSI menu IPL often expects firmware-side console/IOCS behavior that
	// is incomplete in this synthetic path.  When a valid partition boot
	// sector exists, jump there directly instead of executing the menu layer.
	if (bootOffset + 0x40 <= imgSize &&
	    memcmp(imgBuf + bootOffset + 0x2A, "SxSI Disk IPL MENU", 17) == 0) {
		if (partBootOffset != 0 &&
		    partBootOffset + 4 <= imgSize &&
		    (imgBuf[partBootOffset] & 0xF0) == 0x60) {
			snprintf(bootLog, sizeof(bootLog),
			         "SCSI_BOOT bypass SxSI menu -> part=0x%X",
			         (unsigned int)partBootOffset);
			SCSI_LogText(bootLog);
			bootOffset = partBootOffset;
		}
	}
	// LBA2ブートが壊れているイメージに限り、パーティションブートへ退避。
	if (bootOffset + 4 <= imgSize && (imgBuf[bootOffset] & 0xF0) != 0x60) {
		if (partBootOffset != 0 &&
		    partBootOffset + 4 <= imgSize &&
		    (imgBuf[partBootOffset] & 0xF0) == 0x60) {
			snprintf(bootLog, sizeof(bootLog),
			         "SCSI_BOOT fallback part=0x%X lba2first=%02X%02X%02X%02X",
			         (unsigned int)partBootOffset,
			         (unsigned int)imgBuf[bootOffset + 0],
			         (unsigned int)imgBuf[bootOffset + 1],
			         (unsigned int)imgBuf[bootOffset + 2],
			         (unsigned int)imgBuf[bootOffset + 3]);
			SCSI_LogText(bootLog);
			bootOffset = partBootOffset;
		}
	}
	bootBlockSize = SCSI_DetectBootBlockSize(bootOffset);
	if (bootBlockSize != 0) {
		blockSize = bootBlockSize;
	}
	bootBaseLBA = bootOffset / blockSize;
	if (blockSize == 512) {
		d5Exp = 1;
	} else if (blockSize == 1024) {
		d5Exp = 2;
	} else if (blockSize >= 2048) {
		d5Exp = 3;
	}

	// ブートコードとして先頭8セクタをロード
	bootSize = blockSize * 8;
	if (bootOffset + bootSize > (DWORD)SCSI_ImgSize()) {
		bootSize = (DWORD)SCSI_ImgSize() - bootOffset;
	}
	if (bootSize < 16) {
		snprintf(bootLog, sizeof(bootLog),
		         "SCSI_BOOT: invalid bootSize=%u offset=0x%X",
		         (unsigned int)bootSize, (unsigned int)bootOffset);
		SCSI_LogText(bootLog);
		C68k_Set_DReg(&C68K, 0, 0xFFFFFFFF);
		return;
	}

	// M68000 メモリにコピー (バイト単位で書き込み、エンディアン考慮)
	{
		BYTE* src = SCSI_ImgBuf();
		for (i = 0; i < bootSize; i++) {
			Memory_WriteB(destAddr + i, src[bootOffset + i]);
		}
		snprintf(bootLog, sizeof(bootLog),
		         "SCSI_BOOT: load=%u offset=0x%X lbaBase=%u blk=%u d5=%u first=%02X%02X%02X%02X",
		         (unsigned int)bootSize, (unsigned int)bootOffset,
		         (unsigned int)bootBaseLBA,
		         (unsigned int)blockSize,
		         (unsigned int)d5Exp,
		         src[bootOffset + 0], src[bootOffset + 1],
		         src[bootOffset + 2], src[bootOffset + 3]);
	}
	SCSI_LogText(bootLog);

	// Real SCSI IPL passes sector-size code in d5 and target ID in d1.
	// Some boot sectors rely on these registers before the first IOCS call.
	C68k_Set_DReg(&C68K, 5, d5Exp);
	// Real SCSI IPL passes boot target ID in d1 (ID0 -> $20).  Some boot
	// sectors reuse this immediately for follow-up IOCS reads.
	C68k_Set_DReg(&C68K, 1, 0x00000020);

	printf("SCSI_HandleBoot: Loaded %d bytes from +0x%X to $%06X (d5=%u)\n",
	       (int)bootSize, (int)bootOffset, (int)destAddr, (unsigned int)d5Exp);

	// Initialize CRTC and text palette for SCSI boot.
	// Set up a standard 768x512 31kHz text mode so native ROM display handlers
	// (especially fn=$21 _B_PRINT for boot messages) produce visible output.
	{
		SCSI_LogText("SCSI_BOOT_CRTC_INIT setting 768x512 31kHz mode");
		CRTC_Write(0xE80000, 0x00); CRTC_Write(0xE80001, 0x89);  // R0
		CRTC_Write(0xE80002, 0x00); CRTC_Write(0xE80003, 0x0E);  // R1
		CRTC_Write(0xE80006, 0x00); CRTC_Write(0xE80007, 0x7C);  // R3
		CRTC_Write(0xE80004, 0x00); CRTC_Write(0xE80005, 0x1C);  // R2
		CRTC_Write(0xE80008, 0x02); CRTC_Write(0xE80009, 0x37);  // R4
		CRTC_Write(0xE8000A, 0x00); CRTC_Write(0xE8000B, 0x05);  // R5
		CRTC_Write(0xE8000E, 0x02); CRTC_Write(0xE8000F, 0x28);  // R7
		CRTC_Write(0xE8000C, 0x00); CRTC_Write(0xE8000D, 0x28);  // R6
		CRTC_Write(0xE80010, 0x00); CRTC_Write(0xE80011, 0x1B);  // R8
		CRTC_Write(0xE80028, 0x00); CRTC_Write(0xE80029, 0x16);  // R20
		VCtrl_Write(0xE82601, 0x20);  // text layer ON
		Pal_Regs[0x200] = 0x00; Pal_Regs[0x201] = 0x00;
		TextPal[0] = Pal16[0x0000];  // black background
		Pal_Regs[0x202] = 0xFF; Pal_Regs[0x203] = 0xFE;
		TextPal[1] = Pal16[0xFFFE];  // white foreground
		TVRAM_SetAllDirty();
		snprintf(bootLog, sizeof(bootLog),
		         "SCSI_BOOT_CRTC_DONE VSTART=%u VEND=%u",
		         (unsigned)CRTC_VSTART, (unsigned)CRTC_VEND);
		SCSI_LogText(bootLog);
	}

	// Lower IPL level to allow interrupts (timer, keyboard, VSync).
	// Real IPL ROM enables interrupts before jumping to boot code.
	// Without this, SR=$2700 (IPL=7) masks ALL interrupts and
	// Human68k hangs in polling loops that expect timer interrupts.
	C68k_Set_SR(&C68K, 0x2000);  // supervisor mode, IPL=0
	SCSI_LogText("SCSI_BOOT_SR_FIX set SR=$2000 (IPL=0, interrupts enabled)");

	C68k_Set_DReg(&C68K, 0, 0);  // d0 = 0 (成功)
#endif
}


// -----------------------------------------------------------------------
//   IPL-ROM-first SCSI ブート注入
//   IPL ROMの初期化完了後（SASIデバイスセレクト検出時）に呼ばれる。
//   ブートセクタをHDDイメージから$2000にロードし、IOCS[$F5]/$A0をパッチ、
//   レジスタ設定後 PC=$2000 でブートセクタに制御を移す。
// -----------------------------------------------------------------------
void SCSI_InjectBoot(void)
{
#if defined(HAVE_C68K)
	DWORD i;
	DWORD bootSize;
	DWORD bootOffset = 0;
	DWORD partBootOffset = 0;
	DWORD blockSize;
	DWORD bootBlockSize = 0;
	DWORD d5Exp = 0;
	DWORD destAddr = 0x002000;
	DWORD imgSize;
	BYTE* imgBuf;
	char bootLog[192];

	SCSI_LogText("SCSI_INJECT_BOOT: IPL ROM init complete, injecting SCSI boot");
	printf("SCSI_InjectBoot: IPL ROM init complete, injecting SCSI boot...\n");

#ifdef __APPLE__
	if (X68000_GetStorageBusMode() == 2) {
		/* SCSI-U モード: ブートキャッシュをロード */
		if (!SCSIU_EnsureBootCache()) {
			printf("SCSI_InjectBoot: SCSI-U boot cache load failed\n");
			SCSI_LogText("SCSIU_INJECT: boot cache load failed");
			return;
		}
	}
#endif

	// Verify buffer integrity at BPB area (image mode only — SCSI-U cache may be smaller)
	if (s_disk_image_buffer[4] != NULL && s_disk_image_buffer_size[4] > 0x8020
	    && X68000_GetStorageBusMode() != 2) {
		char vb[256];
		snprintf(vb, sizeof(vb),
		         "BUF_VERIFY_INJECT @0x8012: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
		         s_disk_image_buffer[4][0x8012], s_disk_image_buffer[4][0x8013],
		         s_disk_image_buffer[4][0x8014], s_disk_image_buffer[4][0x8015],
		         s_disk_image_buffer[4][0x8016], s_disk_image_buffer[4][0x8017],
		         s_disk_image_buffer[4][0x8018], s_disk_image_buffer[4][0x8019],
		         s_disk_image_buffer[4][0x801A], s_disk_image_buffer[4][0x801B],
		         s_disk_image_buffer[4][0x801C], s_disk_image_buffer[4][0x801D],
		         s_disk_image_buffer[4][0x801E], s_disk_image_buffer[4][0x801F]);
		SCSI_LogText(vb);
		if (s_disk_image_buffer_size[4] > 0x40D26) {
			snprintf(vb, sizeof(vb),
			         "BUF_VERIFY_INJECT rootdir8@0x40D16: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			         s_disk_image_buffer[4][0x40D16], s_disk_image_buffer[4][0x40D17],
			         s_disk_image_buffer[4][0x40D18], s_disk_image_buffer[4][0x40D19],
			         s_disk_image_buffer[4][0x40D1A], s_disk_image_buffer[4][0x40D1B],
			         s_disk_image_buffer[4][0x40D1C], s_disk_image_buffer[4][0x40D1D],
			         s_disk_image_buffer[4][0x40D1E], s_disk_image_buffer[4][0x40D1F]);
			SCSI_LogText(vb);
		}
	}

	if (SCSI_ImgBuf() == NULL || SCSI_ImgSize() <= 0) {
		printf("SCSI_InjectBoot: No HDD image loaded\n");
		SCSI_LogText("SCSI_INJECT_BOOT: no HDD image - aborting");
		return;
	}
	imgBuf = SCSI_ImgBuf();
	imgSize = (DWORD)SCSI_ImgSize();

	blockSize = SCSI_GetImageBlockSize();
	if (blockSize == 0) {
		blockSize = 512;
	}
	bootOffset = SCSI_GetPayloadOffset();
	if (bootOffset == 0) {
		bootOffset = blockSize * 2;
	}
	partBootOffset = SCSI_FindPartitionBootOffset();

	if (partBootOffset != 0 &&
	    partBootOffset + 4 <= imgSize &&
	    (imgBuf[partBootOffset] & 0xF0) == 0x60 &&
	    partBootOffset != bootOffset) {
		snprintf(bootLog, sizeof(bootLog),
		         "SCSI_INJECT prefer partBoot=0x%X over lba2=0x%X",
		         (unsigned int)partBootOffset, (unsigned int)bootOffset);
		SCSI_LogText(bootLog);
		bootOffset = partBootOffset;
	}
	if (bootOffset + 0x40 <= imgSize &&
	    memcmp(imgBuf + bootOffset + 0x2A, "SxSI Disk IPL MENU", 17) == 0) {
		if (partBootOffset != 0 &&
		    partBootOffset + 4 <= imgSize &&
		    (imgBuf[partBootOffset] & 0xF0) == 0x60) {
			snprintf(bootLog, sizeof(bootLog),
			         "SCSI_INJECT bypass SxSI menu -> part=0x%X",
			         (unsigned int)partBootOffset);
			SCSI_LogText(bootLog);
			bootOffset = partBootOffset;
		}
	}
	if (bootOffset + 4 <= imgSize && (imgBuf[bootOffset] & 0xF0) != 0x60) {
		if (partBootOffset != 0 &&
		    partBootOffset + 4 <= imgSize &&
		    (imgBuf[partBootOffset] & 0xF0) == 0x60) {
			snprintf(bootLog, sizeof(bootLog),
			         "SCSI_INJECT fallback part=0x%X lba2first=%02X%02X%02X%02X",
			         (unsigned int)partBootOffset,
			         (unsigned int)imgBuf[bootOffset + 0],
			         (unsigned int)imgBuf[bootOffset + 1],
			         (unsigned int)imgBuf[bootOffset + 2],
			         (unsigned int)imgBuf[bootOffset + 3]);
			SCSI_LogText(bootLog);
			bootOffset = partBootOffset;
		}
	}
	bootBlockSize = SCSI_DetectBootBlockSize(bootOffset);
	if (bootBlockSize != 0) {
		blockSize = bootBlockSize;
	}
	if (blockSize == 512) {
		d5Exp = 1;
	} else if (blockSize == 1024) {
		d5Exp = 2;
	} else if (blockSize >= 2048) {
		d5Exp = 3;
	}

	bootSize = blockSize * 8;
	if (bootOffset + bootSize > imgSize) {
		bootSize = imgSize - bootOffset;
	}
	if (bootSize < 16) {
		snprintf(bootLog, sizeof(bootLog),
		         "SCSI_INJECT: invalid bootSize=%u offset=0x%X",
		         (unsigned int)bootSize, (unsigned int)bootOffset);
		SCSI_LogText(bootLog);
		return;
	}

	for (i = 0; i < bootSize; i++) {
		Memory_WriteB(destAddr + i, imgBuf[bootOffset + i]);
	}

	snprintf(bootLog, sizeof(bootLog),
	         "SCSI_INJECT: load=%u offset=0x%X blk=%u d5=%u first=%02X%02X%02X%02X",
	         (unsigned int)bootSize, (unsigned int)bootOffset,
	         (unsigned int)blockSize, (unsigned int)d5Exp,
	         imgBuf[bootOffset + 0], imgBuf[bootOffset + 1],
	         imgBuf[bootOffset + 2], imgBuf[bootOffset + 3]);
	SCSI_LogText(bootLog);

	s_scsi_deferred_boot_pending = 1;
	s_scsi_deferred_boot_addr = destAddr;
	s_scsi_deferred_d5 = d5Exp;

	snprintf(bootLog, sizeof(bootLog),
	         "SCSI_INJECT: deferred boot armed addr=$%06X d5=%u",
	         (unsigned int)destAddr, (unsigned int)d5Exp);
	SCSI_LogText(bootLog);
	printf("SCSI_InjectBoot: boot sector loaded, deferred jump addr=$%06X\n",
	       (unsigned int)destAddr);
#endif
}

int SCSI_HasDeferredBoot(void)
{
	return s_scsi_deferred_boot_pending;
}

void SCSI_CommitDeferredBoot(void)
{
#if defined(HAVE_C68K)
	char bootLog[192];

	if (!s_scsi_deferred_boot_pending) {
		return;
	}

	// Commit after the current C68K slice returns so the core-local PC/BasePC
	// state cannot be desynchronized by a mid-instruction jump from SASI_Write.
	Memory_WriteD(0x7D4, SCSI_SYNTH_IOCS_ENTRY);
	C68k_Set_DReg(&C68K, 0, 0);
	C68k_Set_DReg(&C68K, 1, 0x00000020);
	C68k_Set_DReg(&C68K, 5, s_scsi_deferred_d5);
	C68k_Set_SR(&C68K, 0x2000);
	Memory_SetSCSIMode();
	C68k_Set_PC(&C68K, s_scsi_deferred_boot_addr);
	cpu_setOPbase24(s_scsi_deferred_boot_addr);

	snprintf(bootLog, sizeof(bootLog),
	         "SCSI_INJECT_COMMIT: addr=$%06X d5=%u sr=$%04X",
	         (unsigned int)s_scsi_deferred_boot_addr,
	         (unsigned int)s_scsi_deferred_d5,
	         (unsigned int)(C68k_Get_SR(&C68K) & 0xffff));
	SCSI_LogText(bootLog);

	s_scsi_deferred_boot_pending = 0;
	s_scsi_deferred_boot_addr = 0;
	s_scsi_deferred_d5 = 0;
#endif
}



// -----------------------------------------------------------------------
//   SCSI IOCS トラップハンドラ
//   d1.b = SCSI IOCS コマンド番号
//   d0.l = ステータス (0 成功, -1 失敗)
// -----------------------------------------------------------------------
static void SCSI_HandleIOCS(BYTE cmd)
{
#if defined(HAVE_C68K)
	DWORD d2 = C68k_Get_DReg(&C68K, 2);
	DWORD d3 = C68k_Get_DReg(&C68K, 3);
	DWORD d4 = C68k_Get_DReg(&C68K, 4);
	DWORD d5 = C68k_Get_DReg(&C68K, 5);
	DWORD d1 = C68k_Get_DReg(&C68K, 1);
	DWORD a1 = C68k_Get_AReg(&C68K, 1);
	DWORD result = 0xFFFFFFFF;
	DWORD i;
	char logLine[192];


	snprintf(logLine, sizeof(logLine),
	         "SCSI_IOCS_BEGIN cmd=$%02X d1=%08X d2=%08X d3=%08X d4=%08X d5=%08X a1=%08X",
	         cmd, (unsigned int)d1, (unsigned int)d2, (unsigned int)d3, (unsigned int)d4,
	         (unsigned int)d5, (unsigned int)a1);
	SCSI_LogText(logLine);

		switch (cmd) {
	case 0x20: {  // _S_INQUIRY
		BYTE inquiry[36];
		DWORD len = d3 & 0xff;
		DWORD copyLen;

		memset(inquiry, 0x20, sizeof(inquiry));
		inquiry[0] = 0x00;  // Direct access device
		inquiry[1] = 0x00;  // non-removable
		inquiry[2] = 0x02;  // SCSI-2
		inquiry[3] = 0x00;
		inquiry[4] = 31;    // additional length
		memcpy(&inquiry[8], "PX68K   ", 8);
		memcpy(&inquiry[16], "SCSI HDD EMU    ", 16);
		memcpy(&inquiry[32], "1.00", 4);

		copyLen = (len < (DWORD)sizeof(inquiry)) ? len : (DWORD)sizeof(inquiry);
		if (!SCSI_IsLinearRamRange(a1, copyLen)) {
			result = 0xFFFFFFFF;
			break;
		}
		for (i = 0; i < copyLen; i++) {
			Memory_WriteB(a1 + i, inquiry[i]);
		}
		result = 0;
		break;
	}

		case 0x21:  // _S_READ
		case 0x26:  // _S_READEXT
		case 0x2e: {  // _S_READI
			DWORD lba;
			DWORD blocks;
			DWORD blockSize = SCSI_GetBlockSizeFromCode(d5);
			DWORD partBaseLba = 0;
			int usePackedLegacy = 0;
			DWORD imageOffset;
			DWORD transferBytes;

				lba = SCSI_GetTransferLBA(cmd, d2, d4);
				if (blockSize != 0 && s_scsi_partition_byte_offset != 0) {
					partBaseLba = (DWORD)((unsigned long long)s_scsi_partition_byte_offset /
					                     (unsigned long long)blockSize);
				}
				// Boot-path packed register encoding: when d1=$0640 the
				// registers d1-d3 carry data-structure values (d1=$0640,
				// d2=$0641, d3=$06420643). The real LBA is d2's low byte
				// plus the partition base, and block count comes from d4.
				if (cmd == 0x21 &&
				    d1 == 0x00000640U &&
				    (d2 & 0xffff0000U) == 0U) {
					DWORD packedLba = d2 & 0x000000ffU;
					if (partBaseLba != 0) {
						packedLba += partBaseLba;
					}
					if (packedLba != lba) {
						snprintf(logLine, sizeof(logLine),
						         "SCSI_XFER_LBA_PACKED cmd=$%02X d1=%08X d2=%08X lba=%u->%u base=%u",
						         (unsigned int)cmd,
						         (unsigned int)d1,
						         (unsigned int)d2,
						         (unsigned int)lba,
						         (unsigned int)packedLba,
						         (unsigned int)partBaseLba);
						SCSI_LogText(logLine);
						lba = packedLba;
					}
					usePackedLegacy = 1;
				}
				if ((cmd == 0x26 || cmd == 0x27) && (d2 & 0x00ffffffU) == 0 && lba != 0) {
					snprintf(logLine, sizeof(logLine),
					         "SCSI_XFER_LBA_FALLBACK cmd=$%02X d2=%08X d4=%08X -> lba=%u",
				         cmd, (unsigned int)d2, (unsigned int)d4, (unsigned int)lba);
				SCSI_LogText(logLine);
			}
			if (cmd == 0x2e && d1 == 0xffffffffU) {
				// Some forced-boot IPL paths use _S_READI with d1=-1 as a probe
				// before full SCSI stack handoff. Treat it as success no-op.
				snprintf(logLine, sizeof(logLine),
				         "SCSI_XFER_READI_PROBE d2=%08X d3=%08X d4=%08X d5=%08X",
				         (unsigned int)d2,
				         (unsigned int)d3,
				         (unsigned int)d4,
				         (unsigned int)d5);
				SCSI_LogText(logLine);
				C68k_Set_DReg(&C68K, 2, 0);
				result = 0;
				break;
			}
			blocks = d3;
			if (cmd == 0x21) {
				blocks &= 0xff;
				if (blocks == 0) {
					blocks = 256;
				}
				// Some legacy boot paths pack non-count metadata into d3 and carry
				// transfer byte size in d4 (for example d3=$40020800, d4=$00000400).
				// Interpreting d3.b==0 as 256 blocks in that case over-reads and
				// clobbers boot code. Prefer a sane d4-derived count when available.
				if (!usePackedLegacy &&
				    (d3 & 0xffffff00U) != 0 &&
				    d4 != 0 &&
				    blockSize != 0) {
					DWORD bytesHint = d4 & 0x00ffffffU;
					DWORD hintedBlocks = bytesHint / blockSize;
					if (bytesHint != 0 && hintedBlocks == 0) {
						hintedBlocks = 1;
					}
					if (hintedBlocks > 0 && hintedBlocks <= 256 && hintedBlocks < blocks) {
						snprintf(logLine, sizeof(logLine),
						         "SCSI_XFER_BLK_HINT cmd=$%02X d3=%08X d4=%08X blocks=%u->%u",
						         (unsigned int)cmd,
						         (unsigned int)d3,
						         (unsigned int)d4,
						         (unsigned int)blocks,
						         (unsigned int)hintedBlocks);
						SCSI_LogText(logLine);
						blocks = hintedBlocks;
					}
				}
				if (usePackedLegacy) {
					DWORD packedBlocks = d4 & 0x0000ffffU;
					if (packedBlocks == 0) {
						packedBlocks = 1;
					}
					snprintf(logLine, sizeof(logLine),
					         "SCSI_XFER_BLK_PACKED cmd=$%02X d4=%08X blocks=%u->%u",
					         (unsigned int)cmd,
					         (unsigned int)d4,
					         (unsigned int)blocks,
					         (unsigned int)packedBlocks);
					SCSI_LogText(logLine);
					blocks = packedBlocks;
				}
			} else if (cmd == 0x26) {
				// _S_READEXT: some boot sectors pass packed metadata in d3
				// (for example d3=$40020800) and effective transfer bytes in d4.
				// Prefer a sane d4-derived block count when present.
				if ((d3 & 0xffff0000U) != 0 &&
				    d4 != 0 &&
				    blockSize != 0) {
					DWORD bytesHint = d4 & 0x00ffffffU;
					DWORD hintedBlocks = bytesHint / blockSize;
					if (bytesHint != 0 && hintedBlocks == 0) {
						hintedBlocks = 1;
					}
					if (hintedBlocks > 0 && hintedBlocks < blocks) {
						snprintf(logLine, sizeof(logLine),
						         "SCSI_XFER_BLK_HINT cmd=$%02X d3=%08X d4=%08X blocks=%u->%u",
						         (unsigned int)cmd,
						         (unsigned int)d3,
						         (unsigned int)d4,
						         (unsigned int)blocks,
						         (unsigned int)hintedBlocks);
						SCSI_LogText(logLine);
						blocks = hintedBlocks;
					}
				}
			} else if (cmd == 0x2e) {
			// _S_READI uses its own parameter semantics; d3=0 appears as a probe/no-op
			// in this boot path, so do not force a sector transfer here.
				blocks &= 0xffff;
				if (blocks == 0) {
				snprintf(logLine, sizeof(logLine),
				         "SCSI_XFER_READI_NOOP d2=%08X d3=%08X d4=%08X d5=%08X",
				         (unsigned int)d2,
				         (unsigned int)d3,
				         (unsigned int)d4,
				         (unsigned int)d5);
				SCSI_LogText(logLine);
				C68k_Set_DReg(&C68K, 2, 0);
				result = 0;
				break;
			}
		}
			if (!SCSI_ResolveTransfer(lba, blocks, blockSize, &imageOffset, &transferBytes)) {
				result = 0xFFFFFFFF;
				break;
			}
		if (!SCSI_IsLinearRamRange(a1, transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}

		if (X68000_GetStorageBusMode() == 2) {
#ifdef __APPLE__
			BYTE* tmpRd = (BYTE*)malloc(transferBytes);
			if (tmpRd == NULL || !SCSIU_ReadBlocks(lba, blocks, blockSize, tmpRd)) {
				free(tmpRd);
				result = 0xFFFFFFFF;
				break;
			}
			for (i = 0; i < transferBytes; i++) {
				Memory_WriteB(a1 + i, tmpRd[i]);
			}
			free(tmpRd);
			snprintf(logLine, sizeof(logLine),
			         "SCSIU_XFER_READ cmd=$%02X lba=%u blocks=%u blockSize=%u",
			         cmd, (unsigned int)lba, (unsigned int)blocks, (unsigned int)blockSize);
			SCSI_LogText(logLine);
#else
			result = 0xFFFFFFFF;
			break;
#endif
		} else {
			for (i = 0; i < transferBytes; i++) {
				Memory_WriteB(a1 + i, s_disk_image_buffer[4][imageOffset + i]);
			}
			snprintf(logLine, sizeof(logLine),
			         "SCSI_XFER_READ cmd=$%02X lba=%u blocks=%u blockSize=%u imgOff=0x%X first=%02X%02X%02X%02X",
			         cmd,
			         (unsigned int)lba,
			         (unsigned int)blocks,
			         (unsigned int)blockSize,
			         (unsigned int)imageOffset,
			         s_disk_image_buffer[4][imageOffset + 0],
			         s_disk_image_buffer[4][imageOffset + 1],
			         s_disk_image_buffer[4][imageOffset + 2],
			         s_disk_image_buffer[4][imageOffset + 3]);
			SCSI_LogText(logLine);
		}
		result = 0;
		break;
	}

	case 0x22:  // _S_WRITE
	case 0x27: {  // _S_WRITEEXT
		DWORD lba;
		DWORD blocks;
		DWORD blockSize = SCSI_GetBlockSizeFromCode(d5);
		DWORD partBaseLba = 0;
		int usePackedLegacy = 0;
		DWORD imageOffset;
		DWORD transferBytes;

		lba = SCSI_GetTransferLBA(cmd, d2, d4);
		if (blockSize != 0 && s_scsi_partition_byte_offset != 0) {
			partBaseLba = (DWORD)((unsigned long long)s_scsi_partition_byte_offset /
			                     (unsigned long long)blockSize);
		}
		if (cmd == 0x22 &&
		    d1 == 0x00000640U &&
		    (d2 & 0xffff0000U) == 0 &&
		    (d2 & 0x0000ff00U) != 0 &&
		    (d2 & 0x0000ff00U) == (d1 & 0x0000ff00U)) {
			DWORD packedLba = d2 & 0x000000ffU;
			if (partBaseLba != 0) {
				packedLba += partBaseLba;
			}
			if (packedLba != lba) {
				snprintf(logLine, sizeof(logLine),
				         "SCSI_XFER_LBA_PACKED cmd=$%02X d1=%08X d2=%08X lba=%u->%u base=%u",
				         (unsigned int)cmd,
				         (unsigned int)d1,
				         (unsigned int)d2,
				         (unsigned int)lba,
				         (unsigned int)packedLba,
				         (unsigned int)partBaseLba);
				SCSI_LogText(logLine);
				lba = packedLba;
			}
			usePackedLegacy = 1;
		}
		blocks = d3;
			if (cmd == 0x22) {
				blocks &= 0xff;
				if (blocks == 0) {
					blocks = 256;
				}
				if ((d3 & 0xffffff00U) != 0 &&
				    d4 != 0 &&
				    blockSize != 0) {
					DWORD bytesHint = d4 & 0x00ffffffU;
					DWORD hintedBlocks = bytesHint / blockSize;
					if (bytesHint != 0 && hintedBlocks == 0) {
						hintedBlocks = 1;
					}
					if (hintedBlocks > 0 && hintedBlocks <= 256 && hintedBlocks < blocks) {
						snprintf(logLine, sizeof(logLine),
						         "SCSI_XFER_BLK_HINT cmd=$%02X d3=%08X d4=%08X blocks=%u->%u",
						         (unsigned int)cmd,
						         (unsigned int)d3,
						         (unsigned int)d4,
						         (unsigned int)blocks,
						         (unsigned int)hintedBlocks);
						SCSI_LogText(logLine);
						blocks = hintedBlocks;
					}
				}
				if (usePackedLegacy) {
					DWORD packedBlocks = d4 & 0x0000ffffU;
					if (packedBlocks == 0) {
						packedBlocks = 1;
					}
				snprintf(logLine, sizeof(logLine),
				         "SCSI_XFER_BLK_PACKED cmd=$%02X d4=%08X blocks=%u->%u",
				         (unsigned int)cmd,
				         (unsigned int)d4,
				         (unsigned int)blocks,
				         (unsigned int)packedBlocks);
					SCSI_LogText(logLine);
					blocks = packedBlocks;
				}
			} else if (cmd == 0x27) {
				if ((d3 & 0xffff0000U) != 0 &&
				    d4 != 0 &&
				    blockSize != 0) {
					DWORD bytesHint = d4 & 0x00ffffffU;
					DWORD hintedBlocks = bytesHint / blockSize;
					if (bytesHint != 0 && hintedBlocks == 0) {
						hintedBlocks = 1;
					}
					if (hintedBlocks > 0 && hintedBlocks < blocks) {
						snprintf(logLine, sizeof(logLine),
						         "SCSI_XFER_BLK_HINT cmd=$%02X d3=%08X d4=%08X blocks=%u->%u",
						         (unsigned int)cmd,
						         (unsigned int)d3,
						         (unsigned int)d4,
						         (unsigned int)blocks,
						         (unsigned int)hintedBlocks);
						SCSI_LogText(logLine);
						blocks = hintedBlocks;
					}
				}
			}
		if (!SCSI_ResolveTransfer(lba, blocks, blockSize, &imageOffset, &transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}
		if (!SCSI_IsLinearRamRange(a1, transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}

		if (X68000_GetStorageBusMode() == 2) {
#ifdef __APPLE__
			BYTE* tmpWr = (BYTE*)malloc(transferBytes);
			if (tmpWr == NULL) {
				result = 0xFFFFFFFF;
				break;
			}
			for (i = 0; i < transferBytes; i++) {
				tmpWr[i] = Memory_ReadB(a1 + i);
			}
			if (!SCSIU_WriteBlocks(lba, blocks, blockSize, tmpWr)) {
				free(tmpWr);
				result = 0xFFFFFFFF;
				break;
			}
			SCSIU_InvalidateBootCacheRange(lba, blocks, blockSize);
			free(tmpWr);
			snprintf(logLine, sizeof(logLine),
			         "SCSIU_XFER_WRITE cmd=$%02X lba=%u blocks=%u blockSize=%u",
			         cmd, (unsigned int)lba, (unsigned int)blocks, (unsigned int)blockSize);
			SCSI_LogText(logLine);
#else
			result = 0xFFFFFFFF;
			break;
#endif
		} else {
			for (i = 0; i < transferBytes; i++) {
				s_disk_image_buffer[4][imageOffset + i] = Memory_ReadB(a1 + i);
			}
			snprintf(logLine, sizeof(logLine),
			         "SCSI_XFER_WRITE cmd=$%02X lba=%u blocks=%u blockSize=%u imgOff=0x%X",
			         cmd,
			         (unsigned int)lba,
			         (unsigned int)blocks,
			         (unsigned int)blockSize,
			         (unsigned int)imageOffset);
			SCSI_LogText(logLine);
		}
		SASI_SetDirtyFlag(0);
		result = 0;
		break;
	}

	case 0x2f: {  // _S_STARTSTOP / extended read
		DWORD rawLba = d2 & 0x00ffffffU;
		DWORD lba = rawLba;
		DWORD blocks = d4 & 0x0000ffffU;
		DWORD blockSize = SCSI_GetBlockSizeFromCode(d5);
		DWORD partBaseLba = 0;
		int lbaAdjusted = 0;
		DWORD imageOffset;
		DWORD transferBytes;

		if (blocks == 0) {
			blocks = d3 & 0x0000ffffU;
		}
		if (blocks == 0) {
			blocks = 1;
		}
		if (blockSize != 0 && s_scsi_partition_byte_offset != 0) {
			partBaseLba = (DWORD)((unsigned long long)s_scsi_partition_byte_offset /
			                     (unsigned long long)blockSize);
			// Some boot paths pass partition-relative LBA for cmd=$2F.
			// If so, lift it to absolute disk LBA before resolving.
			if (partBaseLba > 0 && lba < partBaseLba) {
				lba += partBaseLba;
				lbaAdjusted = 1;
			}
		}
		if (!SCSI_ResolveTransfer(lba, blocks, blockSize, &imageOffset, &transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}
		if (!SCSI_IsLinearRamRange(a1, transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}
		if (X68000_GetStorageBusMode() == 2) {
#ifdef __APPLE__
			BYTE* tmpRd2 = (BYTE*)malloc(transferBytes);
			if (tmpRd2 == NULL || !SCSIU_ReadBlocks(lba, blocks, blockSize, tmpRd2)) {
				free(tmpRd2);
				result = 0xFFFFFFFF;
				break;
			}
			for (i = 0; i < transferBytes; i++) {
				Memory_WriteB(a1 + i, tmpRd2[i]);
			}
			free(tmpRd2);
			snprintf(logLine, sizeof(logLine),
			         "SCSIU_XFER_READ cmd=$%02X lba=%u blocks=%u blockSize=%u",
			         cmd, (unsigned int)lba, (unsigned int)blocks, (unsigned int)blockSize);
			SCSI_LogText(logLine);
			result = 0;
			break;
#else
			result = 0xFFFFFFFF;
			break;
#endif
		}
		for (i = 0; i < transferBytes; i++) {
			Memory_WriteB(a1 + i, s_disk_image_buffer[4][imageOffset + i]);
		}
		snprintf(logLine, sizeof(logLine),
		         "SCSI_XFER_READ cmd=$%02X lba=%u raw=%u base=%u adj=%u blocks=%u blockSize=%u imgOff=0x%X a1=%08X d3=%08X d4=%08X m1121c=%02X%02X%02X%02X m123d2=%02X%02X%02X%02X",
		         (unsigned int)cmd,
		         (unsigned int)lba,
		         (unsigned int)rawLba,
		         (unsigned int)partBaseLba,
		         (unsigned int)lbaAdjusted,
		         (unsigned int)blocks,
		         (unsigned int)blockSize,
		         (unsigned int)imageOffset,
		         (unsigned int)a1,
		         (unsigned int)d3,
		         (unsigned int)d4,
		         (unsigned int)Memory_ReadB(0x0001121c),
		         (unsigned int)Memory_ReadB(0x0001121d),
		         (unsigned int)Memory_ReadB(0x0001121e),
		         (unsigned int)Memory_ReadB(0x0001121f),
		         (unsigned int)Memory_ReadB(0x000123d2),
		         (unsigned int)Memory_ReadB(0x000123d3),
		         (unsigned int)Memory_ReadB(0x000123d4),
		         (unsigned int)Memory_ReadB(0x000123d5));
		SCSI_LogText(logLine);
		result = 0;
		break;
	}


	case 0x23:  // _S_FORMAT: physical format (dummy — image size preserved)
	case 0x2a:  // _S_MODESELECT
	case 0x2b:  // _S_REZEROUNIT
	case 0x2d:  // _S_SEEK
	case 0x30:  // _S_EJECT6MO1
	case 0x31:  // _S_REASSIGN
	case 0x36:  // _b_dskini
	case 0x37:  // _b_format
	case 0x38:  // _b_badfmt
	case 0x39:  // _b_assign
		result = 0;
		break;

	case 0x24:  // _S_TESTUNIT
		if (X68000_GetStorageBusMode() == 2) {
			result = SCSIU_IsConnected() ? 0 : 0xFFFFFFFF;
		} else {
			result = (s_disk_image_buffer[4] != NULL && s_disk_image_buffer_size[4] > 0) ? 0 : 0xFFFFFFFF;
		}
		break;

	case 0x25: {  // _S_READCAP
		DWORD blockSize = SCSI_GetImageBlockSize();
		DWORD dataOffset;
		DWORD dataSize = 0;
		DWORD totalBlocks;

		if (X68000_GetStorageBusMode() == 2) {
#ifdef __APPLE__
			DWORD lastLBA = 0;
			if (!SCSIU_IsConnected()) {
				result = 0xFFFFFFFF;
				break;
			}
			if (!SCSI_IsLinearRamRange(a1, 8)) {
				result = 0xFFFFFFFF;
				break;
			}
			/* SPC READ CAPACITY(10) で実際の容量を取得する */
			blockSize = 512;
			if (SCSIU_ReadCapacity(&lastLBA, &blockSize)) {
				totalBlocks = lastLBA + 1;
			} else {
				/* 失敗時フォールバック: 4GB 分 */
				blockSize = 512;
				totalBlocks = 0x800000;
			}
			Memory_WriteB(a1 + 0, (BYTE)(((totalBlocks - 1) >> 24) & 0xff));
			Memory_WriteB(a1 + 1, (BYTE)(((totalBlocks - 1) >> 16) & 0xff));
			Memory_WriteB(a1 + 2, (BYTE)(((totalBlocks - 1) >>  8) & 0xff));
			Memory_WriteB(a1 + 3, (BYTE)((totalBlocks - 1) & 0xff));
			Memory_WriteB(a1 + 4, (BYTE)((blockSize >> 24) & 0xff));
			Memory_WriteB(a1 + 5, (BYTE)((blockSize >> 16) & 0xff));
			Memory_WriteB(a1 + 6, (BYTE)((blockSize >>  8) & 0xff));
			Memory_WriteB(a1 + 7, (BYTE)(blockSize & 0xff));
			snprintf(logLine, sizeof(logLine),
			         "SCSIU_READCAP lastLBA=%u blockSize=%u",
			         (unsigned int)(totalBlocks - 1), (unsigned int)blockSize);
			SCSI_LogText(logLine);
			result = 0;
			break;
#else
			result = 0xFFFFFFFF;
			break;
#endif
		}

		dataOffset = SCSI_GetIocsDataOffset();
		if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
			result = 0xFFFFFFFF;
			break;
		}
		dataSize = (DWORD)s_disk_image_buffer_size[4];
		if (dataOffset >= dataSize) {
			result = 0xFFFFFFFF;
			break;
		}
		dataSize -= dataOffset;
		totalBlocks = dataSize / blockSize;
		if (totalBlocks == 0) {
			result = 0xFFFFFFFF;
			break;
		}
		if (!SCSI_IsLinearRamRange(a1, 8)) {
			result = 0xFFFFFFFF;
			break;
		}

		Memory_WriteB(a1 + 0, (BYTE)(((totalBlocks - 1) >> 24) & 0xff));
		Memory_WriteB(a1 + 1, (BYTE)(((totalBlocks - 1) >> 16) & 0xff));
		Memory_WriteB(a1 + 2, (BYTE)(((totalBlocks - 1) >> 8) & 0xff));
		Memory_WriteB(a1 + 3, (BYTE)((totalBlocks - 1) & 0xff));
		Memory_WriteB(a1 + 4, (BYTE)((blockSize >> 24) & 0xff));
		Memory_WriteB(a1 + 5, (BYTE)((blockSize >> 16) & 0xff));
		Memory_WriteB(a1 + 6, (BYTE)((blockSize >> 8) & 0xff));
		Memory_WriteB(a1 + 7, (BYTE)(blockSize & 0xff));
			snprintf(logLine, sizeof(logLine),
			         "SCSI_READCAP blocks=%u blockSize=%u dataOff=0x%X",
			         (unsigned int)totalBlocks,
			         (unsigned int)blockSize,
			         (unsigned int)dataOffset);
			SCSI_LogText(logLine);
			result = 0;
			break;
	}

	case 0x29: {  // _S_MODESENSE
		BYTE modeSense[12];
		DWORD len = d3 & 0xff;
		DWORD blockSize = SCSI_GetImageBlockSize();
		DWORD dataOffset = SCSI_GetIocsDataOffset();
		DWORD dataSize = 0;
		DWORD totalBlocks = 0;
		DWORD copyLen;

		memset(modeSense, 0, sizeof(modeSense));
		if (s_disk_image_buffer[4] != NULL) {
			dataSize = (DWORD)s_disk_image_buffer_size[4];
			if (dataOffset < dataSize) {
				dataSize -= dataOffset;
				totalBlocks = dataSize / blockSize;
			}
		}

		modeSense[0] = 0x0b;  // mode data length (excluding byte 0)
		modeSense[3] = 0x08;  // block descriptor length
		modeSense[5] = (BYTE)((totalBlocks >> 16) & 0xff);
		modeSense[6] = (BYTE)((totalBlocks >> 8) & 0xff);
		modeSense[7] = (BYTE)(totalBlocks & 0xff);
		modeSense[9] = (BYTE)((blockSize >> 16) & 0xff);
		modeSense[10] = (BYTE)((blockSize >> 8) & 0xff);
		modeSense[11] = (BYTE)(blockSize & 0xff);

		copyLen = (len < (DWORD)sizeof(modeSense)) ? len : (DWORD)sizeof(modeSense);
		if (!SCSI_IsLinearRamRange(a1, copyLen)) {
			result = 0xFFFFFFFF;
			break;
		}
		for (i = 0; i < copyLen; i++) {
			Memory_WriteB(a1 + i, modeSense[i]);
		}
		result = 0;
		break;
	}

	case 0x2c: {  // _S_REQUEST (REQUEST SENSE)
		DWORD len = d3 & 0xff;
		if (!SCSI_IsLinearRamRange(a1, len)) {
			result = 0xFFFFFFFF;
			break;
		}
		for (i = 0; i < len; i++) {
			Memory_WriteB(a1 + i, 0x00);
		}
		result = 0;
		break;
	}

	case 0x3f:  // observed after device-link on some Human68k boot paths
		// Compatibility no-op: returning -1 here can divert the caller into
		// fragile fallback code paths that corrupt the IOCS work stack.
		result = 0;
		break;

	// Low-level SCSI IOCS commands can arrive with two different
	// function codes depending on the call path:
	//   - Via IOCS vector: $A0-$A9
	//   - Via trap #15 $F5 d1.w: $00-$07
	// Map the $00-$07 variants to the same handlers.
	case 0x00:  // _S_RESET (via trap #15 $F5)
	case 0xA0: { // _S_RESET / SCSI IOCS probe
		// SCSI bus reset: set SPC INTS to indicate reset complete.
		s_spc_regs[0x09] = 0x04;
		result = 0;
		// SCSI IOCS probe: the init routine passes d1.w=$81A6 as a
		// signature and expects d1.w=1 back to confirm extension present.
		// Return d0=1 (non-zero) so CC fix sets Z=0; probe code that uses
		// BEQ/BNE after TRAP #15 won't mis-branch on Z=1 from d0=0.
		if ((d1 & 0xFFFF) == 0x81A6) {
			C68k_Set_DReg(&C68K, 1, (d1 & 0xFFFF0000U) | 0x0001U);
			result = 1;
#if MPX68K_ENABLE_RUNTIME_FILE_LOGS
			// DEBUG: dump probe return PC and machine code (bypass log limit)
			{
				static int s_a0_probe_dumps = 0;
				if (s_a0_probe_dumps < 3) {
					s_a0_probe_dumps++;
					DWORD sp = C68k_Get_AReg(&C68K, 7);
					DWORD retPC = Memory_ReadD(sp + 14) & 0x00FFFFFFU;
					DWORD d0val = C68k_Get_DReg(&C68K, 0);
					DWORD d1val = C68k_Get_DReg(&C68K, 1);
					WORD srVal = Memory_ReadW(sp + 12);
					char probePath[512];
					SCSI_GetLogPath(probePath, sizeof(probePath));
					// Change filename to _probe_dump.txt
					char *sl = strrchr(probePath, '/');
					if (sl) strcpy(sl + 1, "_probe_dump.txt");
					FILE *pf = fopen(probePath, "a");
					if (pf) {
						fprintf(pf, "fn=$A0 PROBE #%d: retPC=$%06X d0=$%08X d1=$%08X SR=$%04X\n",
							s_a0_probe_dumps, (unsigned)retPC, (unsigned)d0val, (unsigned)d1val, (unsigned)srVal);
						fprintf(pf, "  code at retPC: ");
						for (int i = 0; i < 48; i++) {
							fprintf(pf, "%02X", (unsigned)Memory_ReadB(retPC + i));
							if (i % 2 == 1) fprintf(pf, " ");
						}
						fprintf(pf, "\n");
						fprintf(pf, "  code before retPC(-32): ");
						for (int i = 32; i > 0; i--) {
							fprintf(pf, "%02X", (unsigned)Memory_ReadB(retPC - i));
							if (i % 2 == 1) fprintf(pf, " ");
						}
						fprintf(pf, "\n");
						fclose(pf);
					}
				}
			}
#endif /* MPX68K_ENABLE_RUNTIME_FILE_LOGS */
		}
		break;
	}

	case 0x01:  // _S_SELECT (via trap #15 $F5)
	case 0xA1:  // _S_SELECT: select SCSI target
	case 0xA2:  // _S_SELECTA: select with ATN
		// d4 = target ID, d5 = initiator ID.
		// Only SCSI ID matching our mounted image responds.
		s_spc_target_id = (BYTE)(d4 & 0x07);
		s_spc_cdb_len = 0;
		s_spc_cmd_valid = 0;
		s_spc_status_byte = 0x00;   // GOOD
		s_spc_message_byte = 0x00;  // Command Complete
		if ((X68000_GetStorageBusMode() == 2 && SCSIU_IsConnected()) ||
		    (s_disk_image_buffer[4] != NULL && s_disk_image_buffer_size[4] > 0)) {
			s_spc_regs[0x09] = 0x01;  // INTS: SEL complete
			result = 0;
		} else {
			result = (DWORD)0xFFFFFFFF;  // no target
		}
		break;

	case 0x02:  // _S_CMDOUT (via trap #15 $F5)
	case 0xA3: {  // _S_CMDOUT: send CDB to target
		// a1 = CDB buffer address, d3 = CDB byte count.
		DWORD cdbAddr = a1 & 0x00FFFFFFU;
		DWORD cdbLen = d3 & 0xFF;
		DWORD ci;
		if (cdbLen > 16) cdbLen = 16;
		for (ci = 0; ci < cdbLen; ci++) {
			s_spc_cdb[ci] = Memory_ReadB(cdbAddr + ci);
		}
		s_spc_cdb_len = cdbLen;
		s_spc_cmd_valid = 1;
		s_spc_status_byte = 0x00;  // assume GOOD
		s_spc_regs[0x09] = 0x04;   // INTS: command complete
		result = 0;
		break;
	}

	case 0x03:  // _S_DATAIN (via trap #15 $F5)
	case 0xA4: {  // _S_DATAIN: read data from target into memory
		// a1 = destination buffer, d3 = byte count.
		// Interpret the CDB stored by _S_CMDOUT and perform immediate
		// memory copy (pSCSI DMA-bypass style).
		DWORD dstAddr = a1 & 0x00FFFFFFU;
		DWORD xferLen = d3;
		result = 0;
		s_spc_status_byte = 0x00;

		if (!s_spc_cmd_valid || s_spc_cdb_len == 0) {
			s_spc_status_byte = 0x02;  // CHECK CONDITION
			result = (DWORD)0xFFFFFFFF;
		} else {
			BYTE scsiCmd = s_spc_cdb[0];
			switch (scsiCmd) {
			case 0x08: {  // READ(6)
				DWORD lba = ((s_spc_cdb[1] & 0x1F) << 16) |
				            (s_spc_cdb[2] << 8) | s_spc_cdb[3];
				DWORD blocks = s_spc_cdb[4];
				if (blocks == 0) blocks = 256;
				DWORD blockSize = 512;
				DWORD byteOff = lba * blockSize;
				DWORD byteLen = blocks * blockSize;
				DWORD ri;
				if (xferLen < byteLen) byteLen = xferLen;
#ifdef __APPLE__
				if (X68000_GetStorageBusMode() == 2) {
					BYTE* tmpR6 = (BYTE*)malloc(byteLen);
					if (tmpR6 && SCSIU_ReadBlocks(lba, byteLen / blockSize, blockSize, tmpR6)) {
						for (ri = 0; ri < byteLen; ri++)
							Memory_WriteB(dstAddr + ri, tmpR6[ri]);
					} else {
						for (ri = 0; ri < byteLen; ri++) Memory_WriteB(dstAddr + ri, 0x00);
					}
					free(tmpR6);
				} else
#endif
				if (s_disk_image_buffer[4] &&
				    byteOff + byteLen <= (DWORD)s_disk_image_buffer_size[4]) {
					for (ri = 0; ri < byteLen; ri++)
						Memory_WriteB(dstAddr + ri, s_disk_image_buffer[4][byteOff + ri]);
				} else {
					for (ri = 0; ri < byteLen; ri++)
						Memory_WriteB(dstAddr + ri, 0x00);
				}
				break;
			}
			case 0x28: {  // READ(10)
				DWORD lba = ((DWORD)s_spc_cdb[2] << 24) |
				            ((DWORD)s_spc_cdb[3] << 16) |
				            ((DWORD)s_spc_cdb[4] << 8) |
				            (DWORD)s_spc_cdb[5];
				DWORD blocks = ((DWORD)s_spc_cdb[7] << 8) | s_spc_cdb[8];
				DWORD blockSize = 512;
				DWORD byteOff = lba * blockSize;
				DWORD byteLen = blocks * blockSize;
				DWORD ri;
				if (xferLen < byteLen) byteLen = xferLen;
#ifdef __APPLE__
				if (X68000_GetStorageBusMode() == 2) {
					BYTE* tmpR10 = (BYTE*)malloc(byteLen);
					if (tmpR10 && blocks > 0 && SCSIU_ReadBlocks(lba, blocks, blockSize, tmpR10)) {
						for (ri = 0; ri < byteLen; ri++)
							Memory_WriteB(dstAddr + ri, tmpR10[ri]);
					} else {
						for (ri = 0; ri < byteLen; ri++) Memory_WriteB(dstAddr + ri, 0x00);
					}
					free(tmpR10);
				} else
#endif
				if (s_disk_image_buffer[4] &&
				    byteOff + byteLen <= (DWORD)s_disk_image_buffer_size[4]) {
					for (ri = 0; ri < byteLen; ri++)
						Memory_WriteB(dstAddr + ri, s_disk_image_buffer[4][byteOff + ri]);
				} else {
					for (ri = 0; ri < byteLen; ri++)
						Memory_WriteB(dstAddr + ri, 0x00);
				}
				break;
			}
			case 0x12: {  // INQUIRY
				BYTE inq[36];
				DWORD ri;
				memset(inq, 0, sizeof(inq));
				inq[0] = 0x00;  // Direct access device
				inq[1] = 0x00;  // Not removable
				inq[2] = 0x02;  // SCSI-2
				inq[3] = 0x02;  // Response data format
				inq[4] = 31;    // Additional length
				memcpy(&inq[8],  "SHARP   ", 8);   // Vendor
				memcpy(&inq[16], "MPX68K HDD      ", 16); // Product
				memcpy(&inq[32], "1.0 ", 4);        // Revision
				DWORD copyLen = (xferLen < 36) ? xferLen : 36;
				for (ri = 0; ri < copyLen; ri++)
					Memory_WriteB(dstAddr + ri, inq[ri]);
				break;
			}
			case 0x25: {  // READ CAPACITY
				DWORD totalBlocks = 0;
				DWORD capBlockSize = 512;
				BYTE cap[8];
#ifdef __APPLE__
				if (X68000_GetStorageBusMode() == 2) {
					DWORD lastLBA = 0;
					SCSIU_ReadCapacity(&lastLBA, &capBlockSize);
					totalBlocks = lastLBA; /* already 0-based last LBA */
				} else
#endif
				if (s_disk_image_buffer[4]) {
					totalBlocks = (DWORD)(s_disk_image_buffer_size[4] / 512);
					if (totalBlocks > 0) totalBlocks--;
				}
				cap[0] = (totalBlocks >> 24) & 0xFF;
				cap[1] = (totalBlocks >> 16) & 0xFF;
				cap[2] = (totalBlocks >> 8) & 0xFF;
				cap[3] = totalBlocks & 0xFF;
				cap[4] = (capBlockSize >> 24) & 0xFF;
				cap[5] = (capBlockSize >> 16) & 0xFF;
				cap[6] = (capBlockSize >>  8) & 0xFF;
				cap[7] = capBlockSize & 0xFF;
				{
					DWORD copyLen = (xferLen < 8) ? xferLen : 8;
					DWORD ri;
					for (ri = 0; ri < copyLen; ri++)
						Memory_WriteB(dstAddr + ri, cap[ri]);
				}
				break;
			}
			case 0x03: {  // REQUEST SENSE
				BYTE sense[18];
				DWORD ri;
				memset(sense, 0, sizeof(sense));
				sense[0] = 0x70;  // Current error, fixed format
				sense[7] = 10;    // Additional sense length
				// No error (sense key = 0)
				{
					DWORD copyLen = (xferLen < 18) ? xferLen : 18;
					for (ri = 0; ri < copyLen; ri++)
						Memory_WriteB(dstAddr + ri, sense[ri]);
				}
				break;
			}
			case 0x1A: {  // MODE SENSE(6)
				BYTE mode[4];
				DWORD ri;
				memset(mode, 0, sizeof(mode));
				mode[0] = 3;  // Mode data length
				{
					DWORD copyLen = (xferLen < 4) ? xferLen : 4;
					for (ri = 0; ri < copyLen; ri++)
						Memory_WriteB(dstAddr + ri, mode[ri]);
				}
				break;
			}
			default:
				// Unknown command: return empty data
				{
					DWORD ri;
					DWORD fillLen = (xferLen < 256) ? xferLen : 256;
					for (ri = 0; ri < fillLen; ri++)
						Memory_WriteB(dstAddr + ri, 0x00);
				}
				break;
			}
		}
		s_spc_regs[0x09] = 0x04;  // INTS: transfer complete
		break;
	}

	case 0x04:  // _S_DATAOUT (via trap #15 $F5)
	case 0xA5: {  // _S_DATAOUT: write data from memory to target
		DWORD srcAddr = a1 & 0x00FFFFFFU;
		DWORD xferLen = d3;
		result = 0;
		s_spc_status_byte = 0x00;

		if (s_spc_cmd_valid && s_spc_cdb_len > 0) {
			BYTE scsiCmd = s_spc_cdb[0];
			if (scsiCmd == 0x0A || scsiCmd == 0x2A) {
				// WRITE(6) or WRITE(10)
				DWORD lba, blocks, blockSize = 512;
				DWORD byteOff, byteLen;
				if (scsiCmd == 0x0A) {
					lba = ((s_spc_cdb[1] & 0x1F) << 16) |
					      (s_spc_cdb[2] << 8) | s_spc_cdb[3];
					blocks = s_spc_cdb[4];
					if (blocks == 0) blocks = 256;
				} else {
					lba = ((DWORD)s_spc_cdb[2] << 24) |
					      ((DWORD)s_spc_cdb[3] << 16) |
					      ((DWORD)s_spc_cdb[4] << 8) |
					      (DWORD)s_spc_cdb[5];
					blocks = ((DWORD)s_spc_cdb[7] << 8) | s_spc_cdb[8];
				}
				byteOff = lba * blockSize;
				byteLen = blocks * blockSize;
				if (xferLen < byteLen) byteLen = xferLen;
#ifdef __APPLE__
				if (X68000_GetStorageBusMode() == 2) {
					BYTE* tmpW = NULL;
					DWORD writeBlocks = 0;
					if (byteLen == 0 || (byteLen % blockSize) != 0) {
						result = (DWORD)0xFFFFFFFF;
						s_spc_status_byte = 0x02;
						break;
					}
					writeBlocks = byteLen / blockSize;
					tmpW = (BYTE*)malloc(byteLen);
					if (tmpW == NULL) {
						result = (DWORD)0xFFFFFFFF;
						s_spc_status_byte = 0x02;
						break;
					}
					{
						DWORD wi;
						for (wi = 0; wi < byteLen; wi++) {
							tmpW[wi] = Memory_ReadB(srcAddr + wi);
						}
					}
					if (!SCSIU_WriteBlocks(lba, writeBlocks, blockSize, tmpW)) {
						free(tmpW);
						result = (DWORD)0xFFFFFFFF;
						s_spc_status_byte = 0x02;
						break;
					}
					SCSIU_InvalidateBootCacheRange(lba, writeBlocks, blockSize);
					free(tmpW);
				} else
#endif
				if (s_disk_image_buffer[4] &&
				    byteOff + byteLen <= (DWORD)s_disk_image_buffer_size[4]) {
					DWORD wi;
					for (wi = 0; wi < byteLen; wi++)
						s_disk_image_buffer[4][byteOff + wi] = Memory_ReadB(srcAddr + wi);
				}
			}
			// MODE SELECT ($15/$55): accept and ignore
		}
		s_spc_regs[0x09] = 0x04;  // INTS: transfer complete
		break;
	}

	case 0x05:  // _S_STSIN (via trap #15 $F5)
	case 0xA6:  // _S_STSIN: receive status byte from target
		result = s_spc_status_byte;  // 0x00 = GOOD
		s_spc_regs[0x09] = 0x04;
		break;

	case 0x06:  // _S_MSGIN (via trap #15 $F5)
	case 0xA7:  // _S_MSGIN: receive message byte from target
		result = s_spc_message_byte;  // 0x00 = Command Complete
		s_spc_regs[0x09] = 0x04;
		break;

	case 0xA8:  // _S_MSGOUT: send message byte to target
		// Accept and ignore
		s_spc_regs[0x09] = 0x04;
		result = 0;
		break;

	case 0x07:  // _S_PHASE (via trap #15 $F5)
	case 0xA9:  // _S_PHASE: get current bus phase
		// Return 0 = bus free
		result = 0;
		break;

	case 0x08: {  // _S_LEVEL: get SCSI interrupt/ready level
		// pSCSI ref: Human68k Disk Driver v1.04 enters an infinite loop
		// when S_LEVEL < 4, treating it as "hardware in invalid state".
		// Return 4 to indicate "SCSI controller ready, no pending IRQ".
		result = 4;
		break;
	}

	case 0x0a:  // _S_DATAINI: initiate data-in (DMA read from target)
	case 0x0b:  // _S_DATAOUTI: initiate data-out (DMA write to target)
		// pSCSI ref: these transfer-initiation commands are needed for
		// drivers that use the low-level SCSI path.  In our trap-based
		// model the actual transfer is handled by _S_READ/_S_READEXT,
		// so these are no-ops that return success.
		s_spc_regs[0x09] = 0x04;  // INTS: transfer complete
		result = 0;
		break;

	case 0x28:  // _S_VERIFYEXT: verify sectors (no data transfer)
		// Pretend verification succeeded.
		result = 0;
		break;

	default:
		// Return 0 (success) for unhandled functions.  Returning -1 causes
		// callers to retry in a loop.  SCSI functions that legitimately
		// need -1 are handled as specific cases above.
		result = 0;
		snprintf(logLine, sizeof(logLine),
		         "SCSI_IOCS_UNHANDLED cmd=$%02X d2=%08X d3=%08X d4=%08X d5=%08X a1=%08X",
		         cmd,
		         (unsigned int)d2, (unsigned int)d3, (unsigned int)d4,
		         (unsigned int)d5, (unsigned int)a1);
		SCSI_LogText(logLine);
		break;
	}

	C68k_Set_DReg(&C68K, 0, result);

	snprintf(logLine, sizeof(logLine),
	         "SCSI_IOCS cmd=$%02X d2=%08X d3=%08X d4=%08X d5=%08X a1=%08X d0=%08X",
	         cmd, (unsigned int)d2, (unsigned int)d3, (unsigned int)d4,
	         (unsigned int)d5, (unsigned int)a1, (unsigned int)result);
	SCSI_LogText(logLine);
#endif
}


// -----------------------------------------------------------------------
//   I/O Read
//   - $EA0000-$EA1FFF: SCSI ROM (SCSIIPL) を返す
//   - $E9E000-$E9FFFF: SPC レジスタ範囲 → 安全なダミー値を返す
// -----------------------------------------------------------------------
BYTE FASTCALL SCSI_Read(DWORD adr)
{
	// SCSI ROM 読み取り: 外付けSCSI ROM ($EA0000-$EA1FFF) のみ
	// $FC0000-$FDFFFF はIPL ROMミラーとして維持（オーバーレイしない）
	if (adr >= 0x00ea0000 && adr < 0x00ea2000) {
		if (adr >= 0x00ea0100 && adr <= 0x00ea0103) {
			return s_scsi_devhdr_next[adr - 0x00ea0100];
		}
		return SCSIIPL[(adr ^ 1) & 0x1fff];
	}

	// SPC (MB89352) register window at $E96000.
	// Human68k's SCSI driver writes SCTL/BDID and reads back to verify the
	// chip exists.  Return the last written value for config registers so the
	// write-readback check passes.  For status registers return appropriate
	// fixed values.
	if (adr >= 0x00e96000 && adr < 0x00e98000) {
		BYTE offset = adr & 0x1f;
		BYTE result;
		switch (offset) {
		case 0x01:  // BDID - return last written value
		case 0x03:  // SCTL - return last written value
		case 0x05:  // SCMD - return last written value
			result = s_spc_regs[offset];
			break;
		case 0x09:  // INTS - interrupt sense
			result = s_spc_regs[0x09];
			break;
		case 0x0b:  // PSNS - phase sense (bus free)
			result = 0x00;
			break;
		case 0x0d:  // SSTS - SPC status (TC0 | DREG_EMPTY)
			result = 0x05;
			break;
		case 0x0f:  // SERR - no error
			result = 0x00;
			break;
		default:
			result = s_spc_regs[offset];
			break;
		}
		SCSI_LogSPCAccess("READ960", adr, result);
		return result;
	}

	// SPC レジスタ範囲 ($E9E000-$E9FFFF)
	// SPCレジスタはエミュレートしないため、安全なダミー値を返す
	// Alias: some SCSI ROM variants access SPC at $E9E000 range.
	// Use the same s_spc_regs[] state as the $E96000 window.
	if (adr >= 0x00e9e000 && adr < 0x00ea0000) {
		BYTE offset = adr & 0x1f;
		BYTE result;
		switch (offset) {
		case 0x01:  // BDID
		case 0x03:  // SCTL
		case 0x05:  // SCMD
			result = s_spc_regs[offset];
			break;
		case 0x09:  // INTS
			result = s_spc_regs[0x09];
			break;
		case 0x0b:  // PSNS
			result = 0x00;
			break;
		case 0x0d:  // SSTS
			result = 0x05;
			break;
		case 0x0f:  // SERR
			result = 0x00;
			break;
		default:
			result = s_spc_regs[offset];
			break;
		}
		SCSI_LogSPCAccess("READ", adr, result);
		return result;
	}

	return 0xff;
}


// -----------------------------------------------------------------------
//   I/O Write
//   - $E9F800: SCSI トラップアドレス (ブート/IOCS)
//   - $E9E000-$E9FFFF: SPC レジスタ範囲 → 安全に無視
// -----------------------------------------------------------------------
void FASTCALL SCSI_Write(DWORD adr, BYTE data)
{
	if (adr >= 0x00ea0100 && adr <= 0x00ea0103) {
		s_scsi_devhdr_next[adr - 0x00ea0100] = data;
		return;
	}

	// SCSIトラップ:
	//  - 合成ROM経路: $E9F800
	//  - 実ROM互換経路: $E96020
	if (adr == 0x00e9f800 || adr == 0x00e96020) {
#if defined(HAVE_C68K)
		if (data != 0xff) {
			DWORD d2 = C68k_Get_DReg(&C68K, 2);
			DWORD d3 = C68k_Get_DReg(&C68K, 3);
			DWORD d4 = C68k_Get_DReg(&C68K, 4);
			DWORD d5 = C68k_Get_DReg(&C68K, 5);
			DWORD a1 = C68k_Get_AReg(&C68K, 1);
			if (s_last_iocs_sig_valid &&
			    s_last_iocs_sig_cmd == data &&
			    s_last_iocs_sig_d2 == d2 &&
			    s_last_iocs_sig_d3 == d3 &&
			    s_last_iocs_sig_d4 == d4 &&
			    s_last_iocs_sig_d5 == d5 &&
			    s_last_iocs_sig_a1 == a1 &&
			    s_last_iocs_sig_addr != adr) {
				char dupLog[128];
				snprintf(dupLog, sizeof(dupLog),
				         "SCSI_TRAP_DUP suppressed cmd=$%02X adr=$%06X prev=$%06X",
				         (unsigned int)data,
				         (unsigned int)adr,
				         (unsigned int)s_last_iocs_sig_addr);
				SCSI_LogText(dupLog);
				return;
			}
			s_last_iocs_sig_valid = 1;
			s_last_iocs_sig_addr = adr;
			s_last_iocs_sig_cmd = data;
			s_last_iocs_sig_d2 = d2;
			s_last_iocs_sig_d3 = d3;
			s_last_iocs_sig_d4 = d4;
			s_last_iocs_sig_d5 = d5;
			s_last_iocs_sig_a1 = a1;
		} else {
			s_last_iocs_sig_valid = 0;
		}
#endif
		SCSI_LogIO(adr, "TRAP", data);
		if (data == 0xff) {
			s_scsi_boot_activity = 1;
			// cmd=$FF triggers SCSI boot. Guard against repeated invocation
			// within the same session (stray $FF after boot completes).
			if (s_scsi_boot_done) {
				SCSI_LogText("SCSI_BOOT_CMD_IGNORED already_booted");
				return;
			}
			s_scsi_boot_done = 1;
			SCSI_HandleBoot();
		} else {
			s_scsi_boot_activity = 1;
			SCSI_HandleIOCS(data);
#if defined(HAVE_C68K)
			// ── TRAP #15 条件コード修正 ──
			// TRAP #15 ディスパッチャ ($EA0088) は RTE で復帰するため、
			// IOCS ハンドラが設定した d0 の結果が条件コードに反映されない。
			// 呼び出し元が TRAP #15 後に tst.l d0 なしで bmi/beq 等を使う場合、
			// 古い CC で分岐してしまう。
			// 対策: TRAP コンテキスト (JSR 戻りアドレス=$EA00B4) なら
			// 例外フレームの保存 SR の CC ビットを d0 に基づいて更新する。
			//
			// スタックレイアウト (ハンドラ $EA00F2 内):
			//   SP+0:  JSR 戻りアドレス ($EA00B4)
			//   SP+4:  保存 a0
			//   SP+8:  保存 d0
			//   SP+12: 例外 SR (2 bytes)
			//   SP+14: 例外 PC (4 bytes)
			{
				DWORD sp = C68k_Get_AReg(&C68K, 7);
				DWORD retAddr = Memory_ReadD(sp) & 0x00FFFFFFU;
				if (retAddr == 0x00EA00B4U) {
					DWORD d0 = C68k_Get_DReg(&C68K, 0);
					WORD savedSR = Memory_ReadW(sp + 12);
					// CC ビットをクリア (N=3, Z=2, V=1, C=0)
					savedSR &= ~0x000FU;
					// d0 == 0 → Z=1 (成功)
					if (d0 == 0) savedSR |= 0x0004U;
					// d0 < 0 (bit31) → N=1 (エラー)
					if (d0 & 0x80000000U) savedSR |= 0x0008U;
					Memory_WriteW(sp + 12, savedSR);
				}
			}
#endif
		}
		return;
	}


	// デバイスドライバトラップ: $E9F802
	//  data=$02 → strategy (A5をC側で退避)
	//  data=$01 → interrupt (コマンド処理)
	//
	// Safety: ignore this trap unless the synthetic driver is actually linked
	// into the Human68k device chain. This prevents stray writes from driving
	// request-packet handling with invalid pointers.
	if (adr == 0x00e9f802) {
		if (!s_scsi_dev_linked) {
			return;
		}
#if defined(HAVE_C68K)
		if (data == 0x02) {
			s_scsi_driver_activity = 1;
			s_scsi_dev_reqpkt = SCSI_Mask24(C68k_Get_AReg(&C68K, 5));
			// Log caller + dump kernel code once
			{
				static int s_devtrap_log_count = 0;
				static int s_kernel_dumped = 0;
				if (s_devtrap_log_count < 60) {
					DWORD sp = C68k_Get_AReg(&C68K, 7) & 0x00FFFFFFU;
					DWORD ret1 = Memory_ReadD(sp) & 0x00FFFFFFU;
					DWORD ret2 = Memory_ReadD(sp + 4) & 0x00FFFFFFU;
					DWORD sr = C68k_Get_SR(&C68K);
					// Deep stack: SP+8..+33=pkt(26), +34/+38/+42=saved regs, +46=caller
					DWORD deep_caller = Memory_ReadD(sp + 46) & 0x00FFFFFFU;
					DWORD deep_caller2 = Memory_ReadD(sp + 50) & 0x00FFFFFFU;
					// Also dump SP+34..+50 raw to verify layout
					char dbg[256];
					snprintf(dbg, sizeof(dbg),
					         "DEVTRAP_CALLER a5=$%08X sp=$%06X ret1=$%06X ret2=$%06X caller=$%06X caller2=$%06X stk34=%08X stk38=%08X stk42=%08X sr=$%04X",
					         (unsigned int)s_scsi_dev_reqpkt,
					         (unsigned int)sp,
					         (unsigned int)ret1,
					         (unsigned int)ret2,
					         (unsigned int)deep_caller,
					         (unsigned int)deep_caller2,
					         (unsigned int)(Memory_ReadD(sp + 34) & 0x00FFFFFFU),
					         (unsigned int)(Memory_ReadD(sp + 38) & 0x00FFFFFFU),
					         (unsigned int)(Memory_ReadD(sp + 42) & 0x00FFFFFFU),
					         (unsigned int)(sr & 0xFFFF));
					SCSI_LogText(dbg);
					s_devtrap_log_count++;
				}
				// One-time kernel code dump around caller addresses
				if (!s_kernel_dumped && s_devtrap_log_count >= 5) {
					s_kernel_dumped = 1;
					// Dump multiple kernel areas
					struct { DWORD start; DWORD end; const char *tag; } areas[] = {
						{ 0x00D400, 0x00D4F0, "KERN_LOOP" },    // Loop function at $D432/$D436
						{ 0x00DEF0, 0x00DF50, "KERN_WRAP" },    // Wrapper at $DEFA → dispatcher
						{ 0x00A9D0, 0x00AA20, "KERN_A9D8" },    // Loop exit condition function
						{ 0x00C2C0, 0x00C420, "KERN_OUTER" },   // Outer caller + continuation
						{ 0x00C3F0, 0x00C430, "KERN_CONT" },    // Continuation at $C3FA
					};
					int a;
					for (a = 0; a < 5; a++) {
						DWORD base;
						for (base = areas[a].start; base < areas[a].end; base += 16) {
							char line[200];
							snprintf(line, sizeof(line),
							         "%s $%06X: %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X",
							         areas[a].tag,
							         (unsigned int)base,
							         (unsigned int)Memory_ReadB(base+0),  (unsigned int)Memory_ReadB(base+1),
							         (unsigned int)Memory_ReadB(base+2),  (unsigned int)Memory_ReadB(base+3),
							         (unsigned int)Memory_ReadB(base+4),  (unsigned int)Memory_ReadB(base+5),
							         (unsigned int)Memory_ReadB(base+6),  (unsigned int)Memory_ReadB(base+7),
							         (unsigned int)Memory_ReadB(base+8),  (unsigned int)Memory_ReadB(base+9),
							         (unsigned int)Memory_ReadB(base+10), (unsigned int)Memory_ReadB(base+11),
							         (unsigned int)Memory_ReadB(base+12), (unsigned int)Memory_ReadB(base+13),
							         (unsigned int)Memory_ReadB(base+14), (unsigned int)Memory_ReadB(base+15));
							SCSI_LogText(line);
						}
					}
				}
			}
		} else if (data == 0x01) {
			s_scsi_driver_activity = 1;
			SCSI_HandleDeviceCommand();
		}
#endif
		return;
	}

	// SPC register write at $E96000 page.
	// Store the value so readback checks pass, and handle commands.
	if (adr >= 0x00e96000 && adr < 0x00e98000) {
		BYTE offset = adr & 0x1f;
		SCSI_LogSPCAccess("WRITE960", adr, data);
		s_spc_regs[offset] = data;
		if (offset == 0x03) {  // SCTL - SPC control register
			if (data & 0x80) {
				// RD (Reset and Disable) bit: perform SPC software reset.
				// Real MB89352 clears the RD bit automatically once the
				// reset completes.  Drivers poll SCTL waiting for bit7=0;
				// returning the written value unchanged traps them in an
				// infinite loop.  Clear it immediately (instantaneous reset).
				s_spc_regs[0x03] = data & 0x7F;
				s_spc_regs[0x09] = 0x04;  // INTS: reset complete
				s_spc_regs[0x0b] = 0x00;  // PSNS: bus free
				s_spc_regs[0x0d] = 0x05;  // SSTS: TC0 | DREG_EMPTY
				s_spc_regs[0x0f] = 0x00;  // SERR: no error
			}
		} else if (offset == 0x05) {  // SCMD - command register
			BYTE cmd = data & 0xe0;
			if (cmd == 0x20) {
				// SELECT command: pretend target responded immediately
				s_spc_regs[0x09] = 0x01;  // INTS: SEL/RESEL complete
			} else if (cmd == 0x40) {
				// RESET command: clear INTS, signal reset done
				s_spc_regs[0x09] = 0x04;  // INTS: command complete
			} else if (cmd == 0x00) {
				// BUS RELEASE
				s_spc_regs[0x09] = 0x04;  // INTS: command complete
			}
		} else if (offset == 0x09) {
			// Writing to INTS clears the bits (write-1-to-clear)
			s_spc_regs[0x09] &= ~data;
		}
		return;
	}

	// Alias: $E9E000 range SPC writes - same state as $E96000.
	if (adr >= 0x00e9e000 && adr < 0x00ea0000) {
		BYTE offset = adr & 0x1f;
		SCSI_LogSPCAccess("WRITE", adr, data);
		s_spc_regs[offset] = data;
		if (offset == 0x03) {
			if (data & 0x80) {
				s_spc_regs[0x03] = data & 0x7F;
				s_spc_regs[0x09] = 0x04;
				s_spc_regs[0x0b] = 0x00;
				s_spc_regs[0x0d] = 0x05;
				s_spc_regs[0x0f] = 0x00;
			}
		} else if (offset == 0x05) {
			BYTE cmd = data & 0xe0;
			if (cmd == 0x20) {
				s_spc_regs[0x09] = 0x01;
			} else if (cmd == 0x40) {
				s_spc_regs[0x09] = 0x04;
			} else if (cmd == 0x00) {
				s_spc_regs[0x09] = 0x04;
			}
		} else if (offset == 0x09) {
			s_spc_regs[0x09] &= ~data;
		}
		return;
	}
}


// -----------------------------------------------------------------------
//   ブートセクタ上の BPB から論理セクタサイズを推定
// -----------------------------------------------------------------------
static DWORD SCSI_DetectBootBlockSize(DWORD bootOffset)
{
	static const DWORD bpbOffsets[] = { 0x12, 0x11, 0x0E, 0x0B };
	BYTE* buf;
	DWORD size;
	DWORD i;

	if (SCSI_ImgBuf() == NULL || SCSI_ImgSize() <= 0) {
		return 0;
	}
	buf = SCSI_ImgBuf();
	size = (DWORD)SCSI_ImgSize();
	for (i = 0; i < (DWORD)(sizeof(bpbOffsets) / sizeof(bpbOffsets[0])); i++) {
		DWORD pos = bootOffset + bpbOffsets[i];
		WORD bytesPerSec;
		if (pos + 2 > size) {
			continue;
		}
		bytesPerSec = ((WORD)buf[pos] << 8) | (WORD)buf[pos + 1];
		if (bytesPerSec == 256 || bytesPerSec == 512 || bytesPerSec == 1024) {
			return (DWORD)bytesPerSec;
		}
	}
	return 0;
}


// -----------------------------------------------------------------------
//   パーティションブートセクタのバイトオフセットを取得
//   X68SCSI1 ヘッダ → パーティションテーブル → ブートセクタ位置
// -----------------------------------------------------------------------
static DWORD SCSI_FindPartitionBootOffset(void)
{
	BYTE* buf;
	long size;
	DWORD physSectorSize;
	DWORD partTableOffset;
	DWORD i;

	if (SCSI_ImgBuf() == NULL || SCSI_ImgSize() < 0x1000) {
		return 0;
	}
	buf = SCSI_ImgBuf();
	size = SCSI_ImgSize();

	// IOCS/boot pathと同じ判定を使い、raw/X68SCSI1で一貫させる
	physSectorSize = SCSI_GetImageBlockSize();
	if (physSectorSize != 256 && physSectorSize != 512 && physSectorSize != 1024) {
		physSectorSize = 512;
	}

	// パーティションテーブルは LBA 4 (physical sectors)
	partTableOffset = physSectorSize * 4;
	if (partTableOffset + 64 > (DWORD)size) {
		return 0;
	}

	// "X68K" シグネチャ確認
	if (memcmp(buf + partTableOffset, "X68K", 4) != 0) {
		return 0;
	}

	// パーティションエントリを検索 (テーブル内で "Human68k" を探す)
	for (i = partTableOffset + 4; i < partTableOffset + physSectorSize && i + 16 <= (DWORD)size; i += 2) {
		if (buf[i] == 'H' && memcmp(buf + i, "Human68k", 8) == 0) {
			// エントリ構造: type(8) + start(4,BE) + size(4,BE)
			DWORD startSec = ((DWORD)buf[i + 8] << 24) |
			                 ((DWORD)buf[i + 9] << 16) |
			                 ((DWORD)buf[i + 10] << 8) |
			                 ((DWORD)buf[i + 11]);
			// startSec は 1024バイト論理セクタ単位
			DWORD byteOffset = startSec * 1024;
			if (byteOffset < (DWORD)size) {
				char log[96];
				snprintf(log, sizeof(log),
				         "SCSI_DEV partBoot=0x%X startSec=%u",
				         (unsigned int)byteOffset, (unsigned int)startSec);
				SCSI_LogText(log);
				return byteOffset;
			}
		}
	}

	// フォールバック: LBA 64 (512バイトセクタ) = offset 0x8000
	if (0x8000 < (DWORD)size) {
		SCSI_LogText("SCSI_DEV partBoot fallback 0x8000");
		return 0x8000;
	}
	return 0;
}


// -----------------------------------------------------------------------
//   ディスクイメージからBPBを読み取る
//   outBpb: 36バイトのバッファ
//   outPartOffset: パーティション先頭のバイトオフセット
//   戻り値: 成功=1, 失敗=0
// -----------------------------------------------------------------------
static int SCSI_ReadBPBFromImage(BYTE* outBpb, DWORD* outPartOffset)
{
	BYTE* buf;
	long size;
	DWORD bootOffset;
	DWORD bpbOff = 0;
	BYTE rawBpb[36];
	WORD bytesPerSec = 0;

	if (SCSI_ImgBuf() == NULL || SCSI_ImgSize() <= 0) {
		return 0;
	}
	buf = SCSI_ImgBuf();
	size = SCSI_ImgSize();

	bootOffset = SCSI_FindPartitionBootOffset();
	if (bootOffset == 0 || bootOffset + 0x30 > (DWORD)size) {
		return 0;
	}

	// ブートセクタ先頭は BRA.W (0x60xx) であるべき
	if ((buf[bootOffset] & 0xF0) != 0x60) {
		SCSI_LogText("SCSI_DEV bootSec not BRA");
		return 0;
	}

	// Human68k BPB はブートセクタ内の以下のオフセットに存在しうる:
	//   $12: BRA.S (2B) + OEM 16B の場合
	//   $11: BRA.S (2B) + OEM 15B の場合
	//   $0E: BRA.W (4B) + OEM 10B の場合
	//   $0B: DOS互換 (JMP 3B + OEM 8B)
	{
		static const DWORD bpbOffsets[] = { 0x12, 0x11, 0x0E, 0x0B };
		int found = 0;
		int k;
		for (k = 0; k < 4; k++) {
			bpbOff = bootOffset + bpbOffsets[k];
			if (bpbOff + 36 > (DWORD)size) continue;
			bytesPerSec = ((WORD)buf[bpbOff] << 8) | (WORD)buf[bpbOff + 1];
			if (bytesPerSec == 256 || bytesPerSec == 512 || bytesPerSec == 1024) {
				found = 1;
				break;
			}
		}
		if (!found) {
			SCSI_LogText("SCSI_DEV BPB not found in bootSec");
			return 0;
		}
	}

	if (bpbOff + 36 > (DWORD)size) {
		return 0;
	}

	memcpy(rawBpb, buf + bpbOff, sizeof(rawBpb));
	memcpy(outBpb, rawBpb, sizeof(rawBpb));
	*outPartOffset = bootOffset;

	{
		char log[128];
		char raw[192];
		snprintf(raw, sizeof(raw),
		         "SCSI_DEV BPB raw off=0x%X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
		         (unsigned int)bpbOff,
		         (unsigned int)rawBpb[0], (unsigned int)rawBpb[1], (unsigned int)rawBpb[2], (unsigned int)rawBpb[3],
		         (unsigned int)rawBpb[4], (unsigned int)rawBpb[5], (unsigned int)rawBpb[6], (unsigned int)rawBpb[7],
		         (unsigned int)rawBpb[8], (unsigned int)rawBpb[9], (unsigned int)rawBpb[10], (unsigned int)rawBpb[11],
		         (unsigned int)rawBpb[12], (unsigned int)rawBpb[13], (unsigned int)rawBpb[14], (unsigned int)rawBpb[15],
		         (unsigned int)rawBpb[16], (unsigned int)rawBpb[17], (unsigned int)rawBpb[18], (unsigned int)rawBpb[19]);
		SCSI_LogText(raw);
		snprintf(raw, sizeof(raw),
		         "SCSI_DEV BPB drv: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
		         (unsigned int)outBpb[0], (unsigned int)outBpb[1], (unsigned int)outBpb[2], (unsigned int)outBpb[3],
		         (unsigned int)outBpb[4], (unsigned int)outBpb[5], (unsigned int)outBpb[6], (unsigned int)outBpb[7],
		         (unsigned int)outBpb[8], (unsigned int)outBpb[9], (unsigned int)outBpb[10], (unsigned int)outBpb[11],
		         (unsigned int)outBpb[12], (unsigned int)outBpb[13], (unsigned int)outBpb[14], (unsigned int)outBpb[15],
		         (unsigned int)outBpb[16], (unsigned int)outBpb[17], (unsigned int)outBpb[18], (unsigned int)outBpb[19]);
		SCSI_LogText(raw);
		snprintf(log, sizeof(log),
		         "SCSI_DEV BPB secSize=%u secClus=%u b3=%02X b4=%02X b5=%02X rootEnt=%u media=$%02X b11=%02X",
		         (unsigned int)bytesPerSec,
		         (unsigned int)outBpb[2],
		         (unsigned int)outBpb[3],
		         (unsigned int)outBpb[4],
		         (unsigned int)outBpb[5],
		         (unsigned int)(((DWORD)outBpb[6] << 8) | outBpb[7]),
		         (unsigned int)outBpb[10],
		         (unsigned int)outBpb[11]);
		SCSI_LogText(log);
	}
	return 1;
}


// -----------------------------------------------------------------------
//   デバイスドライバコマンドハンドラ
//   s_scsi_dev_reqpkt に保存されたリクエストパケットを処理
//
//   リクエストパケット構造:
//     +$00: length (byte)
//     +$01: unit (byte)
//     +$02: command (byte)
//     +$03: status (word, BE) - ドライバが返す
//     +$0D: media / units (byte)
//     +$0E: transfer address / break address (long, BE)
//     +$12: count / BPB pointer (long, BE)
//     +$16: start sector (long, BE)
// -----------------------------------------------------------------------
static void SCSI_HandleDeviceCommand(void)
{
#if defined(HAVE_C68K)
	DWORD reqpkt = s_scsi_dev_reqpkt;
	BYTE pktLen;
	BYTE cmd;
	char logLine[192];

	reqpkt = SCSI_Mask24(reqpkt);
	if (reqpkt == 0) {
		return;
	}
	if (!SCSI_IsLinearRamRange(reqpkt, 6)) {
		SCSI_LogText("SCSI_DEV reqpkt range error");
		s_scsi_dev_reqpkt = 0;
		return;
	}
	pktLen = Memory_ReadB(reqpkt + 0);
	if (pktLen < 6) {
		pktLen = 6;
	}
	if (!SCSI_IsLinearRamRange(reqpkt, (DWORD)pktLen)) {
		SCSI_LogText("SCSI_DEV reqpkt len range error");
		s_scsi_dev_reqpkt = 0;
		return;
	}
	// A stale packet pointer can cause repeated processing if interrupt
	// is invoked without a preceding strategy call.
	s_scsi_dev_reqpkt = 0;

	cmd = Memory_ReadB(reqpkt + 2);

	snprintf(logLine, sizeof(logLine),
	         "SCSI_DEV cmd=%u reqpkt=$%08X len=%u unit=%u",
	         (unsigned int)cmd, (unsigned int)reqpkt,
	         (unsigned int)pktLen,
	         (unsigned int)Memory_ReadB(reqpkt + 1));
	SCSI_LogText(logLine);
	if (cmd == 0x57) {
		SCSI_LogDevicePacket(reqpkt, pktLen);
	}

	switch (cmd) {
	case 0: {  // INIT
		BYTE bpbData[36];
		DWORD partOffset = 0;
		DWORD breakAddrIn;
		DWORD workBase;
		DWORD breakAddrOut;
		DWORD i;
		WORD secSize;
		SCSI_LogDevicePacket(reqpkt, pktLen);

		breakAddrIn = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x11));
		breakAddrIn = SCSI_Mask24(breakAddrIn);

		if (SCSI_ReadBPBFromImage(bpbData, &partOffset)) {
			BYTE fatCount;
			WORD reservedSec;
			WORD rootEnt;
			BYTE secPerFat;
			DWORD rootDirBytes;
			WORD totalSec16;
			DWORD totalSec;
			secSize = ((WORD)bpbData[0] << 8) | (WORD)bpbData[1];
			s_scsi_partition_byte_offset = partOffset;
			s_scsi_sector_size = (secSize > 0) ? secSize : 1024;
			s_scsi_dev_absolute_sectors = -1;
			fatCount = bpbData[3];
			reservedSec = ((WORD)bpbData[4] << 8) | (WORD)bpbData[5];
			rootEnt = ((WORD)bpbData[6] << 8) | (WORD)bpbData[7];
			totalSec16 = ((WORD)bpbData[8] << 8) | (WORD)bpbData[9];
			totalSec = (DWORD)totalSec16;
			if (totalSec == 0) {
				totalSec = ((DWORD)bpbData[12] << 24) |
				           ((DWORD)bpbData[13] << 16) |
				           ((DWORD)bpbData[14] << 8) |
				           (DWORD)bpbData[15];
			}
			if (totalSec == 0 && s_disk_image_buffer_size[4] > 0 && secSize > 0) {
				unsigned long long partBytes =
				    (unsigned long long)s_disk_image_buffer_size[4] -
				    (unsigned long long)partOffset;
				unsigned long long partSectors = partBytes / (unsigned long long)secSize;
				if (partSectors == 0) partSectors = 1;
				if (partSectors > 0xFFFFFFFFULL) partSectors = 0xFFFFFFFFULL;
				totalSec = (DWORD)partSectors;
				bpbData[12] = (BYTE)((totalSec >> 24) & 0xff);
				bpbData[13] = (BYTE)((totalSec >> 16) & 0xff);
				bpbData[14] = (BYTE)((totalSec >> 8) & 0xff);
				bpbData[15] = (BYTE)(totalSec & 0xff);
			}
			secPerFat = bpbData[11];
			rootDirBytes = (DWORD)rootEnt * 32U;
			s_scsi_total_sectors = totalSec;
			s_scsi_fat_start_sector = (DWORD)reservedSec;
			s_scsi_fat_sectors = (DWORD)secPerFat;
			s_scsi_fat_count = (DWORD)fatCount;
			s_scsi_root_dir_start_sector =
			    (DWORD)reservedSec + ((DWORD)fatCount * (DWORD)secPerFat);
			s_scsi_root_dir_sector_count =
			    (s_scsi_sector_size != 0) ? ((rootDirBytes + s_scsi_sector_size - 1) / s_scsi_sector_size) : 0;
			s_scsi_sec_per_clus = (bpbData[2] != 0) ? bpbData[2] : 1;
			s_scsi_data_start_sector =
			    s_scsi_root_dir_start_sector + s_scsi_root_dir_sector_count;

			// Use caller-provided break address as scratch/work area for BPB and
			// pointer table. Hard-coding low memory (e.g. $00000C00) corrupts
			// Human68k work areas and leads to boot-time crashes later.
			workBase = SCSI_Mask24((breakAddrIn + 0x1f) & ~0x1fU);
			breakAddrOut = workBase + 0x80;
			if (!SCSI_IsLinearRamRange(workBase, 0x80)) {
				workBase = SCSI_BPB_RAM_ADDR;
				breakAddrOut = breakAddrIn;
			}
			s_scsi_bpb_ram_addr = workBase;
			s_scsi_bpbptr_ram_addr = workBase + 0x40;

			// BPB/BPBポインタ領域を初期化（未初期化値がポインタとして
			// 解釈されると、カーネル側で異常アドレス参照が起きる）
			for (i = 0; i < 64; i++) {
				Memory_WriteB(s_scsi_bpb_ram_addr + i, 0x00);
				Memory_WriteB(s_scsi_bpbptr_ram_addr + i, 0x00);
			}

			// BPBをRAMに書き込み
			for (i = 0; i < 36; i++) {
				Memory_WriteB(s_scsi_bpb_ram_addr + i, bpbData[i]);
			}

			// hidden sectors at BPB+16
			{
				DWORD hiddenSec = partOffset / s_scsi_sector_size;
				Memory_WriteB(s_scsi_bpb_ram_addr + 16, (BYTE)((hiddenSec >> 24) & 0xff));
				Memory_WriteB(s_scsi_bpb_ram_addr + 17, (BYTE)((hiddenSec >> 16) & 0xff));
				Memory_WriteB(s_scsi_bpb_ram_addr + 18, (BYTE)((hiddenSec >> 8) & 0xff));
				Memory_WriteB(s_scsi_bpb_ram_addr + 19, (BYTE)(hiddenSec & 0xff));
			}

			// Determine whether FAT16->FAT12 conversion is needed.
			{
				BYTE spc = bpbData[2];
				DWORD dataStart = s_scsi_root_dir_start_sector + s_scsi_root_dir_sector_count;
				DWORD totalClus = (spc > 0 && s_scsi_total_sectors > dataStart) ?
				    (s_scsi_total_sectors - dataStart) / spc : 0;
				if (totalClus < 4085) {
					s_scsi_need_fat16to12 = 1;
				} else {
					s_scsi_need_fat16to12 = 0;
				}
				{
					char fl[128];
					snprintf(fl, sizeof(fl),
					         "SCSI_DEV FAT check: totalSec=%u totalClus=%u fat12conv=%d",
					         (unsigned int)s_scsi_total_sectors,
					         (unsigned int)totalClus,
					         s_scsi_need_fat16to12);
					SCSI_LogText(fl);
				}
			}

			// INIT: BPBポインタ配列は直接BPBアドレス（単一参照）
			// BUILD BPBとは異なり、INITでは直接参照が使われる
			// workBase+$30 に indirect cell も維持（BUILD BPB用）
			{
				DWORD indirectCell = workBase + 0x30;
				Memory_WriteB(indirectCell + 0, (BYTE)((s_scsi_bpb_ram_addr >> 24) & 0xff));
				Memory_WriteB(indirectCell + 1, (BYTE)((s_scsi_bpb_ram_addr >> 16) & 0xff));
				Memory_WriteB(indirectCell + 2, (BYTE)((s_scsi_bpb_ram_addr >> 8) & 0xff));
				Memory_WriteB(indirectCell + 3, (BYTE)(s_scsi_bpb_ram_addr & 0xff));
			}

			// BPB ポインタ配列: 1ユニット分のBPBアドレスのみ格納
			// Human68k INIT では unitCount 個のBPBポインタが必要。
			// 余分なエントリはカーネルに余計なDPBを作らせ無限ループの原因になる。
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 0, (BYTE)((s_scsi_bpb_ram_addr >> 24) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 1, (BYTE)((s_scsi_bpb_ram_addr >> 16) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 2, (BYTE)((s_scsi_bpb_ram_addr >> 8) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 3, (BYTE)(s_scsi_bpb_ram_addr & 0xff));

			// Reserve the scratch area we just populated via break address.
			Memory_WriteB(reqpkt + 0x0E, (BYTE)((breakAddrOut >> 24) & 0xff));
			Memory_WriteB(reqpkt + 0x0F, (BYTE)((breakAddrOut >> 16) & 0xff));
			Memory_WriteB(reqpkt + 0x10, (BYTE)((breakAddrOut >> 8) & 0xff));
			Memory_WriteB(reqpkt + 0x11, (BYTE)(breakAddrOut & 0xff));

			// INIT +$12 は BPB ポインタ配列を返す。
			// [0] = BPB 先頭, [1] = -1(終端)
			// +$0D はユニット数。未設定だとカーネルが不定値(例: $F8)を
			// 参照して異常な初期化ループに入る。
			Memory_WriteB(reqpkt + 0x0D, 0x01);
			Memory_WriteB(reqpkt + 0x12, (BYTE)((s_scsi_bpbptr_ram_addr >> 24) & 0xff));
			Memory_WriteB(reqpkt + 0x13, (BYTE)((s_scsi_bpbptr_ram_addr >> 16) & 0xff));
			Memory_WriteB(reqpkt + 0x14, (BYTE)((s_scsi_bpbptr_ram_addr >> 8) & 0xff));
			Memory_WriteB(reqpkt + 0x15, (BYTE)(s_scsi_bpbptr_ram_addr & 0xff));
				SCSI_SetReqStatus(reqpkt, 1, 0x00);

			snprintf(logLine, sizeof(logLine),
			         "SCSI_DEV INIT ok partOff=0x%X secSize=%u hiddenSec=%u rootSec=%u rootCnt=%u brkIn=$%08X brkOut=$%08X bpb=$%08X",
			         (unsigned int)partOffset,
			         (unsigned int)s_scsi_sector_size,
			         (unsigned int)(partOffset / s_scsi_sector_size),
			         (unsigned int)s_scsi_root_dir_start_sector,
			         (unsigned int)s_scsi_root_dir_sector_count,
			         (unsigned int)breakAddrIn,
			         (unsigned int)breakAddrOut,
			         (unsigned int)s_scsi_bpb_ram_addr);
			SCSI_LogText(logLine);
			SCSI_LogDevicePacket(reqpkt, pktLen);

			// Do NOT force $1C12 to 1 (SCSI).  The IPL ROM sets it to 2
			// (SASI) because it booted from the SASI bus.  Setting it to 1
			// triggers the kernel's SCSI-specific CONFIG.SYS loop at $C3FA
			// (CMP.B #$01,$1C12 → BSR $9014 → CMP.W $1CBC → BEQ restart)
			// which causes an infinite cmd=5/cmd=4 cycle.  Our synthetic
			// block device driver works correctly under the SASI code path.

			// ===== DPB INITIALIZATION =====
			// Human68k system variables (verified from oswork.txt):
			//   $1C38: Current Directory Table (CDT) base address
			//   $1C3C: DPB chain head pointer
			//   $1C7E+A: internal drive number for logical drive A
			//
			// Internal DPB layout (56 bytes per local drive):
			//   +$00: drive number (1.b)
			//   +$01: unit number (1.b)
			//   +$02: device driver address (1.l)
			//   +$06: next DPB address (1.l)
			//   +$0A: bytes per sector (1.w)
			//   +$0C: sectors/cluster - 1 (1.b)
			//   +$0D: cluster shift (1.b, bit7=16-bit FAT)
			//   +$0E: FAT start sector (1.w)
			//   +$10: FAT count (1.b)
			//   +$11: FAT sectors per FAT (1.b)
			//   +$12: max root dir entries (1.w)
			//   +$14: data start sector (1.w)
			//   +$16: total clusters + 1 (1.w)
			//   +$18: root dir start sector (1.w)
			//   +$1A: media byte (1.b)
			//   +$1B: sector-to-byte shift (1.b)
			//
			// CDT entry: 78 bytes ($4E), DPB pointer at +$46
			{
				DWORD cdtBase = ((DWORD)Memory_ReadB(0x1C38) << 24) |
				                ((DWORD)Memory_ReadB(0x1C39) << 16) |
				                ((DWORD)Memory_ReadB(0x1C3A) << 8) |
				                ((DWORD)Memory_ReadB(0x1C3B));
				DWORD dpbHead = ((DWORD)Memory_ReadB(0x1C3C) << 24) |
				                ((DWORD)Memory_ReadB(0x1C3D) << 16) |
				                ((DWORD)Memory_ReadB(0x1C3E) << 8) |
				                ((DWORD)Memory_ReadB(0x1C3F));
				{
					char fixlog[128];
					snprintf(fixlog, sizeof(fixlog),
					         "SCSI_DIAG CDT=$%08X DPB_HEAD=$%08X",
					         (unsigned int)cdtBase, (unsigned int)dpbHead);
					SCSI_LogText(fixlog);
				}
				// Walk DPB chain to find our drive (device=$EA0100)
				// and also find the last DPB to link ours
				{
					DWORD dpb = dpbHead;
					DWORD lastDpb = 0;
					int dpbCount = 0;
					int ourDpbFound = 0;
					while (dpb != 0 && dpb != 0xFFFFFFFF && dpbCount < 26) {
						BYTE drvNum = Memory_ReadB(dpb + 0);
						BYTE unitNum = Memory_ReadB(dpb + 1);
						DWORD devPtr = ((DWORD)Memory_ReadB(dpb + 2) << 24) |
						               ((DWORD)Memory_ReadB(dpb + 3) << 16) |
						               ((DWORD)Memory_ReadB(dpb + 4) << 8) |
						               ((DWORD)Memory_ReadB(dpb + 5));
						WORD secSize = ((WORD)Memory_ReadB(dpb + 0x0A) << 8) |
						               (WORD)Memory_ReadB(dpb + 0x0B);
						char fixlog[128];
						snprintf(fixlog, sizeof(fixlog),
						         "SCSI_DIAG DPB#%d @$%06X drv=%u unit=%u dev=$%08X secSize=%u",
						         dpbCount, (unsigned int)dpb,
						         (unsigned int)drvNum, (unsigned int)unitNum,
						         (unsigned int)devPtr, (unsigned int)secSize);
						SCSI_LogText(fixlog);
						if ((devPtr & 0x00FFFFFF) == (SCSI_SYNTH_DEVHDR_ADDR & 0x00FFFFFF)) {
							ourDpbFound = 1;
						}
						lastDpb = dpb;
						dpb = ((DWORD)Memory_ReadB(dpb + 6) << 24) |
						      ((DWORD)Memory_ReadB(dpb + 7) << 16) |
						      ((DWORD)Memory_ReadB(dpb + 8) << 8) |
						      ((DWORD)Memory_ReadB(dpb + 9));
						dpbCount++;
					}
					if (!ourDpbFound) {
						SCSI_LogText("SCSI_DIAG DPB for SCSI NOT FOUND in chain");
					}
				}
			}
		} else {
			// BPB読取失敗
			Memory_WriteB(reqpkt + 0x0D, 0x00);
				SCSI_SetReqStatus(reqpkt, 0, 0x02);  // error: not ready
			SCSI_LogText("SCSI_DEV INIT failed (no BPB)");
			SCSI_LogDevicePacket(reqpkt, pktLen);
		}
		break;
	}

	case 1: {  // MEDIA CHECK
		// Human68k: +$0E = 1: not changed, 0: unknown, -1($FF): changed
		// (opposite of MS-DOS convention)
		// First call must return "unknown" (0) to force BUILD BPB,
		// otherwise the kernel skips DPB initialization.
		// s_scsi_media_check_count is file-scope (reset in SCSI_Init)
		BYTE pre03 = Memory_ReadB(reqpkt + 0x03);
		BYTE pre0E = Memory_ReadB(reqpkt + 0x0E);
		if (s_scsi_media_check_count < 2) {
			Memory_WriteB(reqpkt + 0x0E, 0x00);  // 0 = unknown → triggers BUILD BPB
			s_scsi_media_check_count++;
		} else {
			Memory_WriteB(reqpkt + 0x0E, 0x01);  // 1 = not changed
		}
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		{
			BYTE post03 = Memory_ReadB(reqpkt + 0x03);
			BYTE post0E = Memory_ReadB(reqpkt + 0x0E);
			char mc[128];
			snprintf(mc, sizeof(mc),
			         "SCSI_DEV cmd=1 MEDIACHECK reqpkt=$%06X pre03=$%02X pre0E=$%02X post03=$%02X post0E=$%02X",
			         (unsigned int)reqpkt,
			         (unsigned int)pre03, (unsigned int)pre0E,
			         (unsigned int)post03, (unsigned int)post0E);
			SCSI_LogText(mc);
		}
		break;
	}

	case 5: {  // DRIVE CONTROL/STATUS (X68000-specific block request)
		// One-time fix: set bit7 of DPB+$0D (cluster shift) to indicate
		// 16-bit FAT.  The disk image uses 16-bit FAT entries but the
		// cluster count (4081) falls below the kernel's auto-detect
		// threshold (~4085), so the kernel defaults to 12-bit.  Without
		// this flag the kernel misreads every FAT entry and cannot follow
		// cluster chains (files/dirs are inaccessible).
		{
			// s_scsi_fat16_fixed is file-scope (reset in SCSI_Init)
			#define s_fat16_fixed s_scsi_fat16_fixed
			if (!s_fat16_fixed) {
				DWORD dpbHead = ((DWORD)Memory_ReadB(0x1C3C) << 24) |
				                ((DWORD)Memory_ReadB(0x1C3D) << 16) |
				                ((DWORD)Memory_ReadB(0x1C3E) << 8) |
				                ((DWORD)Memory_ReadB(0x1C3F));
				DWORD dpb = dpbHead;
				int cnt = 0;
				while (dpb != 0 && dpb != 0xFFFFFFFF && cnt < 10) {
					DWORD devPtr = ((DWORD)Memory_ReadB(dpb + 2) << 24) |
					               ((DWORD)Memory_ReadB(dpb + 3) << 16) |
					               ((DWORD)Memory_ReadB(dpb + 4) << 8) |
					               ((DWORD)Memory_ReadB(dpb + 5));
					if (devPtr == SCSI_SYNTH_DEVHDR_ADDR) {
						s_scsi_dpb_addr = dpb;
						// Do NOT set FAT16 flag (DPB+$0D bit7).
						// The kernel determines FAT type from totalClus,
						// not from this flag.  FAT12 conversion in the
						// READ handler provides correct cluster chains.
						s_fat16_fixed = 1;
						break;
					}
					dpb = ((DWORD)Memory_ReadB(dpb + 6) << 24) |
					      ((DWORD)Memory_ReadB(dpb + 7) << 16) |
					      ((DWORD)Memory_ReadB(dpb + 8) << 8) |
					      ((DWORD)Memory_ReadB(dpb + 9));
					cnt++;
				}
			}
		}
		// Diagnostic: log system vars for first 15 cmd=5 calls
		{
			static int s_cmd5_diag_count = 0;
			if (s_cmd5_diag_count < 15) {
				char diagLine[256];
				BYTE v1c12 = Memory_ReadB(0x1C12);
				WORD v1cbc = ((WORD)Memory_ReadB(0x1CBC) << 8) |
				             (WORD)Memory_ReadB(0x1CBD);
				DWORD v1c28 = ((DWORD)Memory_ReadB(0x1C28) << 24) |
				              ((DWORD)Memory_ReadB(0x1C29) << 16) |
				              ((DWORD)Memory_ReadB(0x1C2A) << 8) |
				              ((DWORD)Memory_ReadB(0x1C2B));
				// Read what's at $1C28 pointer (potential DPB)
				BYTE dpb0 = 0, dpb1 = 0, dpbE = 0;
				if (v1c28 > 0 && v1c28 < 0xC00000 && SCSI_IsLinearRamRange(v1c28, 0x44)) {
					dpb0 = Memory_ReadB(v1c28);
					dpb1 = Memory_ReadB(v1c28 + 1);
					dpbE = Memory_ReadB(v1c28 + 0x0E);
				}
				snprintf(diagLine, sizeof(diagLine),
				         "SCSI_DIAG cmd5#%d $1C12=%02X $1CBC=%04X dpb@$%08X [0]=%02X [1]=%02X [E]=%02X",
				         s_cmd5_diag_count,
				         (unsigned int)v1c12, (unsigned int)v1cbc,
				         (unsigned int)v1c28,
				         (unsigned int)dpb0, (unsigned int)dpb1, (unsigned int)dpbE);
				SCSI_LogText(diagLine);
				// Also dump first 32 bytes at $1C28 pointer (DPB structure)
				if (v1c28 > 0 && v1c28 < 0xC00000 && SCSI_IsLinearRamRange(v1c28, 0x44)) {
					char hexLine[256];
					int j;
					int pos = 0;
					pos += snprintf(hexLine + pos, sizeof(hexLine) - pos,
					                "SCSI_DIAG DPB@$%08X: ", (unsigned int)v1c28);
					for (j = 0; j < 48 && pos < 200; j++) {
						pos += snprintf(hexLine + pos, sizeof(hexLine) - pos, "%02X ",
						                (unsigned int)Memory_ReadB(v1c28 + j));
					}
					SCSI_LogText(hexLine);
				}
				s_cmd5_diag_count++;
			}
		}
		BYTE subcmd = Memory_ReadB(reqpkt + 0x0D);
		DWORD arg0 = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x11));
		DWORD arg1 = ((DWORD)Memory_ReadB(reqpkt + 0x12) << 24) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x13) << 16) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x14) << 8) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x15));
		DWORD arg2 = ((DWORD)Memory_ReadB(reqpkt + 0x16) << 24) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x17) << 16) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x18) << 8) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x19));
		DWORD arg0Mem = SCSI_Mask24(arg0);
		DWORD arg1Mem = SCSI_Mask24(arg1);
		DWORD arg2Mem = SCSI_Mask24(arg2);
		BYTE drvState = s_scsi_drvchk_state;
		char cmd5Log[128];

		// Human68k uses request command 5 on block devices as a drive
		// control/status packet. +$0D is an internal command compatible with
		// IOCS _B_DRVCHK (0,1,2,3,4,5,6,7,9), and the driver returns drive
		// state in +$0D.
		SCSI_LogDevicePacket(reqpkt, pktLen);
		snprintf(cmd5Log, sizeof(cmd5Log),
		         "SCSI_DEV cmd=5 drvchk sub=%u arg0=$%08X arg1=$%08X arg2=$%08X",
		         (unsigned int)subcmd, (unsigned int)arg0,
		         (unsigned int)arg1, (unsigned int)arg2);
		SCSI_LogText(cmd5Log);
		if (arg0 != 0 && SCSI_IsLinearRamRange(arg0Mem, 8)) {
			snprintf(cmd5Log, sizeof(cmd5Log),
			         "SCSI_DEV cmd=5 arg0[8]=%02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)Memory_ReadB(arg0Mem + 0), (unsigned int)Memory_ReadB(arg0Mem + 1),
			         (unsigned int)Memory_ReadB(arg0Mem + 2), (unsigned int)Memory_ReadB(arg0Mem + 3),
			         (unsigned int)Memory_ReadB(arg0Mem + 4), (unsigned int)Memory_ReadB(arg0Mem + 5),
			         (unsigned int)Memory_ReadB(arg0Mem + 6), (unsigned int)Memory_ReadB(arg0Mem + 7));
			SCSI_LogText(cmd5Log);
		}
		if (subcmd == 9) {
			if (arg0 != 0 && SCSI_IsLinearRamRange(arg0Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg0[16]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg0Mem + 0), (unsigned int)Memory_ReadB(arg0Mem + 1),
				         (unsigned int)Memory_ReadB(arg0Mem + 2), (unsigned int)Memory_ReadB(arg0Mem + 3),
				         (unsigned int)Memory_ReadB(arg0Mem + 4), (unsigned int)Memory_ReadB(arg0Mem + 5),
				         (unsigned int)Memory_ReadB(arg0Mem + 6), (unsigned int)Memory_ReadB(arg0Mem + 7),
				         (unsigned int)Memory_ReadB(arg0Mem + 8), (unsigned int)Memory_ReadB(arg0Mem + 9),
				         (unsigned int)Memory_ReadB(arg0Mem + 10), (unsigned int)Memory_ReadB(arg0Mem + 11),
				         (unsigned int)Memory_ReadB(arg0Mem + 12), (unsigned int)Memory_ReadB(arg0Mem + 13),
				         (unsigned int)Memory_ReadB(arg0Mem + 14), (unsigned int)Memory_ReadB(arg0Mem + 15));
				SCSI_LogText(cmd5Log);
			}
			if (arg0 != 0 && SCSI_IsLinearRamRange(arg0Mem, 64)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg0[32..47]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg0Mem + 32), (unsigned int)Memory_ReadB(arg0Mem + 33),
				         (unsigned int)Memory_ReadB(arg0Mem + 34), (unsigned int)Memory_ReadB(arg0Mem + 35),
				         (unsigned int)Memory_ReadB(arg0Mem + 36), (unsigned int)Memory_ReadB(arg0Mem + 37),
				         (unsigned int)Memory_ReadB(arg0Mem + 38), (unsigned int)Memory_ReadB(arg0Mem + 39),
				         (unsigned int)Memory_ReadB(arg0Mem + 40), (unsigned int)Memory_ReadB(arg0Mem + 41),
				         (unsigned int)Memory_ReadB(arg0Mem + 42), (unsigned int)Memory_ReadB(arg0Mem + 43),
				         (unsigned int)Memory_ReadB(arg0Mem + 44), (unsigned int)Memory_ReadB(arg0Mem + 45),
				         (unsigned int)Memory_ReadB(arg0Mem + 46), (unsigned int)Memory_ReadB(arg0Mem + 47));
				SCSI_LogText(cmd5Log);
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg0[48..63]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg0Mem + 48), (unsigned int)Memory_ReadB(arg0Mem + 49),
				         (unsigned int)Memory_ReadB(arg0Mem + 50), (unsigned int)Memory_ReadB(arg0Mem + 51),
				         (unsigned int)Memory_ReadB(arg0Mem + 52), (unsigned int)Memory_ReadB(arg0Mem + 53),
				         (unsigned int)Memory_ReadB(arg0Mem + 54), (unsigned int)Memory_ReadB(arg0Mem + 55),
				         (unsigned int)Memory_ReadB(arg0Mem + 56), (unsigned int)Memory_ReadB(arg0Mem + 57),
				         (unsigned int)Memory_ReadB(arg0Mem + 58), (unsigned int)Memory_ReadB(arg0Mem + 59),
				         (unsigned int)Memory_ReadB(arg0Mem + 60), (unsigned int)Memory_ReadB(arg0Mem + 61),
				         (unsigned int)Memory_ReadB(arg0Mem + 62), (unsigned int)Memory_ReadB(arg0Mem + 63));
				SCSI_LogText(cmd5Log);
			}
			if (SCSI_IsLinearRamRange(arg1Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg1[16] pre=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg1Mem + 0), (unsigned int)Memory_ReadB(arg1Mem + 1),
				         (unsigned int)Memory_ReadB(arg1Mem + 2), (unsigned int)Memory_ReadB(arg1Mem + 3),
				         (unsigned int)Memory_ReadB(arg1Mem + 4), (unsigned int)Memory_ReadB(arg1Mem + 5),
				         (unsigned int)Memory_ReadB(arg1Mem + 6), (unsigned int)Memory_ReadB(arg1Mem + 7),
				         (unsigned int)Memory_ReadB(arg1Mem + 8), (unsigned int)Memory_ReadB(arg1Mem + 9),
				         (unsigned int)Memory_ReadB(arg1Mem + 10), (unsigned int)Memory_ReadB(arg1Mem + 11),
				         (unsigned int)Memory_ReadB(arg1Mem + 12), (unsigned int)Memory_ReadB(arg1Mem + 13),
				         (unsigned int)Memory_ReadB(arg1Mem + 14), (unsigned int)Memory_ReadB(arg1Mem + 15));
				SCSI_LogText(cmd5Log);
			}
			if (SCSI_IsLinearRamRange(arg2Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg2[16] pre=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg2Mem + 0), (unsigned int)Memory_ReadB(arg2Mem + 1),
				         (unsigned int)Memory_ReadB(arg2Mem + 2), (unsigned int)Memory_ReadB(arg2Mem + 3),
				         (unsigned int)Memory_ReadB(arg2Mem + 4), (unsigned int)Memory_ReadB(arg2Mem + 5),
				         (unsigned int)Memory_ReadB(arg2Mem + 6), (unsigned int)Memory_ReadB(arg2Mem + 7),
				         (unsigned int)Memory_ReadB(arg2Mem + 8), (unsigned int)Memory_ReadB(arg2Mem + 9),
				         (unsigned int)Memory_ReadB(arg2Mem + 10), (unsigned int)Memory_ReadB(arg2Mem + 11),
				         (unsigned int)Memory_ReadB(arg2Mem + 12), (unsigned int)Memory_ReadB(arg2Mem + 13),
				         (unsigned int)Memory_ReadB(arg2Mem + 14), (unsigned int)Memory_ReadB(arg2Mem + 15));
				SCSI_LogText(cmd5Log);
			}
		}

		// Human68k applications often use DOS _DRVCTRL, which reaches the block
		// driver's command 5 handler.  Real drivers (for example SUSIE) report a
		// fixed HDD as $42 here; returning $00 makes apps such as LHES/BASIC think
		// the drive is unavailable even though plain DOS file I/O still works.
		drvState = 0x42;
		if (SCSI_ImgBuf() == NULL || SCSI_ImgSize() <= 0) {
			drvState = 0x00;
		}

		// sub=9 is used both as a plain status probe and as a payload-bearing
		// _DRVCTRL path.  Preserve arg payloads and only update +$0D.
		if (subcmd == 9) {
			s_scsi_drvchk_state = drvState;
			Memory_WriteB(reqpkt + 0x0D, drvState);
			if (arg0 == 0 && arg1 == 0 && arg2 == 0) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 probe -> state=$%02X",
				         (unsigned int)drvState);
				SCSI_LogText(cmd5Log);
			} else {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 private/config -> OK(state=$%02X)",
				         (unsigned int)drvState);
				SCSI_LogText(cmd5Log);
			}
			SCSI_SetReqStatus(reqpkt, 1, 0x00);
			SCSI_LogDevicePacket(reqpkt, pktLen);
			break;
		}

		switch (subcmd) {
		case 0: // status check 1
		case 1: // eject
		case 9: // status check 2
			// Fixed media: report state, ignore eject.
			break;
		case 4: // LED on
			break;
		case 5: // LED off
			break;
		case 2: // set eject inhibit 1
		case 3: // clear eject inhibit 1
		case 6: // set eject inhibit 2
		case 7: // clear eject inhibit 2
			// Fixed HDD: keep reporting a stable ready state.
			break;
			default:
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 drvchk sub=%u unsupported -> NOP success",
				         (unsigned int)subcmd);
				SCSI_LogText(cmd5Log);
				// Keep boot resilient: unknown subcommands are treated as
				// no-op success with the current drive-state result.
				break;
			}
		s_scsi_drvchk_state = drvState;
		Memory_WriteB(reqpkt + 0x0D, drvState); // return drive state
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		if (subcmd == 9) {
			if (SCSI_IsLinearRamRange(arg1Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg1[16] post=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg1Mem + 0), (unsigned int)Memory_ReadB(arg1Mem + 1),
				         (unsigned int)Memory_ReadB(arg1Mem + 2), (unsigned int)Memory_ReadB(arg1Mem + 3),
				         (unsigned int)Memory_ReadB(arg1Mem + 4), (unsigned int)Memory_ReadB(arg1Mem + 5),
				         (unsigned int)Memory_ReadB(arg1Mem + 6), (unsigned int)Memory_ReadB(arg1Mem + 7),
				         (unsigned int)Memory_ReadB(arg1Mem + 8), (unsigned int)Memory_ReadB(arg1Mem + 9),
				         (unsigned int)Memory_ReadB(arg1Mem + 10), (unsigned int)Memory_ReadB(arg1Mem + 11),
				         (unsigned int)Memory_ReadB(arg1Mem + 12), (unsigned int)Memory_ReadB(arg1Mem + 13),
				         (unsigned int)Memory_ReadB(arg1Mem + 14), (unsigned int)Memory_ReadB(arg1Mem + 15));
				SCSI_LogText(cmd5Log);
			}
			if (SCSI_IsLinearRamRange(arg2Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg2[16] post=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg2Mem + 0), (unsigned int)Memory_ReadB(arg2Mem + 1),
				         (unsigned int)Memory_ReadB(arg2Mem + 2), (unsigned int)Memory_ReadB(arg2Mem + 3),
				         (unsigned int)Memory_ReadB(arg2Mem + 4), (unsigned int)Memory_ReadB(arg2Mem + 5),
				         (unsigned int)Memory_ReadB(arg2Mem + 6), (unsigned int)Memory_ReadB(arg2Mem + 7),
				         (unsigned int)Memory_ReadB(arg2Mem + 8), (unsigned int)Memory_ReadB(arg2Mem + 9),
				         (unsigned int)Memory_ReadB(arg2Mem + 10), (unsigned int)Memory_ReadB(arg2Mem + 11),
				         (unsigned int)Memory_ReadB(arg2Mem + 12), (unsigned int)Memory_ReadB(arg2Mem + 13),
				         (unsigned int)Memory_ReadB(arg2Mem + 14), (unsigned int)Memory_ReadB(arg2Mem + 15));
				SCSI_LogText(cmd5Log);
			}
		}
		snprintf(cmd5Log, sizeof(cmd5Log),
		         "SCSI_DEV cmd=5 drvchk sub=%u -> state=$%02X",
		         (unsigned int)subcmd, (unsigned int)drvState);
		SCSI_LogText(cmd5Log);
		SCSI_LogDevicePacket(reqpkt, pktLen);
		break;
	}

	case 2: {  // BUILD BPB
		// BPBポインタを返す (INITで設定済)
		// Human68k カーネルは BUILD BPB の返値も二重参照する:
		// reqpkt+$12 → indirect cell → BPB data
		// s_scsi_bpb_ram_addr + 0x30 に indirect cell を配置済
		DWORD bpbAddr = s_scsi_bpb_ram_addr ? s_scsi_bpb_ram_addr : (DWORD)SCSI_BPB_RAM_ADDR;
		DWORD indirectCell = bpbAddr + 0x30;  // workBase + 0x30
		// indirect cell に BPB データアドレスを再書き込み（念のため）
		Memory_WriteB(indirectCell + 0, (BYTE)((bpbAddr >> 24) & 0xff));
		Memory_WriteB(indirectCell + 1, (BYTE)((bpbAddr >> 16) & 0xff));
		Memory_WriteB(indirectCell + 2, (BYTE)((bpbAddr >> 8) & 0xff));
		Memory_WriteB(indirectCell + 3, (BYTE)(bpbAddr & 0xff));
		// reqpkt+$12 に indirect cell アドレスを返す（カーネルがここから二重参照）
		Memory_WriteB(reqpkt + 0x12, (BYTE)((indirectCell >> 24) & 0xff));
		Memory_WriteB(reqpkt + 0x13, (BYTE)((indirectCell >> 16) & 0xff));
		Memory_WriteB(reqpkt + 0x14, (BYTE)((indirectCell >> 8) & 0xff));
		Memory_WriteB(reqpkt + 0x15, (BYTE)(indirectCell & 0xff));
		{
			char bpblog[128];
			snprintf(bpblog, sizeof(bpblog),
			         "SCSI_DEV BUILD_BPB indCell=$%08X *indCell=$%08X",
			         (unsigned int)indirectCell, (unsigned int)bpbAddr);
			SCSI_LogText(bpblog);
		}
		// カーネルディスパッチャ ($CBA6) は byte+3==$00 を成功として扱う。
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		break;
	}

	case 4: {  // READ
		SCSI_LogDevicePacket(reqpkt, pktLen);
		SCSI_LogKernelQueueState("SCSI_DEV READ pre");
		// Diagnostic: log kernel system vars that control CONFIG.SYS loop
		{
			static int s_read_diag_count = 0;
			if (s_read_diag_count < 10) {
				char diagLine[256];
				BYTE v1c12 = Memory_ReadB(0x1C12);
				DWORD cdtBase = ((DWORD)Memory_ReadB(0x1C38) << 24) |
				                ((DWORD)Memory_ReadB(0x1C39) << 16) |
				                ((DWORD)Memory_ReadB(0x1C3A) << 8) |
				                ((DWORD)Memory_ReadB(0x1C3B));
				DWORD dpbHead = ((DWORD)Memory_ReadB(0x1C3C) << 24) |
				                ((DWORD)Memory_ReadB(0x1C3D) << 16) |
				                ((DWORD)Memory_ReadB(0x1C3E) << 8) |
				                ((DWORD)Memory_ReadB(0x1C3F));
				snprintf(diagLine, sizeof(diagLine),
				         "SCSI_DIAG READ#%d $1C12=%02X CDT=$%08X DPB_HEAD=$%08X",
				         s_read_diag_count,
				         (unsigned int)v1c12,
				         (unsigned int)cdtBase, (unsigned int)dpbHead);
				SCSI_LogText(diagLine);

				// Walk DPB chain and dump each entry (full dump for our device)
				{
					DWORD dpb = dpbHead;
					int cnt = 0;
					while (dpb != 0 && dpb != 0xFFFFFFFF && cnt < 10) {
						BYTE drvNum = Memory_ReadB(dpb + 0);
						BYTE unitNum = Memory_ReadB(dpb + 1);
						DWORD devPtr = ((DWORD)Memory_ReadB(dpb + 2) << 24) |
						               ((DWORD)Memory_ReadB(dpb + 3) << 16) |
						               ((DWORD)Memory_ReadB(dpb + 4) << 8) |
						               ((DWORD)Memory_ReadB(dpb + 5));
						WORD secSize = ((WORD)Memory_ReadB(dpb + 0x0A) << 8) |
						               (WORD)Memory_ReadB(dpb + 0x0B);
						char dl[256];
						snprintf(dl, sizeof(dl),
						         "SCSI_DIAG DPB#%d @$%06X drv=%u unit=%u dev=$%08X sec=%u",
						         cnt, (unsigned int)dpb, (unsigned int)drvNum,
						         (unsigned int)unitNum,
						         (unsigned int)devPtr, (unsigned int)secSize);
						SCSI_LogText(dl);

						// Full dump for our SCSI device DPB
						if (devPtr == SCSI_SYNTH_DEVHDR_ADDR) {
							BYTE secPerClus = Memory_ReadB(dpb + 0x0C);
							BYTE clusShift  = Memory_ReadB(dpb + 0x0D);
							WORD fatStart   = ((WORD)Memory_ReadB(dpb + 0x0E) << 8) |
							                  (WORD)Memory_ReadB(dpb + 0x0F);
							BYTE fatCount   = Memory_ReadB(dpb + 0x10);
							BYTE fatSectors = Memory_ReadB(dpb + 0x11);
							WORD rootEntries = ((WORD)Memory_ReadB(dpb + 0x12) << 8) |
							                   (WORD)Memory_ReadB(dpb + 0x13);
							WORD dataStart  = ((WORD)Memory_ReadB(dpb + 0x14) << 8) |
							                  (WORD)Memory_ReadB(dpb + 0x15);
							WORD totalClus  = ((WORD)Memory_ReadB(dpb + 0x16) << 8) |
							                  (WORD)Memory_ReadB(dpb + 0x17);
							WORD rootStart  = ((WORD)Memory_ReadB(dpb + 0x18) << 8) |
							                  (WORD)Memory_ReadB(dpb + 0x19);
							BYTE mediaByte  = Memory_ReadB(dpb + 0x1A);
							BYTE secShift   = Memory_ReadB(dpb + 0x1B);
							snprintf(dl, sizeof(dl),
							         "  secClus-1=%u cluShift=%u fatStart=%u fatCnt=%u fatSec=%u",
							         (unsigned int)secPerClus, (unsigned int)clusShift,
							         (unsigned int)fatStart, (unsigned int)fatCount,
							         (unsigned int)fatSectors);
							SCSI_LogText(dl);
							snprintf(dl, sizeof(dl),
							         "  rootEnt=%u dataStart=%u totalClus+1=%u rootStart=%u media=$%02X secShift=%u",
							         (unsigned int)rootEntries, (unsigned int)dataStart,
							         (unsigned int)totalClus, (unsigned int)rootStart,
							         (unsigned int)mediaByte, (unsigned int)secShift);
							SCSI_LogText(dl);
							snprintf(dl, sizeof(dl),
							         "  DPB+$1C: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
							         Memory_ReadB(dpb+0x1C), Memory_ReadB(dpb+0x1D),
							         Memory_ReadB(dpb+0x1E), Memory_ReadB(dpb+0x1F),
							         Memory_ReadB(dpb+0x20), Memory_ReadB(dpb+0x21),
							         Memory_ReadB(dpb+0x22), Memory_ReadB(dpb+0x23),
							         Memory_ReadB(dpb+0x24), Memory_ReadB(dpb+0x25),
							         Memory_ReadB(dpb+0x26), Memory_ReadB(dpb+0x27),
							         Memory_ReadB(dpb+0x28), Memory_ReadB(dpb+0x29),
							         Memory_ReadB(dpb+0x2A), Memory_ReadB(dpb+0x2B));
							SCSI_LogText(dl);
							snprintf(dl, sizeof(dl),
							         "  DPB+$2C: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
							         Memory_ReadB(dpb+0x2C), Memory_ReadB(dpb+0x2D),
							         Memory_ReadB(dpb+0x2E), Memory_ReadB(dpb+0x2F),
							         Memory_ReadB(dpb+0x30), Memory_ReadB(dpb+0x31),
							         Memory_ReadB(dpb+0x32), Memory_ReadB(dpb+0x33),
							         Memory_ReadB(dpb+0x34), Memory_ReadB(dpb+0x35),
							         Memory_ReadB(dpb+0x36), Memory_ReadB(dpb+0x37),
							         Memory_ReadB(dpb+0x38), Memory_ReadB(dpb+0x39),
							         Memory_ReadB(dpb+0x3A), Memory_ReadB(dpb+0x3B));
							SCSI_LogText(dl);
						}

						dpb = ((DWORD)Memory_ReadB(dpb + 6) << 24) |
						      ((DWORD)Memory_ReadB(dpb + 7) << 16) |
						      ((DWORD)Memory_ReadB(dpb + 8) << 8) |
						      ((DWORD)Memory_ReadB(dpb + 9));
						cnt++;
					}
				}
				s_read_diag_count++;

				// One-time dump: kernel code at READ return addresses
				if (s_read_diag_count == 1) {
					DWORD base;
					char rl[200];
					// Dump $CB90-$CC30 (return from dev dispatch at $CBAA)
					for (base = 0xCB90; base < 0xCC30; base += 16) {
						snprintf(rl, sizeof(rl),
						         "KREAD $%06X: %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X",
						         (unsigned int)base,
						         Memory_ReadB(base+0),  Memory_ReadB(base+1),
						         Memory_ReadB(base+2),  Memory_ReadB(base+3),
						         Memory_ReadB(base+4),  Memory_ReadB(base+5),
						         Memory_ReadB(base+6),  Memory_ReadB(base+7),
						         Memory_ReadB(base+8),  Memory_ReadB(base+9),
						         Memory_ReadB(base+10), Memory_ReadB(base+11),
						         Memory_ReadB(base+12), Memory_ReadB(base+13),
						         Memory_ReadB(base+14), Memory_ReadB(base+15));
						SCSI_LogText(rl);
					}
					// Dump $EB00-$EB60 (caller2 area at $EB24)
					for (base = 0xEB00; base < 0xEB60; base += 16) {
						snprintf(rl, sizeof(rl),
						         "KCALL $%06X: %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X",
						         (unsigned int)base,
						         Memory_ReadB(base+0),  Memory_ReadB(base+1),
						         Memory_ReadB(base+2),  Memory_ReadB(base+3),
						         Memory_ReadB(base+4),  Memory_ReadB(base+5),
						         Memory_ReadB(base+6),  Memory_ReadB(base+7),
						         Memory_ReadB(base+8),  Memory_ReadB(base+9),
						         Memory_ReadB(base+10), Memory_ReadB(base+11),
						         Memory_ReadB(base+12), Memory_ReadB(base+13),
						         Memory_ReadB(base+14), Memory_ReadB(base+15));
						SCSI_LogText(rl);
					}
					// Dump $B680-$B6E0 (caller at $B692)
					for (base = 0xB680; base < 0xB6E0; base += 16) {
						snprintf(rl, sizeof(rl),
						         "KCALR $%06X: %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X",
						         (unsigned int)base,
						         Memory_ReadB(base+0),  Memory_ReadB(base+1),
						         Memory_ReadB(base+2),  Memory_ReadB(base+3),
						         Memory_ReadB(base+4),  Memory_ReadB(base+5),
						         Memory_ReadB(base+6),  Memory_ReadB(base+7),
						         Memory_ReadB(base+8),  Memory_ReadB(base+9),
						         Memory_ReadB(base+10), Memory_ReadB(base+11),
						         Memory_ReadB(base+12), Memory_ReadB(base+13),
						         Memory_ReadB(base+14), Memory_ReadB(base+15));
						SCSI_LogText(rl);
					}
					// Dump stack around $6580-$6620 (where a7 is at crash)
					for (base = 0x6570; base < 0x6630; base += 16) {
						snprintf(rl, sizeof(rl),
						         "STK $%06X: %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X",
						         (unsigned int)base,
						         Memory_ReadB(base+0),  Memory_ReadB(base+1),
						         Memory_ReadB(base+2),  Memory_ReadB(base+3),
						         Memory_ReadB(base+4),  Memory_ReadB(base+5),
						         Memory_ReadB(base+6),  Memory_ReadB(base+7),
						         Memory_ReadB(base+8),  Memory_ReadB(base+9),
						         Memory_ReadB(base+10), Memory_ReadB(base+11),
						         Memory_ReadB(base+12), Memory_ReadB(base+13),
						         Memory_ReadB(base+14), Memory_ReadB(base+15));
						SCSI_LogText(rl);
					}
				}
			}
		}
		DWORD bufAddr = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x11));
		bufAddr = SCSI_Mask24(bufAddr);
		DWORD count   = ((DWORD)Memory_ReadB(reqpkt + 0x12) << 24) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x13) << 16) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x14) << 8) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x15));
		DWORD startSec = ((DWORD)Memory_ReadB(reqpkt + 0x16) << 24) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x17) << 16) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x18) << 8) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x19));
		DWORD secSize = s_scsi_sector_size;
		unsigned long long byteOffset;
		unsigned long long absByteOffset;
		unsigned long long relByteOffset = 0;
		unsigned long long byteCount;
		unsigned long long imgSize = 0;
		DWORD hiddenSec = 0;
		int useRelative = 0;
		DWORD i;

		absByteOffset = (unsigned long long)startSec * (unsigned long long)secSize;
		byteOffset = absByteOffset;
		if (secSize != 0 && s_scsi_partition_byte_offset != 0) {
			hiddenSec = s_scsi_partition_byte_offset / secSize;
			relByteOffset = (unsigned long long)s_scsi_partition_byte_offset + absByteOffset;
			if (s_scsi_dev_absolute_sectors == 1) {
				useRelative = 0;
			} else if (s_scsi_dev_absolute_sectors == 0) {
				useRelative = 1;
			} else {
				// Infer once from the first root-directory style access.
				// rootSec matches BPB-relative layout, rootSec+hidden matches
				// absolute layout.
				if (hiddenSec != 0 &&
				    startSec == s_scsi_root_dir_start_sector) {
					useRelative = 1;
					s_scsi_dev_absolute_sectors = 0;
					snprintf(logLine, sizeof(logLine),
					         "SCSI_DEV MODE infer=relative sec=%u root=%u hidden=%u",
					         (unsigned int)startSec,
					         (unsigned int)s_scsi_root_dir_start_sector,
					         (unsigned int)hiddenSec);
					SCSI_LogText(logLine);
				} else if (hiddenSec != 0 &&
				           startSec == (s_scsi_root_dir_start_sector + hiddenSec)) {
					useRelative = 0;
					s_scsi_dev_absolute_sectors = 1;
					snprintf(logLine, sizeof(logLine),
					         "SCSI_DEV MODE infer=absolute sec=%u root=%u hidden=%u",
					         (unsigned int)startSec,
					         (unsigned int)s_scsi_root_dir_start_sector,
					         (unsigned int)hiddenSec);
					SCSI_LogText(logLine);
				} else {
					// Conservative default for this driver path.
					useRelative = 1;
				}
			}
			byteOffset = useRelative ? relByteOffset : absByteOffset;
		}
		byteCount = (unsigned long long)count * (unsigned long long)secSize;
		if (SCSI_ImgBuf() != NULL && SCSI_ImgSize() > 0) {
			imgSize = (unsigned long long)SCSI_ImgSize();
		}
		if (SCSI_ImgBuf() != NULL &&
		    SCSI_ImgSize() > 0 &&
		    (byteOffset + byteCount > imgSize)) {
			if (useRelative &&
			    (absByteOffset + byteCount <= imgSize)) {
				byteOffset = absByteOffset;
				useRelative = 0;
				s_scsi_dev_absolute_sectors = 1;
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ fallback absolute sec=%u off=0x%llX part=0x%X",
				         (unsigned int)startSec,
				         absByteOffset,
				         (unsigned int)s_scsi_partition_byte_offset);
				SCSI_LogText(logLine);
			} else if (!useRelative &&
			           relByteOffset != 0 &&
			           (relByteOffset + byteCount <= imgSize)) {
				byteOffset = relByteOffset;
				useRelative = 1;
				s_scsi_dev_absolute_sectors = 0;
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ fallback relative sec=%u off=0x%llX hidden=%u part=0x%X",
				         (unsigned int)startSec,
				         relByteOffset,
				         (unsigned int)hiddenSec,
				         (unsigned int)s_scsi_partition_byte_offset);
				SCSI_LogText(logLine);
			} else if (absByteOffset + byteCount <= imgSize) {
				byteOffset = absByteOffset;
				useRelative = 0;
			}
		}

		snprintf(logLine, sizeof(logLine),
		         "SCSI_DEV READ sec=%u cnt=%u buf=$%08X off=0x%llX abs=0x%llX rel=0x%llX mode=%s hidden=%u part=0x%X bytes=%llu",
		         (unsigned int)startSec, (unsigned int)count,
		         (unsigned int)bufAddr,
		         byteOffset, absByteOffset, relByteOffset,
		         useRelative ? "rel" : "abs",
		         (unsigned int)hiddenSec,
		         (unsigned int)s_scsi_partition_byte_offset,
		         byteCount);
		SCSI_LogText(logLine);
		/* verbose src dump removed for log space */

		if (SCSI_ImgBuf() == NULL || byteCount == 0) {
			SCSI_SetReqStatus(reqpkt, 0, 0x02);
			SCSI_LogText("SCSI_DEV READ error (no image)");
			break;
		}
#ifdef __APPLE__
		/* SCSI-U モード: 物理 HDD から直接ブロック読み出し */
		if (X68000_GetStorageBusMode() == 2) {
			if (!SCSI_IsLinearRamRange(bufAddr, (DWORD)byteCount)) {
				SCSI_SetReqStatus(reqpkt, 0, 0x02);
				SCSI_LogText("SCSI_DEV READ SCSIU error (buf range)");
				break;
			}
			{
				DWORD physLBA   = (DWORD)(byteOffset / secSize);
				DWORD physBlks  = (DWORD)(byteCount  / secSize);
				BYTE* tmpBuf    = (BYTE*)malloc((DWORD)byteCount);
				if (tmpBuf == NULL) {
					SCSI_SetReqStatus(reqpkt, 0, 0x02);
					SCSI_LogText("SCSI_DEV READ SCSIU malloc fail");
					break;
				}
				if (SCSIU_ReadBlocks(physLBA, physBlks, secSize, tmpBuf)) {
					for (i = 0; i < (DWORD)byteCount; i++)
						Memory_WriteB(bufAddr + i, tmpBuf[i]);
				} else {
					for (i = 0; i < (DWORD)byteCount; i++)
						Memory_WriteB(bufAddr + i, 0x00);
				}
				free(tmpBuf);
				SCSI_SetReqStatus(reqpkt, 1, 0x00);
				{
					char rl[96];
					snprintf(rl, sizeof(rl),
					         "SCSI_DEV READ SCSIU sec=%u lba=%u blks=%u",
					         (unsigned int)startSec,
					         (unsigned int)physLBA,
					         (unsigned int)physBlks);
					SCSI_LogText(rl);
				}
			}
			break;
		}
#endif /* __APPLE__ */
		if (byteOffset + byteCount > (unsigned long long)SCSI_ImgSize()) {
			// Out-of-range sector: return zeros instead of error.
			// The kernel may request sectors beyond the image (e.g. for
			// clusters at high offsets in large partitions).  Returning
			// an error aborts the boot; returning zeros lets the kernel
			// see "empty" data and continue.
			if (SCSI_IsLinearRamRange(bufAddr, (DWORD)byteCount)) {
				for (i = 0; i < (DWORD)byteCount; i++) {
					Memory_WriteB(bufAddr + i, 0x00);
				}
			}
			SCSI_SetReqStatus(reqpkt, 1, 0x00);  // success with empty data
			{
				char rl[128];
				snprintf(rl, sizeof(rl),
				         "SCSI_DEV READ out-of-range sec=%u → zeros (off=0x%llX imgSize=%lld)",
				         (unsigned int)startSec, byteOffset,
				         (long long)SCSI_ImgSize());
				SCSI_LogText(rl);
			}
			break;
		}

		if (!SCSI_IsLinearRamRange(bufAddr, (DWORD)byteCount)) {
			SCSI_SetReqStatus(reqpkt, 0, 0x02);
			SCSI_LogText("SCSI_DEV READ error (buf range)");
			break;
		}

		{
			BYTE* imgPtr = SCSI_ImgBuf();
			for (i = 0; i < (DWORD)byteCount; i++) {
				Memory_WriteB(bufAddr + i, imgPtr[(DWORD)byteOffset + i]);
			}
		}

		// --- FAT16 → FAT12 on-the-fly conversion ---
		// The disk image has 16-bit FAT entries but the kernel uses 12-bit
		// FAT (totalClus < 4085).  Convert each 16-bit entry to a 12-bit
		// packed entry so the kernel can follow cluster chains correctly.
		if (s_scsi_need_fat16to12 && s_scsi_fat_start_sector > 0) {
			DWORD fatEnd = s_scsi_fat_start_sector +
			               s_scsi_fat_sectors * s_scsi_fat_count;
			if (startSec >= s_scsi_fat_start_sector && startSec < fatEnd) {
				// Determine which copy of FAT and offset within it
				DWORD fatRelSec = startSec - s_scsi_fat_start_sector;
				if (fatRelSec >= s_scsi_fat_sectors)
					fatRelSec -= s_scsi_fat_sectors; // 2nd FAT copy
				DWORD fat12ByteBase = fatRelSec * s_scsi_sector_size;

				// FAT16 data location in image
				DWORD imgFatBase = (DWORD)s_scsi_partition_byte_offset +
				                   s_scsi_fat_start_sector * s_scsi_sector_size;
				unsigned long long imgSize =
				    (unsigned long long)SCSI_ImgSize();

				// Clear the buffer first
				for (i = 0; i < (DWORD)byteCount; i++)
					Memory_WriteB(bufAddr + i, 0x00);

				// Convert each cluster's FAT16 entry to FAT12
				// FAT12: cluster N at byte offset N*3/2
				// Determine cluster range for this sector
				DWORD firstClus = fat12ByteBase * 2 / 3;
				DWORD lastClus  = (fat12ByteBase + (DWORD)byteCount) * 2 / 3 + 2;
				if (lastClus > 65535) lastClus = 65535;
				DWORD clus;
				BYTE* imgFatPtr = SCSI_ImgBuf();
				for (clus = firstClus; clus <= lastClus; clus++) {
					DWORD fat12Pos = clus * 3 / 2;
					if (fat12Pos + 1 < fat12ByteBase ||
					    fat12Pos >= fat12ByteBase + (DWORD)byteCount)
						continue;

					// Read FAT16 entry from image/cache
					DWORD f16off = imgFatBase + clus * 2;
					WORD entry16 = 0;
					if (imgFatPtr != NULL && f16off + 1 < (DWORD)imgSize) {
						entry16 = ((WORD)imgFatPtr[f16off] << 8) |
						          (WORD)imgFatPtr[f16off + 1];
					}
					// Clamp to 12-bit
					WORD entry12 = entry16;
					if (entry12 >= 0xFFF0) entry12 = 0xFFF;
					else if (entry12 > 0xFFF) entry12 = 0xFFF;

					// Pack FAT12 entry (little-endian, MS-DOS/Human68k compatible):
					// Even N: byte[off] = entry[7:0], byte[off+1] high nibble unchanged, low nibble = entry[11:8]
					// Odd N:  byte[off] low nibble unchanged, high nibble = entry[3:0]<<4, byte[off+1] = entry[11:4]
					DWORD bpos = fat12Pos - fat12ByteBase;
					if (clus & 1) {
						// Odd cluster
						if (bpos < (DWORD)byteCount) {
							BYTE lo = Memory_ReadB(bufAddr + bpos);
							Memory_WriteB(bufAddr + bpos,
							              (lo & 0x0F) | ((entry12 & 0x0F) << 4));
						}
						if (bpos + 1 < (DWORD)byteCount) {
							Memory_WriteB(bufAddr + bpos + 1,
							              (entry12 >> 4) & 0xFF);
						}
					} else {
						// Even cluster
						if (bpos < (DWORD)byteCount) {
							Memory_WriteB(bufAddr + bpos,
							              entry12 & 0xFF);
						}
						if (bpos + 1 < (DWORD)byteCount) {
							BYTE hi = Memory_ReadB(bufAddr + bpos + 1);
							Memory_WriteB(bufAddr + bpos + 1,
							              (hi & 0xF0) | ((entry12 >> 8) & 0x0F));
						}
					}
				}
				{
					static int s_fat_conv_log = 0;
					if (s_fat_conv_log < 3) {
						char fl[128];
						snprintf(fl, sizeof(fl),
						         "SCSI_DEV FAT16to12 sec=%u clus=%u-%u base=%u",
						         (unsigned int)startSec,
						         (unsigned int)firstClus,
						         (unsigned int)lastClus,
						         (unsigned int)fat12ByteBase);
						SCSI_LogText(fl);
						s_fat_conv_log++;
					}
				}
			}
		}

		// Verify copy integrity: compare image bytes vs memory for entry 8 (CONFIG.SYS position)
		if (startSec == s_scsi_root_dir_start_sector && count >= 1) {
			DWORD entOff = 8 * 32; // entry 8 = CONFIG.SYS
			BYTE* vImgPtr = SCSI_ImgBuf();
			if (vImgPtr != NULL && entOff + 32 <= byteCount &&
			    (DWORD)byteOffset + entOff + 32 <= (DWORD)SCSI_ImgSize()) {
				char vLine[256];
				int mismatch = 0;
				for (i = 0; i < 32; i++) {
					BYTE imgByte = vImgPtr[(DWORD)byteOffset + entOff + i];
					BYTE memByte = Memory_ReadB(bufAddr + entOff + i);
					if (imgByte != memByte) mismatch++;
				}
				if (mismatch > 0) {
					snprintf(vLine, sizeof(vLine),
					         "SCSI_VERIFY MISMATCH entry8 count=%d img22-27=%02X%02X%02X%02X%02X%02X mem22-27=%02X%02X%02X%02X%02X%02X",
					         mismatch,
					         vImgPtr[(DWORD)byteOffset + entOff + 22],
					         vImgPtr[(DWORD)byteOffset + entOff + 23],
					         vImgPtr[(DWORD)byteOffset + entOff + 24],
					         vImgPtr[(DWORD)byteOffset + entOff + 25],
					         vImgPtr[(DWORD)byteOffset + entOff + 26],
					         vImgPtr[(DWORD)byteOffset + entOff + 27],
					         Memory_ReadB(bufAddr + entOff + 22),
					         Memory_ReadB(bufAddr + entOff + 23),
					         Memory_ReadB(bufAddr + entOff + 24),
					         Memory_ReadB(bufAddr + entOff + 25),
					         Memory_ReadB(bufAddr + entOff + 26),
					         Memory_ReadB(bufAddr + entOff + 27));
					SCSI_LogText(vLine);
				} else {
					snprintf(vLine, sizeof(vLine),
					         "SCSI_VERIFY OK entry8 img26-27=%02X%02X mem26-27=%02X%02X",
					         vImgPtr[(DWORD)byteOffset + entOff + 26],
					         vImgPtr[(DWORD)byteOffset + entOff + 27],
					         Memory_ReadB(bufAddr + entOff + 26),
					         Memory_ReadB(bufAddr + entOff + 27));
					SCSI_LogText(vLine);
				}
			}
		}
		// --- CONFIG.SYS boot-phase patch ---
		// During early boot the kernel's FAT buffer is not yet loaded, so
		// it re-reads the CONFIG.SYS sector in a loop.  We detect the boot
		// phase and return EOF after enough reads to process all lines.
		// Once boot is complete (other sectors are read), we stop patching
		// so normal file I/O (TYPE CONFIG.SYS etc.) works.
		{
			// Use file-scope variables (reset in SCSI_Init)
			#define s_config_sector s_scsi_config_sector
			#define s_config_size s_scsi_config_size
			#define s_config_read_count s_scsi_config_read_count
			#define s_config_boot_phase s_scsi_config_boot_phase

			// Detect CONFIG.SYS sector on first encounter (fallback)
			if (s_config_sector == 0 &&
			    startSec != s_scsi_root_dir_start_sector) {
				BYTE b0 = Memory_ReadB(bufAddr + 0);
				BYTE b1 = Memory_ReadB(bufAddr + 1);
				BYTE b2 = Memory_ReadB(bufAddr + 2);
				BYTE b3 = Memory_ReadB(bufAddr + 3);
				if (b0 == 'F' && b1 == 'I' && b2 == 'L' && b3 == 'E') {
					s_config_sector = startSec;
					if (s_config_size == 0) {
						s_config_size = secSize;
					}
				}
			}

			// If CONFIG.SYS has no SHELL= line, inject a default.
			// Without SHELL=, AUTOEXEC.BAT runs in a child process and
			// environment changes such as PATH are lost.
			if (startSec == s_config_sector && s_config_sector != 0) {
				int hasShell = 0;
				DWORD lastRemPos = 0xFFFFFFFF;
				DWORD lastRemEnd = 0;
				DWORD lineStart = 0;
				for (i = 0; i < (DWORD)byteCount; i++) {
					BYTE c = Memory_ReadB(bufAddr + i);
					if (c == 0x1A || c == 0x00) {
						break;
					}
					if (c == 0x0A) {
						lineStart = i + 1;
					}
					if (i == lineStart) {
						if (c == 'S' && i + 4 < (DWORD)byteCount &&
						    Memory_ReadB(bufAddr + i + 1) == 'H' &&
						    Memory_ReadB(bufAddr + i + 2) == 'E' &&
						    Memory_ReadB(bufAddr + i + 3) == 'L' &&
						    Memory_ReadB(bufAddr + i + 4) == 'L') {
							hasShell = 1;
						}
						if ((c == 'r' || c == 'R') && i + 3 < (DWORD)byteCount &&
						    (Memory_ReadB(bufAddr + i + 1) == 'e' || Memory_ReadB(bufAddr + i + 1) == 'E') &&
						    (Memory_ReadB(bufAddr + i + 2) == 'm' || Memory_ReadB(bufAddr + i + 2) == 'M') &&
						    Memory_ReadB(bufAddr + i + 3) == ' ') {
							lastRemPos = i;
						}
					}
				}
				if (lastRemPos != 0xFFFFFFFF) {
					for (i = lastRemPos; i < (DWORD)byteCount; i++) {
						BYTE c = Memory_ReadB(bufAddr + i);
						if (c == 0x0A || c == 0x1A || c == 0x00) {
							lastRemEnd = (c == 0x0A) ? i + 1 : i;
							break;
						}
					}
				}
				if (!hasShell && lastRemPos != 0xFFFFFFFF && lastRemEnd > lastRemPos) {
					const char* shell = "SHELL=\\COMMAND.X /P\r\n";
					DWORD shellLen = 0;
					DWORD remLen = lastRemEnd - lastRemPos;
					while (shell[shellLen] != '\0') {
						shellLen++;
					}
					for (i = 0; i < shellLen && lastRemPos + i < (DWORD)byteCount; i++) {
						Memory_WriteB(bufAddr + lastRemPos + i, (BYTE)shell[i]);
					}
					for (; i < remLen && lastRemPos + i < (DWORD)byteCount; i++) {
						Memory_WriteB(bufAddr + lastRemPos + i, ' ');
					}
				}
			}

			// CONFIG.SYS is re-read in large bursts while the kernel processes
			// each line. Only force EOF after the loop is clearly runaway, and
			// reset the burst counter once any other sector is touched.
			if (startSec == s_config_sector && s_config_sector != 0) {
				s_config_read_count++;
				if (s_config_read_count > 2000) {
					Memory_WriteB(bufAddr + 0, 0x1A);
					for (i = 1; i < (DWORD)byteCount && i < 32; i++) {
						Memory_WriteB(bufAddr + i, 0x00);
					}
					Memory_WriteB(reqpkt + 0x12, 0x00);
					Memory_WriteB(reqpkt + 0x13, 0x00);
					Memory_WriteB(reqpkt + 0x14, 0x00);
					Memory_WriteB(reqpkt + 0x15, 0x00);
					if (s_config_read_count == 2001) {
						SCSI_LogText("SCSI_DEV CONFIG_LOOP_ESCAPE force EOF/len0");
					}
				} else if (s_config_read_count <= 4 || (s_config_read_count % 64) == 0) {
					char cfgLog[160];
					snprintf(cfgLog, sizeof(cfgLog),
					         "SCSI_DEV CONFIG_READ sec=%u size=%u read#=%d",
					         (unsigned int)startSec,
					         (unsigned int)s_config_size,
					         s_config_read_count);
					SCSI_LogText(cfgLog);
				}
			} else {
				s_config_read_count = 0;
			}
		}

		SCSI_NormalizeRootShortNames(bufAddr, startSec, count, secSize);
		SCSI_LogRootConfigEntry(bufAddr, startSec, count, secSize);

		// Mark the DPB buffer state as valid so the kernel stops re-reading
		// the same sector forever during CONFIG.SYS processing.
		if (s_scsi_dpb_addr != 0) {
			Memory_WriteB(s_scsi_dpb_addr + 0x1C, 0x02);
		}

		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		break;
	}

	case 8:   // WRITE
	case 9: { // WRITE WITH VERIFY
		DWORD bufAddr = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x11));
		bufAddr = SCSI_Mask24(bufAddr);
		DWORD count   = ((DWORD)Memory_ReadB(reqpkt + 0x12) << 24) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x13) << 16) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x14) << 8) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x15));
		DWORD startSec = ((DWORD)Memory_ReadB(reqpkt + 0x16) << 24) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x17) << 16) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x18) << 8) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x19));
		DWORD secSize = s_scsi_sector_size;
		unsigned long long byteOffset;
		unsigned long long absByteOffset;
		unsigned long long relByteOffset = 0;
		unsigned long long byteCount;
		unsigned long long imgSize = 0;
		DWORD hiddenSec = 0;
		int useRelative = 0;
		DWORD i;

		absByteOffset = (unsigned long long)startSec * (unsigned long long)secSize;
		byteOffset = absByteOffset;
		if (secSize != 0 && s_scsi_partition_byte_offset != 0) {
			hiddenSec = s_scsi_partition_byte_offset / secSize;
			relByteOffset = (unsigned long long)s_scsi_partition_byte_offset + absByteOffset;
			if (s_scsi_dev_absolute_sectors == 1) {
				useRelative = 0;
			} else if (s_scsi_dev_absolute_sectors == 0) {
				useRelative = 1;
			} else if (hiddenSec != 0 &&
			           startSec == s_scsi_root_dir_start_sector) {
				useRelative = 1;
			} else if (hiddenSec != 0 &&
			           startSec == (s_scsi_root_dir_start_sector + hiddenSec)) {
				useRelative = 0;
			} else {
				useRelative = 1;
			}
			byteOffset = useRelative ? relByteOffset : absByteOffset;
		}
		byteCount = (unsigned long long)count * (unsigned long long)secSize;
		if (SCSI_ImgBuf() != NULL && SCSI_ImgSize() > 0) {
			imgSize = (unsigned long long)SCSI_ImgSize();
		}
		if (SCSI_ImgBuf() != NULL &&
		    SCSI_ImgSize() > 0 &&
		    (byteOffset + byteCount > imgSize)) {
			if (useRelative &&
			    (absByteOffset + byteCount <= imgSize)) {
				byteOffset = absByteOffset;
				useRelative = 0;
				s_scsi_dev_absolute_sectors = 1;
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV WRITE fallback absolute sec=%u off=0x%llX part=0x%X",
				         (unsigned int)startSec,
				         absByteOffset,
				         (unsigned int)s_scsi_partition_byte_offset);
				SCSI_LogText(logLine);
			} else if (!useRelative &&
			           relByteOffset != 0 &&
			           (relByteOffset + byteCount <= imgSize)) {
				byteOffset = relByteOffset;
				useRelative = 1;
				s_scsi_dev_absolute_sectors = 0;
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV WRITE fallback relative sec=%u off=0x%llX hidden=%u part=0x%X",
				         (unsigned int)startSec,
				         relByteOffset,
				         (unsigned int)hiddenSec,
				         (unsigned int)s_scsi_partition_byte_offset);
				SCSI_LogText(logLine);
			} else if (absByteOffset + byteCount <= imgSize) {
				byteOffset = absByteOffset;
				useRelative = 0;
			}
		}
		snprintf(logLine, sizeof(logLine),
		         "SCSI_DEV WRITE sec=%u cnt=%u buf=$%08X off=0x%llX abs=0x%llX rel=0x%llX mode=%s hidden=%u part=0x%X bytes=%llu",
		         (unsigned int)startSec, (unsigned int)count,
		         (unsigned int)bufAddr,
		         byteOffset, absByteOffset, relByteOffset,
		         useRelative ? "rel" : "abs",
		         (unsigned int)hiddenSec,
		         (unsigned int)s_scsi_partition_byte_offset,
		         byteCount);
		SCSI_LogText(logLine);
		{
			BYTE* wImgPtr = SCSI_ImgBuf();
			if (wImgPtr != NULL &&
			    SCSI_ImgSize() > 0 &&
			    byteCount >= 16 &&
			    byteOffset + 16 <= (unsigned long long)SCSI_ImgSize()) {
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV WRITE pre16=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 0],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 1],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 2],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 3],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 4],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 5],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 6],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 7],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 8],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 9],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 10],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 11],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 12],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 13],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 14],
				         (unsigned int)wImgPtr[(DWORD)byteOffset + 15]);
				SCSI_LogText(logLine);
			}
		}

		if (SCSI_ImgBuf() == NULL || byteCount == 0 ||
		    !SCSI_IsLinearRamRange(bufAddr, (DWORD)byteCount)) {
			SCSI_SetReqStatus(reqpkt, 0, 0x02);
			break;
		}
#ifdef __APPLE__
		/* SCSI-U モード: 物理 HDD へ直接ブロック書き込み */
		if (X68000_GetStorageBusMode() == 2) {
			DWORD physLBA  = (DWORD)(byteOffset / secSize);
			DWORD physBlks = (DWORD)(byteCount  / secSize);
			BYTE* tmpBuf   = (BYTE*)malloc((DWORD)byteCount);
			if (tmpBuf == NULL) {
				SCSI_SetReqStatus(reqpkt, 0, 0x02);
				SCSI_LogText("SCSI_DEV WRITE SCSIU malloc fail");
				break;
			}
			for (i = 0; i < (DWORD)byteCount; i++)
				tmpBuf[i] = Memory_ReadB(bufAddr + i);
			if (!SCSIU_WriteBlocks(physLBA, physBlks, secSize, tmpBuf)) {
				free(tmpBuf);
				SCSI_SetReqStatus(reqpkt, 0, 0x02);
				SCSI_LogText("SCSI_DEV WRITE SCSIU failed");
				break;
			}
			SCSIU_InvalidateBootCacheRange(physLBA, physBlks, secSize);
			free(tmpBuf);
			SCSI_SetReqStatus(reqpkt, 1, 0x00);
			{
				char wl[96];
				snprintf(wl, sizeof(wl),
				         "SCSI_DEV WRITE SCSIU sec=%u lba=%u blks=%u",
				         (unsigned int)startSec,
				         (unsigned int)physLBA,
				         (unsigned int)physBlks);
				SCSI_LogText(wl);
			}
			break;
		}
#endif /* __APPLE__ */
		if (byteOffset + byteCount > (unsigned long long)SCSI_ImgSize()) {
			SCSI_SetReqStatus(reqpkt, 0, 0x02);
			break;
		}

		for (i = 0; i < (DWORD)byteCount; i++) {
			s_disk_image_buffer[4][(DWORD)byteOffset + i] = Memory_ReadB(bufAddr + i);
		}
		SASI_SetDirtyFlag(0);

		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		break;
	}

	case 0x57: {  // Human68k extended parameter query (observed)
		SCSI_LogDevicePacket(reqpkt, Memory_ReadB(reqpkt + 0));

		// Unknown extension command.
		// Return success to keep Human68k probe paths progressing.
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		SCSI_LogText("SCSI_DEV cmd=0x57 unsupported (NOP success)");
		break;
	}

	default:
		// 未対応コマンドでも起動継続を優先し、成功扱いで返す。
		// 一部のHuman68k系ドライバは拡張コマンドを投げるため、
		// ここでエラーを返すと起動シーケンスが崩れる。
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		snprintf(logLine, sizeof(logLine),
		         "SCSI_DEV unsupported cmd=%u (NOP success)",
		         (unsigned int)cmd);
		SCSI_LogText(logLine);
		{
			DWORD dbgBuf = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x11));
			DWORD dbgCnt = ((DWORD)Memory_ReadB(reqpkt + 0x12) << 24) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x13) << 16) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x14) << 8) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x15));
			DWORD dbgSec = ((DWORD)Memory_ReadB(reqpkt + 0x16) << 24) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x17) << 16) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x18) << 8) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x19));
			snprintf(logLine, sizeof(logLine),
			         "SCSI_DEV unsupported detail buf=$%08X cnt=%u sec=%u",
			         (unsigned int)dbgBuf,
			         (unsigned int)dbgCnt,
			         (unsigned int)dbgSec);
			SCSI_LogText(logLine);
		}
		break;
	}
#endif
}

static void SCSI_LogDevicePacket(DWORD reqpkt, BYTE len)
{
	char line[256];
	size_t pos = 0;
	int i;
	int dumpLen = (len > 32) ? 32 : (int)len;
	reqpkt = SCSI_Mask24(reqpkt);

	if (!SCSI_IsLinearRamRange(reqpkt, (DWORD)dumpLen)) {
		SCSI_LogText("SCSI_DEV pkt dump skipped (range)");
		return;
	}

	pos += (size_t)snprintf(line + pos, sizeof(line) - pos,
	                        "SCSI_DEV pkt[%u]:",
	                        (unsigned int)dumpLen);
	for (i = 0; i < dumpLen && pos + 4 < sizeof(line); i++) {
		pos += (size_t)snprintf(line + pos, sizeof(line) - pos,
		                        " %02X", (unsigned int)Memory_ReadB(reqpkt + (DWORD)i));
	}
	SCSI_LogText(line);
}


// -----------------------------------------------------------------------
//   デバイスドライバチェインリンク
//   RAMにあるNULデバイスを探し、チェイン末尾に合成デバイスをリンク
// -----------------------------------------------------------------------
int SCSI_IsDeviceLinked(void)
{
	return s_scsi_dev_linked;
}

int SCSI_HasBootActivity(void)
{
	return s_scsi_boot_activity;
}

int SCSI_HasDriverActivity(void)
{
	return s_scsi_driver_activity;
}

void SCSI_LinkDeviceDriver(void)
{
#if defined(HAVE_C68K)
	DWORD addr;
	DWORD devAddr = 0;
	DWORD nextPtr;
	DWORD chainLen = 0;
	char logLine[128];

	if (s_scsi_dev_linked) {
		return;
	}
	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		return;
	}

	// NULデバイスヘッダを探す ("NUL     " at +$0E)
	// カーネルは $6800 付近にロードされる
	for (addr = 0x6800; addr < 0x20000; addr += 2) {
		if (Memory_ReadB(addr + 0x0E) == 'N' &&
		    Memory_ReadB(addr + 0x0F) == 'U' &&
		    Memory_ReadB(addr + 0x10) == 'L' &&
		    Memory_ReadB(addr + 0x11) == ' ' &&
		    Memory_ReadB(addr + 0x12) == ' ') {
			// 属性チェック: bit 15 set (character device)
			WORD attr = ((WORD)Memory_ReadB(addr + 4) << 8) |
			            (WORD)Memory_ReadB(addr + 5);
			if (attr & 0x8000) {
				devAddr = addr;
				break;
			}
		}
	}

	if (devAddr == 0) {
		return;  // NULデバイス未発見 - カーネル未初期化
	}

	snprintf(logLine, sizeof(logLine),
	         "SCSI_DEV NUL found at $%08X", (unsigned int)devAddr);
	SCSI_LogText(logLine);

	// チェインを辿って末尾を見つける
	addr = devAddr;
	while (chainLen < 64) {
		nextPtr = ((DWORD)Memory_ReadB(addr + 0) << 24) |
		          ((DWORD)Memory_ReadB(addr + 1) << 16) |
		          ((DWORD)Memory_ReadB(addr + 2) << 8) |
		          ((DWORD)Memory_ReadB(addr + 3));
		if (nextPtr == 0xFFFFFFFF || nextPtr == 0) {
			break;
		}
		// 既にリンク済みかチェック - リンクは維持するが初期化は続行
		if ((nextPtr & 0x00FFFFFF) == (SCSI_SYNTH_DEVHDR_ADDR & 0x00FFFFFF)) {
			// リセット後は再初期化が必要なため linked フラグだけ設定して
			// return しない（INIT が呼ばれるようにする）
			s_scsi_dev_linked = 1;
			SCSI_LogText("SCSI_DEV already linked (re-init pending)");
			return;
		}
		addr = nextPtr & 0x00FFFFFF;
		chainLen++;
	}

	// チェイン末尾のnextポインタを合成デバイスヘッダへ向ける
	Memory_WriteB(addr + 0, (BYTE)((SCSI_SYNTH_DEVHDR_ADDR >> 24) & 0xff));
	Memory_WriteB(addr + 1, (BYTE)((SCSI_SYNTH_DEVHDR_ADDR >> 16) & 0xff));
	Memory_WriteB(addr + 2, (BYTE)((SCSI_SYNTH_DEVHDR_ADDR >> 8) & 0xff));
	Memory_WriteB(addr + 3, (BYTE)(SCSI_SYNTH_DEVHDR_ADDR & 0xff));

	s_scsi_dev_linked = 1;

	snprintf(logLine, sizeof(logLine),
	         "SCSI_DEV linked at chain end $%08X → $%08X (chain=%u)",
	         (unsigned int)addr, (unsigned int)SCSI_SYNTH_DEVHDR_ADDR,
	         (unsigned int)(chainLen + 1));
	SCSI_LogText(logLine);
#endif
}
