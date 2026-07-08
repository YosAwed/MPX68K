//---------------------------------------------------------------------------
//
//    X68000 EMULATOR "MPX68K"
//
//    Copyright (C) 2025 Awed
//    [ PPI(i8255A) ]
//
//---------------------------------------------------------------------------

#include "common.h"
#include "ppi.h"
#include "../x11/joystick.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/termios.h>
#include <pthread.h>
#include <os/log.h>
#include <IOKit/IOKitLib.h>

// External function declaration
extern void X68000_Joystick_Set(BYTE num, BYTE data);
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/serial/IOSerialKeys.h>

// JoyportU mode configuration
int joyport_ukun_mode = 0;  // 0: disabled, 1: notify mode, 2: command mode

// PPI internal state
static BYTE ppi_porta = 0xFF;
static BYTE ppi_portb = 0xFF;
static BYTE ppi_portc = 0x00;

// JoyportU device state
typedef struct {
    int fd;                    // Serial port file descriptor
    pthread_t rx_thread;       // Receive thread
    pthread_mutex_t mutex;     // Thread synchronization
    int active;                // Device active flag
    int notify_mode;           // 0: command mode, 1: notify mode
    BYTE port_a, port_b, port_c; // Port values
    volatile int recv_end_mark; // End mark received flag
    volatile BYTE recv_data;    // Received data
    int rx_thread_running;     // Receive thread exists (notify mode only)
    int old_command_mode;      // Firmware < v2 command protocol
} JoyportU_Device;

static JoyportU_Device joyport_device = {-1, 0, PTHREAD_MUTEX_INITIALIZER, 0, 0, 0xFF, 0xFF, 0x00, 0, 0, 0, 0};

// Command-mode protocol state (mirrors the ppi_8255.h reference impl).
// In command mode there is NO receive thread: every response is read
// synchronously on the caller's thread, exactly like the reference,
// which stops its thread once command mode is confirmed.
static BYTE ppi_control = 0x92;    // 8255 control word (A/B input, C output)
static BYTE cmd_prefetch[256];     // Port A burst prefetch buffer
static int  cmd_prefetch_len = 0;
static int  cmd_prefetch_pos = 0;
static BYTE last_joyport_cmd = 0;
static int  cmd_timeout_log_count = 0;
static int  cmd_read_log_count = 0;

static os_log_t JoyportULog(void)
{
    static os_log_t log;
    if (!log) {
        log = os_log_create("com.goroman.x68mac", "joyportu");
    }
    return log;
}

//---------------------------------------------------------------------------
//
//	JoyportU USB device detection
//
//---------------------------------------------------------------------------
static char* FindJoyportUDevice(void)
{
    CFMutableDictionaryRef matchDict = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchDict) return NULL;
    
    // Set VID/PID for JoyportU device
    CFDictionarySetValue(matchDict, CFSTR(kUSBVendorID), 
                        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, (const SInt32[]){0x04d8}));
    CFDictionarySetValue(matchDict, CFSTR(kUSBProductID),
                        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, (const SInt32[]){0xE6B3}));
    
    io_iterator_t iterator;
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, matchDict, &iterator) != KERN_SUCCESS) {
        return NULL;
    }
    
    io_object_t usbDevice;
    char* devicePath = NULL;
    
    while ((usbDevice = IOIteratorNext(iterator))) {
        // Find associated serial port
        io_iterator_t childIterator;
        if (IORegistryEntryCreateIterator(usbDevice, kIOServicePlane, 
                                        kIORegistryIterateRecursively, &childIterator) == KERN_SUCCESS) {
            io_object_t childService;
            while ((childService = IOIteratorNext(childIterator))) {
                CFStringRef devicePathRef = IORegistryEntryCreateCFProperty(
                    childService, CFSTR(kIODialinDeviceKey), kCFAllocatorDefault, 0);
                if (devicePathRef) {
                    devicePath = malloc(256);
                    CFStringGetCString(devicePathRef, devicePath, 256, kCFStringEncodingUTF8);
                    CFRelease(devicePathRef);
                    IOObjectRelease(childService);
                    break;
                }
                IOObjectRelease(childService);
            }
            IOObjectRelease(childIterator);
        }
        IOObjectRelease(usbDevice);
        if (devicePath) break;
    }
    IOObjectRelease(iterator);
    return devicePath;
}

