// ---------------------------------------------------------------------------------------
//  FUK_HOST.C - FLOPPY Ukun (ふろっぴU君, TNB製作所): host serial ports (macOS)
//
//  Finds the unit through IOKit (VID 0x04D8 / PID 0xE6B1), opens its two
//  CDC-ACM ports raw and identifies them: port 1 answers 'f' to 0x00, port 2
//  answers 'F'. While connected, a reader thread moves everything the unit
//  sends into one ring per port; the emulation thread (fuk.c) takes it from
//  there, so the unit is never held up by the frame pacing.
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
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#define FUK_MAX_PORTS 4
#define FUK_PATH_LEN  256
#define FUK_PROBE_TIMEOUT_MS 200
#define FUK_WRITE_TIMEOUT_MS 1000

static char s_fuk_status[2048] = "Not connected";

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

// Opens every candidate and identifies it. On success *fd1 / *fd2 hold the
// open ports 1 and 2 (when keepOpen) and their paths are reported.
static int FUK_Identify(int keepOpen, int* fd1, int* fd2)
{
	FUKPortCandidate ports[FUK_MAX_PORTS];
	int count;
	int i;
	int port1 = -1, port2 = -1;
	int open1 = -1, open2 = -1;

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
		if (role >= 0) {
			FUK_StatusAppend("  answer $%02X '%c'\n", role, (role >= 0x20 && role < 0x7f) ? role : '?');
		}
		if (role == 'f' && port1 < 0) {
			port1 = i;
			if (keepOpen) { open1 = fd; continue; }
		} else if (role == 'F' && port2 < 0) {
			port2 = i;
			if (keepOpen) { open2 = fd; continue; }
		}
		FUK_ClosePort(fd);
	}

	if (port1 >= 0 && port2 >= 0) {
		FUK_StatusAppend("OK: port 1 = %s, port 2 = %s\n", ports[port1].path, ports[port2].path);
		if (keepOpen) {
			*fd1 = open1;
			*fd2 = open2;
		}
		return 1;
	}
	FUK_ClosePort(open1);
	FUK_ClosePort(open2);
	if (count == 0) {
		FUK_StatusAppend("NG: unit not found\n");
	} else {
		FUK_StatusAppend("NG: could not identify both ports\n");
	}
	return 0;
}

int FUK_Probe(void)
{
	if (FUK_IsActive()) {
		// the ports are ours already; report the connection instead
		return 1;
	}
	FUK_StatusReset();
	return FUK_Identify(0, NULL, NULL);
}

// -----------------------------------------------------------------------
//   connection: one ring per port, filled by a reader thread
// -----------------------------------------------------------------------

#define FUK_RING_SIZE (1u << 20)

typedef struct {
	BYTE buf[FUK_RING_SIZE];
	atomic_size_t head;     // written by the reader thread
	atomic_size_t tail;     // written by the emulation thread
} FUKRing;

static FUKRing s_ring[2];
static int s_fd[2] = { -1, -1 };
static int s_wakePipe[2] = { -1, -1 };
static pthread_t s_reader;
static int s_readerRunning = 0;
static atomic_int s_stop;
static atomic_int s_lost;   // 0, or errno-like code of the failure

static void FUK_RingReset(FUKRing* ring)
{
	atomic_store(&ring->head, 0);
	atomic_store(&ring->tail, 0);
}

