// ---------------------------------------------------------------------------------------
//  FUK.C - FLOPPY Ukun (ふろっぴU君, TNB製作所) USB FDC unit
//
//  Stage 0: find the unit, open both CDC ports and identify them by the
//  'f' / 'F' handshake. Nothing else is sent to the unit yet.
//  The protocol follows FUK.java in XEiJ+t (Copyright TNB製作所).
// ---------------------------------------------------------------------------------------

#include "fuk.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef __APPLE__
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#define FUK_MAX_PORTS 4
#define FUK_PATH_LEN  256
#define FUK_PROBE_TIMEOUT_MS 200

static char s_fuk_status[2048] = "Not probed";

static void FUK_StatusReset(void)
{
	s_fuk_status[0] = '\0';
}

static void FUK_StatusAppend(const char* fmt, ...)
{
	size_t used = strlen(s_fuk_status);
	va_list ap;

	if (used >= sizeof(s_fuk_status) - 1) {
		return;
	}
	va_start(ap, fmt);
	vsnprintf(s_fuk_status + used, sizeof(s_fuk_status) - used, fmt, ap);
	va_end(ap);
}

const char* FUK_GetStatus(void)
{
	return s_fuk_status;
}

#ifdef __APPLE__

typedef struct {
	char path[FUK_PATH_LEN];
	int interfaceNumber;
} FUKPortCandidate;

static int FUK_SearchParentInt(io_object_t service, CFStringRef key, int* outValue)
{
	CFTypeRef ref;
	int ok = 0;

	ref = IORegistryEntrySearchCFProperty(service, kIOServicePlane, key, kCFAllocatorDefault,
	                                      kIORegistryIterateRecursively | kIORegistryIterateParents);
	if (ref == NULL) {
		return 0;
	}
	if (CFGetTypeID(ref) == CFNumberGetTypeID()) {
		SInt32 value = 0;
		if (CFNumberGetValue((CFNumberRef)ref, kCFNumberSInt32Type, &value)) {
			*outValue = (int)value;
			ok = 1;
		}
	}
	CFRelease(ref);
	return ok;
}

static int FUK_CopyCalloutPath(io_object_t service, char* outPath, int outSize)
{
	CFTypeRef ref;
	Boolean ok;

	ref = IORegistryEntryCreateCFProperty(service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
	if (ref == NULL) {
		return 0;
	}
	ok = CFGetTypeID(ref) == CFStringGetTypeID() &&
	     CFStringGetCString((CFStringRef)ref, outPath, outSize, kCFStringEncodingUTF8);
	CFRelease(ref);
	return ok ? 1 : 0;
}

// Collects the callout devices (/dev/cu.*) of every serial port whose USB
// ancestor carries the FLOPPY Ukun vendor and product ID, ordered by the USB
// interface number.
static int FUK_FindPorts(FUKPortCandidate ports[FUK_MAX_PORTS])
{
	CFMutableDictionaryRef match;
	io_iterator_t iterator;
	io_object_t service;
	int count = 0;

	match = IOServiceMatching(kIOSerialBSDServiceValue);
	if (match == NULL) {
		return 0;
	}
	CFDictionarySetValue(match, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));
	if (IOServiceGetMatchingServices(MACH_PORT_NULL, match, &iterator) != KERN_SUCCESS) {
		return 0;
	}

	while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
		int vendor = -1, product = -1, ifnum = -1;
		char path[FUK_PATH_LEN];

		if (FUK_SearchParentInt(service, CFSTR("idVendor"), &vendor) &&
		    FUK_SearchParentInt(service, CFSTR("idProduct"), &product) &&
		    vendor == FUK_VENDOR_ID && product == FUK_PRODUCT_ID &&
		    FUK_CopyCalloutPath(service, path, sizeof(path)) &&
		    count < FUK_MAX_PORTS) {
			int i;
			FUK_SearchParentInt(service, CFSTR("bInterfaceNumber"), &ifnum);
			// insertion sort by interface number
			i = count;
			while (i > 0 && ports[i - 1].interfaceNumber > ifnum) {
				ports[i] = ports[i - 1];
				--i;
			}
			strncpy(ports[i].path, path, FUK_PATH_LEN - 1);
			ports[i].path[FUK_PATH_LEN - 1] = '\0';
			ports[i].interfaceNumber = ifnum;
			++count;
		}
		IOObjectRelease(service);
	}
	IOObjectRelease(iterator);
	return count;
}