//---------------------------------------------------------------------------
//
//	Synchronous single-byte read with timeout (command mode / handshake)
//
//---------------------------------------------------------------------------
static int JoyportU_ReadByte(BYTE* out, int timeout_us)
{
    int waited = 0;
    while (joyport_device.fd >= 0) {
        ssize_t n = read(joyport_device.fd, out, 1);
        if (n == 1) return 1;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return 0;
        if (waited >= timeout_us) return 0;
        usleep(100);
        waited += 100;
    }
    return 0;
}

//---------------------------------------------------------------------------
//
//	JoyportU receive thread
//
//---------------------------------------------------------------------------
static void* JoyportU_ReceiveThread(void* arg)
{
    BYTE buffer;
    while (joyport_device.active) {
        ssize_t n = read(joyport_device.fd, &buffer, 1);
        if (n > 0) {
            pthread_mutex_lock(&joyport_device.mutex);
            
            // Handle end marks (0x30-0x3F)
            if ((buffer & 0xF0) == 0x30) {
                joyport_device.recv_data = buffer;
                joyport_device.recv_end_mark = 1;
            }
            
            // Handle notify mode data
            if (joyport_device.notify_mode) {
                if ((buffer & 0x90) == 0x80) {
                    // Port A data (0x3A) - Joystick 1
                    joyport_device.port_a = buffer | 0x90;
                    // Convert ATARI joystick data to X68000 format
                    BYTE joy_data = ~(buffer | 0x90);  // Invert for X68000 positive logic
                    X68000_Joystick_Set(0, joy_data);
                } else if ((buffer & 0x90) == 0x90) {
                    // Port B data (0x3B) - Joystick 2  
                    joyport_device.port_b = buffer | 0x90;
                    // Convert ATARI joystick data to X68000 format
                    BYTE joy_data = ~(buffer | 0x90);  // Invert for X68000 positive logic
                    X68000_Joystick_Set(1, joy_data);
                } else if ((buffer & 0xF0) == 0x00) {
                    // Port C data (0x3C)
                    joyport_device.port_c = buffer << 4;
                }
            }
            
            pthread_mutex_unlock(&joyport_device.mutex);
        }
        usleep(1000); // 1ms delay
    }
    return NULL;
}