static void* FUK_ReaderThread(void* arg)
{
	BYTE chunk[4096];
	(void)arg;
	while (!atomic_load(&s_stop)) {
		struct pollfd pfd[3];
		int i, r;
		pfd[0].fd = s_fd[0]; pfd[0].events = POLLIN; pfd[0].revents = 0;
		pfd[1].fd = s_fd[1]; pfd[1].events = POLLIN; pfd[1].revents = 0;
		pfd[2].fd = s_wakePipe[0]; pfd[2].events = POLLIN; pfd[2].revents = 0;
		r = poll(pfd, 3, 200);
		if (r < 0) {
			if (errno == EINTR) continue;
			atomic_store(&s_lost, errno ? errno : EIO);
			break;
		}
		if (pfd[2].revents) break;
		for (i = 0; i < 2; i++) {
			FUKRing* ring = &s_ring[i];
			size_t head, tail, space, want;
			ssize_t n;
			if (pfd[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				atomic_store(&s_lost, ENXIO);
				return NULL;
			}
			if (!(pfd[i].revents & POLLIN)) continue;
			head = atomic_load_explicit(&ring->head, memory_order_relaxed);
			tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
			space = FUK_RING_SIZE - (head - tail);
			if (space == 0) {
				usleep(1000);   // the emulation thread is behind; let it catch up
				continue;
			}
			want = space < sizeof(chunk) ? space : sizeof(chunk);
			n = read(s_fd[i], chunk, want);
			if (n == 0) {
				atomic_store(&s_lost, ENXIO);   // unplugged
				return NULL;
			}
			if (n < 0) {
				if (errno == EAGAIN || errno == EINTR) continue;
				atomic_store(&s_lost, errno);
				return NULL;
			}
			{
				size_t k;
				for (k = 0; k < (size_t)n; k++) {
					ring->buf[(head + k) & (FUK_RING_SIZE - 1)] = chunk[k];
				}
				atomic_store_explicit(&ring->head, head + (size_t)n, memory_order_release);
			}
		}
	}
	return NULL;
}

static int FUK_TransportRead(int port, BYTE* buf, int max)
{
	FUKRing* ring = &s_ring[(port == 2) ? 1 : 0];
	size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
	size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
	size_t avail = head - tail;
	size_t n, k;
	if (avail == 0) {
		return atomic_load(&s_lost) ? -1 : 0;
	}
	n = avail < (size_t)max ? avail : (size_t)max;
	for (k = 0; k < n; k++) {
		buf[k] = ring->buf[(tail + k) & (FUK_RING_SIZE - 1)];
	}
	atomic_store_explicit(&ring->tail, tail + n, memory_order_release);
	return (int)n;
}

static long long FUK_TransportClock(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int FUK_TransportWrite(int port, const BYTE* buf, int len)
{
	int fd = s_fd[(port == 2) ? 1 : 0];
	long long start = FUK_TransportClock();
	int done = 0;
	if (fd < 0 || atomic_load(&s_lost)) return -1;
	while (done < len) {
		ssize_t n = write(fd, buf + done, (size_t)(len - done));
		if (n > 0) {
			done += (int)n;
			continue;
		}
		if (n < 0 && errno != EAGAIN && errno != EINTR) {
			atomic_store(&s_lost, errno);
			return -1;
		}
		if (FUK_TransportClock() - start > FUK_WRITE_TIMEOUT_MS) {
			atomic_store(&s_lost, ETIMEDOUT);
			return -1;
		}
		{
			struct pollfd pfd;
			pfd.fd = fd; pfd.events = POLLOUT; pfd.revents = 0;
			poll(&pfd, 1, 10);
		}
	}
	return 0;
}

static void FUK_TransportWait(void)
{
	sched_yield();
}

static void FUK_Release(void)
{
	int i;
	if (s_readerRunning) {
		BYTE one = 1;
		atomic_store(&s_stop, 1);
		if (s_wakePipe[1] >= 0) {
			ssize_t ignored = write(s_wakePipe[1], &one, 1);
			(void)ignored;
		}
		pthread_join(s_reader, NULL);
		s_readerRunning = 0;
	}
	for (i = 0; i < 2; i++) {
		FUK_ClosePort(s_fd[i]);
		s_fd[i] = -1;
		if (s_wakePipe[i] >= 0) {
			close(s_wakePipe[i]);
			s_wakePipe[i] = -1;
		}
	}
}

static void FUK_TransportLost(const char* reason)
{
	int code = atomic_load(&s_lost);
	FUK_Release();
	FUK_StatusReset();
	if (code) {
		FUK_StatusAppend("Disconnected: %s (%s)\n", reason, strerror(code));
	} else {
		FUK_StatusAppend("Disconnected: %s\n", reason);
	}
}

static const FUKTransport s_transport = {
	FUK_TransportRead,
	FUK_TransportWrite,
	FUK_TransportWait,
	FUK_TransportClock,
	FUK_TransportLost,
};

int FUK_Connect(void)
{
	int fd1 = -1, fd2 = -1;

	if (FUK_IsActive()) {
		return 1;
	}
	FUK_Release();
	FUK_StatusReset();
	if (!FUK_Identify(1, &fd1, &fd2)) {
		return 0;
	}

	s_fd[0] = fd1;
	s_fd[1] = fd2;
	FUK_RingReset(&s_ring[0]);
	FUK_RingReset(&s_ring[1]);
	atomic_store(&s_stop, 0);
	atomic_store(&s_lost, 0);
	if (pipe(s_wakePipe) != 0) {
		FUK_StatusAppend("NG: pipe failed: %s\n", strerror(errno));
		FUK_Release();
		return 0;
	}
	if (pthread_create(&s_reader, NULL, FUK_ReaderThread, NULL) != 0) {
		FUK_StatusAppend("NG: reader thread could not start\n");
		FUK_Release();
		return 0;
	}
	s_readerRunning = 1;
	FUK_Attach(&s_transport);
	FUK_StatusAppend("Connected\n");
	return 1;
}

void FUK_Disconnect(void)
{
	int wasActive = FUK_IsActive();
	FUK_Detach();
	FUK_Release();
	if (wasActive) {
		FUK_StatusReset();
		FUK_StatusAppend("Disconnected\n");
	}
}

#else

int FUK_Probe(void)
{
	FUK_StatusReset();
	FUK_StatusAppend("FLOPPY Ukun is supported on macOS only\n");
	return 0;
}

int FUK_Connect(void)
{
	return FUK_Probe();
}

void FUK_Disconnect(void)
{
	FUK_Detach();
}

#endif