static int FUK_OpenPort(const char* path)
{
	int fd;
	int bits;
	struct termios tty;

	fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		FUK_StatusAppend("  open failed: %s (errno %d)\n", strerror(errno), errno);
		return -1;
	}
	if (ioctl(fd, TIOCEXCL) < 0) {
		FUK_StatusAppend("  TIOCEXCL failed: %s (errno %d)\n", strerror(errno), errno);
		close(fd);
		return -1;
	}
	if (tcgetattr(fd, &tty) < 0) {
		FUK_StatusAppend("  tcgetattr failed: %s (errno %d)\n", strerror(errno), errno);
		close(fd);
		return -1;
	}
	// CDC-ACM ignores the line settings; raw 8N1 keeps the tty layer out of the way.
	cfmakeraw(&tty);
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);
	tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
	tty.c_cflag |= CS8 | CREAD | CLOCAL;
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSANOW, &tty) < 0) {
		FUK_StatusAppend("  tcsetattr failed: %s (errno %d)\n", strerror(errno), errno);
		close(fd);
		return -1;
	}
	// XEiJ+t (jSerialComm) keeps DTR and RTS asserted while connected and
	// clears both on disconnect, so assert them explicitly.
	bits = TIOCM_DTR | TIOCM_RTS;
	if (ioctl(fd, TIOCMBIS, &bits) < 0) {
		FUK_StatusAppend("  TIOCMBIS failed: %s (errno %d)\n", strerror(errno), errno);
	}
	tcflush(fd, TCIOFLUSH);
	return fd;
}

static void FUK_ClosePort(int fd)
{
	int bits = TIOCM_DTR | TIOCM_RTS;
	if (fd < 0) {
		return;
	}
	ioctl(fd, TIOCMBIC, &bits);
	close(fd);
}

// Sends 0x00 and waits for the one-byte answer. Returns the answer (0-255),
// or -1 on timeout or error.
static int FUK_ProbeRole(int fd)
{
	unsigned char request = 0x00;
	unsigned char response[16];
	struct pollfd pfd;
	ssize_t n;
	int r;

	tcflush(fd, TCIOFLUSH);
	if (write(fd, &request, 1) != 1) {
		FUK_StatusAppend("  write failed: %s (errno %d)\n", strerror(errno), errno);
		return -1;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	r = poll(&pfd, 1, FUK_PROBE_TIMEOUT_MS);
	if (r < 0) {
		FUK_StatusAppend("  poll failed: %s (errno %d)\n", strerror(errno), errno);
		return -1;
	}
	if (r == 0) {
		FUK_StatusAppend("  no answer within %d ms\n", FUK_PROBE_TIMEOUT_MS);
		return -1;
	}
	n = read(fd, response, sizeof(response));
	if (n <= 0) {
		FUK_StatusAppend("  read failed: %s (errno %d)\n", n < 0 ? strerror(errno) : "no data", n < 0 ? errno : 0);
		return -1;
	}
	if (n > 1) {
		FUK_StatusAppend("  (%d extra byte(s) after the answer)\n", (int)(n - 1));
	}
	return response[0];
}

int FUK_Probe(void)
{
	FUKPortCandidate ports[FUK_MAX_PORTS];
	int count;
	int i;
	int port1 = -1, port2 = -1;

	FUK_StatusReset();
	memset(ports, 0, sizeof(ports));
	count = FUK_FindPorts(ports);
	FUK_StatusAppend("FLOPPY Ukun (VID %04X / PID %04X): %d serial port(s) found\n",
	                 FUK_VENDOR_ID, FUK_PRODUCT_ID, count);

	for (i = 0; i < count; ++i) {
		int fd;
		int role;

		FUK_StatusAppend("%s (interface %d)\n", ports[i].path, ports[i].interfaceNumber);
		fd = FUK_OpenPort(ports[i].path);
		if (fd < 0) {
			continue;
		}
		role = FUK_ProbeRole(fd);
		FUK_ClosePort(fd);
		if (role < 0) {
			continue;
		}
		FUK_StatusAppend("  answer $%02X '%c'\n", role, (role >= 0x20 && role < 0x7f) ? role : '?');
		if (role == 'f' && port1 < 0) {
			port1 = i;
		} else if (role == 'F' && port2 < 0) {
			port2 = i;
		}
	}

	if (port1 >= 0 && port2 >= 0) {
		FUK_StatusAppend("OK: port 1 = %s, port 2 = %s\n", ports[port1].path, ports[port2].path);
		return 1;
	}
	if (count == 0) {
		FUK_StatusAppend("NG: unit not found\n");
	} else {
		FUK_StatusAppend("NG: could not identify both ports\n");
	}
	return 0;
}

#else

int FUK_Probe(void)
{
	FUK_StatusReset();
	FUK_StatusAppend("FLOPPY Ukun is supported on macOS only\n");
	return 0;
}

#endif