//---------------------------------------------------------------------------
//
//	Initialize JoyportU device
//
//---------------------------------------------------------------------------
static int InitJoyportU(int notify_mode)
{
    char* device_path = FindJoyportUDevice();
    if (!device_path) return 0;
    
    printf("Found JoyportU device at: %s\n", device_path);
    
    joyport_device.fd = open(device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    free(device_path);
    
    if (joyport_device.fd < 0) return 0;
    
    // Configure serial port
    struct termios tty;
    if (tcgetattr(joyport_device.fd, &tty) < 0) {
        close(joyport_device.fd);
        joyport_device.fd = -1;
        return 0;
    }
    
    // Set baud rate to 480M baud (12MHz * 40) - closest available
    cfsetispeed(&tty, B230400);
    cfsetospeed(&tty, B230400);
    
    tty.c_cflag &= ~PARENB;  // No parity
    tty.c_cflag &= ~CSTOPB;  // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;      // 8 data bits
    tty.c_cflag |= CREAD | CLOCAL;
    
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    
    if (tcsetattr(joyport_device.fd, TCSANOW, &tty) < 0) {
        close(joyport_device.fd);
        joyport_device.fd = -1;
        return 0;
    }
    
    joyport_device.notify_mode = notify_mode;
    joyport_device.old_command_mode = 0;
    joyport_device.recv_end_mark = 0;
    cmd_prefetch_len = 0;
    cmd_prefetch_pos = 0;
    last_joyport_cmd = 0;
    cmd_timeout_log_count = 0;
    cmd_read_log_count = 0;
    ppi_control = 0x92;

    BYTE init_cmd;
    if (notify_mode) {
        init_cmd = 0x31;
    } else {
        init_cmd = 0x32;
        joyport_device.old_command_mode = 1;
    }

    os_log_info(JoyportULog(), "send init cmd=0x%{public}02X notify=%{public}d",
                init_cmd, notify_mode);
    if (write(joyport_device.fd, &init_cmd, 1) != 1) {
        os_log_error(JoyportULog(), "init write failed");
        if (joyport_device.rx_thread_running) {
            joyport_device.active = 0;
            pthread_join(joyport_device.rx_thread, NULL);
            joyport_device.rx_thread_running = 0;
        }
        close(joyport_device.fd);
        joyport_device.fd = -1;
        return 0;
    }

    if (notify_mode) {
        // Keep the historically working permissive handshake: give the
        // device time to answer, then let the receive thread take over.
        usleep(100000); // 100ms

        joyport_device.active = 1;
        if (pthread_create(&joyport_device.rx_thread, NULL, JoyportU_ReceiveThread, NULL) != 0) {
            close(joyport_device.fd);
            joyport_device.fd = -1;
            joyport_device.active = 0;
            return 0;
        }
        joyport_device.rx_thread_running = 1;
        printf("JoyportU initialized in notify mode\n");
        os_log_info(JoyportULog(), "initialized notify mode");
    } else {
        // Command mode (0x32) may not produce an immediate ack; some units
        // return the 0x32 end mark with the first 0x3A read. CmdRead skips
        // end marks before returning joystick data.
        usleep(100000);
        tcflush(joyport_device.fd, TCIFLUSH);
        joyport_device.active = 1;
        printf("JoyportU initialized in command mode (0x32 protocol)\n");
        os_log_info(JoyportULog(), "initialized command mode protocol=0x32");
    }
    return 1;
}

//---------------------------------------------------------------------------
//
//	Stop JoyportU device
//
//---------------------------------------------------------------------------
static void StopJoyportU(void)
{
    if (joyport_device.active) {
        joyport_device.active = 0;
        if (joyport_device.rx_thread_running) {
            pthread_join(joyport_device.rx_thread, NULL);
            joyport_device.rx_thread_running = 0;
        }
        if (joyport_device.fd >= 0) {
            close(joyport_device.fd);
            joyport_device.fd = -1;
        }
    }
    cmd_prefetch_len = 0;
    cmd_prefetch_pos = 0;
}

//---------------------------------------------------------------------------
//
//	Execute JoyportU command
//
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//
//	Send data to JoyportU
//
//---------------------------------------------------------------------------
static int SendJoyportUData(BYTE cmd, BYTE data)
{
    if (!joyport_device.active) return 0;
    
    BYTE send_byte = 0;
    
    // Convert commands based on ppi_8255.h protocol
    switch (cmd) {
        case 0x4A:
            send_byte = (data & 0x6F) | 0x80;
            break;
        case 0x4B:
            send_byte = (data & 0x6F) | 0x90;
            break;
        case 0x4C:
            send_byte = data >> 4;
            break;
        case 0x4D:
            if (data & 0x80) {
                send_byte = ((data & 0x78) >> 1) | (data & 0x03) | 0x40;
            } else {
                // Port C bit set/reset: suppress duplicate sends, matching
                // the ppi_8255.h reference (ADPCM pan control hits this path
                // frequently via $E9A007)
                static BYTE last_bit_setreset = 0xFF;
                if (data == last_bit_setreset) {
                    last_joyport_cmd = cmd;
                    return 1;
                }
                last_bit_setreset = data;
                send_byte = (data & 0x0F) | 0x10;
            }
            break;
        default:
            return 0;
    }
    
    if (!joyport_device.notify_mode) {
        // Command mode - send without waiting for an ack
        if (write(joyport_device.fd, &send_byte, 1) != 1) {
            last_joyport_cmd = cmd;
            return 0;
        }
        // REQ line toggle (port C bit 4 set/reset, used by analog sticks):
        // v2 firmware answers with a status byte; 0xFF invalidates the
        // port A prefetch (reference: "STAT LUSTER" workaround)
        if (!joyport_device.old_command_mode && cmd == 0x4D &&
            (data == 0x08 || data == 0x09)) {
            BYTE status = 0;
            if (JoyportU_ReadByte(&status, 5000)) {
                if (status == 0xFF && cmd_prefetch_len > 0) {
                    if (last_joyport_cmd == 0x4D || cmd_prefetch_pos >= 35) {
                        cmd_prefetch_len = 0;
                        cmd_prefetch_pos = 0;
                    }
                }
            } else {
                printf("JoyportU REQ status read timeout\n");
            }
        }
        last_joyport_cmd = cmd;
        return 1;
    } else {
        // Notify mode - send and wait for acknowledgment
        pthread_mutex_lock(&joyport_device.mutex);
        joyport_device.recv_end_mark = 0;   // discard stale end marks
        pthread_mutex_unlock(&joyport_device.mutex);
        if (write(joyport_device.fd, &send_byte, 1) == 1) {
            // Wait for acknowledgment
            for (int i = 0; i < 50000; i++) {
                pthread_mutex_lock(&joyport_device.mutex);
                if (joyport_device.recv_end_mark) {
                    joyport_device.recv_end_mark = 0;   // consume the ack
                    pthread_mutex_unlock(&joyport_device.mutex);
                    last_joyport_cmd = cmd;
                    return 1;
                }
                pthread_mutex_unlock(&joyport_device.mutex);
                usleep(10); // 10µs delay
            }
        }
        // Timeout
        StopJoyportU();
        printf("JoyportU send timeout - device deactivated\n");
        return 0;
    }
}

//---------------------------------------------------------------------------
//
//	Initialize PPI
//
//---------------------------------------------------------------------------
void PPI_Init(void)
{
	joyport_ukun_mode = 0;
	ppi_porta = 0xFF;
	ppi_portb = 0xFF;
	ppi_portc = 0x00;
	ppi_control = 0x92;
	
	// Initialize JoyportU device state
	joyport_device.active = 0;
	joyport_device.fd = -1;
	joyport_device.notify_mode = 0;
	joyport_device.old_command_mode = 0;
	joyport_device.rx_thread_running = 0;
	joyport_device.recv_end_mark = 0;
	joyport_device.port_a = 0xFF;
	joyport_device.port_b = 0xFF;
	joyport_device.port_c = 0x00;
	cmd_prefetch_len = 0;
	cmd_prefetch_pos = 0;
	cmd_timeout_log_count = 0;
	cmd_read_log_count = 0;
	last_joyport_cmd = 0;
}

//---------------------------------------------------------------------------
//
//	Cleanup PPI
//
//---------------------------------------------------------------------------
void PPI_Cleanup(void)
{
	StopJoyportU();
}

//---------------------------------------------------------------------------
//
//	Reset PPI
//
//---------------------------------------------------------------------------
void PPI_Reset(void)
{
	ppi_portc = 0x00;
	ppi_control = 0x92;
	
	// Stop current JoyportU connection
	StopJoyportU();
	
	// Initialize JoyportU based on mode
	switch (joyport_ukun_mode) {
		case 1:
			// Notify mode
			if (InitJoyportU(1)) {
				printf("JoyportU activated in notify mode\n");
			} else {
				printf("Failed to initialize JoyportU in notify mode\n");
			}
			break;
		case 2:
			// Command mode
			if (InitJoyportU(0)) {
				printf("JoyportU activated in command mode\n");
			} else {
				printf("Failed to initialize JoyportU in command mode\n");
			}
			break;
		default:
			// Disabled
			printf("JoyportU disabled\n");
			break;
	}
}

//---------------------------------------------------------------------------
//
//	Read from PPI
//
//---------------------------------------------------------------------------
BYTE FASTCALL PPI_Read(DWORD addr)
{
	// Only odd addresses are decoded
	if ((addr & 1) == 0) {
		return 0xFF;
	}
	
	// 8-byte unit loop
	addr &= 7;
	addr >>= 1;
	
	switch (addr) {
		case 0:  // Port A
			if (joyport_device.active) {
				pthread_mutex_lock(&joyport_device.mutex);
				BYTE data = joyport_device.port_a;
				pthread_mutex_unlock(&joyport_device.mutex);
				return data;
			}
			return ppi_porta;
			
		case 1:  // Port B
			if (joyport_device.active) {
				pthread_mutex_lock(&joyport_device.mutex);
				BYTE data = joyport_device.port_b;
				pthread_mutex_unlock(&joyport_device.mutex);
				return data;
			}
			return ppi_portb;
			
		case 2:  // Port C
			if (joyport_device.active) {
				pthread_mutex_lock(&joyport_device.mutex);
				BYTE data = joyport_device.port_c | (ppi_portc & 0x0F);
				pthread_mutex_unlock(&joyport_device.mutex);
				return data;
			}
			return ppi_portc;
			
		default:
			return 0xFF;
	}
}

//---------------------------------------------------------------------------
//
//	Write to PPI
//
//---------------------------------------------------------------------------
void FASTCALL PPI_Write(DWORD addr, BYTE data)
{
	// Only odd addresses are decoded
	if ((addr & 1) == 0) {
		return;
	}
	
	// 8-byte unit loop
	addr &= 7;
	addr >>= 1;
	
	switch (addr) {
		case 0:  // Port A
			if (joyport_device.active) {
				SendJoyportUData(0x4A, data);
			}
			ppi_porta = data;
			break;
			
		case 1:  // Port B
			if (joyport_device.active) {
				SendJoyportUData(0x4B, data);
			}
			ppi_portb = data;
			break;
			
		case 2:  // Port C
			if (joyport_device.active) {
				// SendJoyportUData extracts the upper 4 bits itself;
				// pass the raw value (matches ppi_8255.h reference)
				SendJoyportUData(0x4C, data);
			}
			ppi_portc = data;   // output latch cache (reference: m_portC)
			break;
			
		case 3:  // Control register
			if (joyport_device.active) {
				// Forward both control-word and bit set/reset forms;
				// SendJoyportUData converts each per the protocol
				SendJoyportUData(0x4D, data);
			}
			if (data < 0x80) {
				// Bit set/reset mode
				int bit = (data >> 1) & 0x07;
				BYTE mask = 1 << bit;
				if (data & 1) {
					ppi_portc |= mask;
				} else {
					ppi_portc &= ~mask;
				}
			} else {
				// Direction/mode control word — command-mode reads use it
				// to decide which ports are device inputs
				ppi_control = data;
			}
			break;
	}
}

//---------------------------------------------------------------------------
//
//	Set JoyportU mode
//
//---------------------------------------------------------------------------
void PPI_SetJoyportUMode(int mode)
{
	joyport_ukun_mode = mode;
	PPI_Reset();  // Reset to apply new mode
}

//---------------------------------------------------------------------------
//
//	Get JoyportU mode
//
//---------------------------------------------------------------------------
int PPI_GetJoyportUMode(void)
{
	return joyport_ukun_mode;
}

//---------------------------------------------------------------------------
//
//	Query whether a JoyportU device is connected in command mode
//
//---------------------------------------------------------------------------
int PPI_JoyportU_InCommandMode(void)
{
	return joyport_device.active && !joyport_device.notify_mode;
}

//---------------------------------------------------------------------------
//
//	Command-mode port read (synchronous 0x3A/0x3B/0x3C query round-trip,
//	mirroring ExecCmd in the ppi_8255.h reference).
//	port: 0=A, 1=B, 2=C.  Returns 0-255, or -1 when the caller should use
//	the internal/emulated value instead (device inactive, port is an
//	output per the control word, or communication failure).
//
//---------------------------------------------------------------------------
int PPI_JoyportU_CmdRead(int port)
{
	BYTE cmd;

	if (!joyport_device.active || joyport_device.notify_mode) {
		return -1;
	}

	// 8255 direction check: output ports read back the internal latch,
	// which pia.c owns — only query the device for input ports
	switch (port) {
		case 0:
			if (!(ppi_control & 0x10)) return -1;
			cmd = 0x3A;
			break;
		case 1:
			// JoyportU is currently wired as the 1P joyport. Polling 0x3B
			// produces spurious 2P input on some units and can stall reads.
			return 0xFF;
		case 2:
			if (!(ppi_control & 0x08)) return -1;
			cmd = 0x3C;
			break;
		default:
			return -1;
	}

	// Drain the port A burst prefetch first (v2 protocol)
	if (cmd == 0x3A && cmd_prefetch_len > 0) {
		if (cmd_prefetch_pos < cmd_prefetch_len) {
			last_joyport_cmd = cmd;
			return cmd_prefetch[cmd_prefetch_pos++];
		}
		cmd_prefetch_len = 0;
		cmd_prefetch_pos = 0;
	}

	if (write(joyport_device.fd, &cmd, 1) != 1) {
		printf("JoyportU command read send failed - device deactivated\n");
		StopJoyportU();
		return -1;
	}

	BYTE b1 = 0;
	int got_data = 0;
	for (int attempt = 0; attempt < 8; attempt++) {
		if (!JoyportU_ReadByte(&b1, 5000)) {
			break;
		}
		if ((b1 & 0xF0) == 0x30) {
			if (cmd_read_log_count < 32) {
				os_log_info(JoyportULog(),
				            "command read skipped endmark cmd=0x%{public}02X mark=0x%{public}02X",
				            cmd, b1);
				cmd_read_log_count++;
			}
			continue;
		}
		got_data = 1;
		break;
	}
	if (!got_data) {
		if (cmd_timeout_log_count < 8) {
			printf("JoyportU command read timeout (cmd=0x%02X)\n", cmd);
			os_log_error(JoyportULog(), "command read timeout cmd=0x%{public}02X port=%{public}d",
			             cmd, port);
			cmd_timeout_log_count++;
		}
		switch (port) {
			case 0:
				return joyport_device.port_a;
			case 1:
				return joyport_device.port_b;
			case 2:
				return joyport_device.port_c;
			default:
				return -1;
		}
	}

	// Port A burst response (v2 protocol): a leading length byte
	// (> 0, high bit clear) is followed by that many data bytes which
	// are prefetched and drained by subsequent port A reads
	if (cmd == 0x3A && !joyport_device.old_command_mode &&
	    (b1 & 0x80) == 0 && b1 > 0) {
		int len = b1;
		int got = 0;
		while (got < len) {
			BYTE d = 0;
			if (!JoyportU_ReadByte(&d, 5000)) {
				if (cmd_timeout_log_count < 8) {
					printf("JoyportU burst read timeout (%d/%d)\n", got, len);
					os_log_error(JoyportULog(),
					             "burst read timeout got=%{public}d len=%{public}d",
					             got, len);
					cmd_timeout_log_count++;
				}
				return joyport_device.port_a;
			}
			cmd_prefetch[got++] = d;
		}
		cmd_prefetch_len = len;
		cmd_prefetch_pos = 1;
		last_joyport_cmd = cmd;
		joyport_device.port_a = cmd_prefetch[0];
		if (cmd_read_log_count < 32) {
			os_log_info(JoyportULog(),
			            "command read burst cmd=0x%{public}02X len=%{public}d first=0x%{public}02X",
			            cmd, len, cmd_prefetch[0]);
			cmd_read_log_count++;
		}
		return cmd_prefetch[0];
	}

	switch (port) {
		case 0:
			joyport_device.port_a = b1;
			break;
		case 1:
			joyport_device.port_b = b1;
			break;
		case 2:
			joyport_device.port_c = b1;
			break;
		default:
			break;
	}

	last_joyport_cmd = cmd;
	if (cmd_read_log_count < 32) {
		os_log_info(JoyportULog(), "command read cmd=0x%{public}02X port=%{public}d data=0x%{public}02X",
		            cmd, port, b1);
		cmd_read_log_count++;
	}
	return b1;
}
