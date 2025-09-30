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
} JoyportU_Device;

static JoyportU_Device joyport_device = {-1, 0, PTHREAD_MUTEX_INITIALIZER, 0, 0, 0xFF, 0xFF, 0x00, 0, 0};

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
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matchDict, &iterator) != KERN_SUCCESS) {
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
    
    joyport_device.active = 1;
    joyport_device.notify_mode = notify_mode;
    joyport_device.recv_end_mark = 0;
    
    // Send initialization command
    BYTE init_cmd = notify_mode ? 0x31 : 0x32;  // 0x31: notify mode, 0x32: command mode
    write(joyport_device.fd, &init_cmd, 1);
    
    // Wait for response
    usleep(100000); // 100ms
    
    // Start receive thread
    if (pthread_create(&joyport_device.rx_thread, NULL, JoyportU_ReceiveThread, NULL) != 0) {
        close(joyport_device.fd);
        joyport_device.fd = -1;
        joyport_device.active = 0;
        return 0;
    }
    
    printf("JoyportU initialized in %s mode\n", notify_mode ? "notify" : "command");
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
        if (joyport_device.fd >= 0) {
            pthread_join(joyport_device.rx_thread, NULL);
            close(joyport_device.fd);
            joyport_device.fd = -1;
        }
    }
}

//---------------------------------------------------------------------------
//
//	Execute JoyportU command
//
//---------------------------------------------------------------------------
static int ExecJoyportUCmd(BYTE cmd)
{
    if (!joyport_device.active) return -1;
    
    if (joyport_device.notify_mode) {
        // In notify mode, return cached data
        pthread_mutex_lock(&joyport_device.mutex);
        switch (cmd) {
            case 0x3A:
                {
                    BYTE data = joyport_device.port_a;
                    pthread_mutex_unlock(&joyport_device.mutex);
                    return data;
                }
            case 0x3B:
                {
                    BYTE data = joyport_device.port_b;
                    pthread_mutex_unlock(&joyport_device.mutex);
                    return data;
                }
            case 0x3C:
                {
                    BYTE data = joyport_device.port_c;
                    pthread_mutex_unlock(&joyport_device.mutex);
                    return data;
                }
            default:
                pthread_mutex_unlock(&joyport_device.mutex);
                return 0;
        }
    } else {
        // In command mode, send command and wait for response
        joyport_device.recv_end_mark = 0;
        if (write(joyport_device.fd, &cmd, 1) == 1) {
            // Wait for response with timeout
            for (int i = 0; i < 50000; i++) {
                pthread_mutex_lock(&joyport_device.mutex);
                if (joyport_device.recv_end_mark) {
                    BYTE data = joyport_device.recv_data;
                    pthread_mutex_unlock(&joyport_device.mutex);
                    return data;
                }
                pthread_mutex_unlock(&joyport_device.mutex);
                usleep(10); // 10µs delay
            }
        }
        // Timeout - deactivate device
        StopJoyportU();
        printf("JoyportU command timeout - device deactivated\n");
        return -1;
    }
}

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
                send_byte = (data & 0x0F) | 0x10;
            }
            break;
        default:
            return 0;
    }
    
    if (!joyport_device.notify_mode) {
        // Command mode - just send
        return (write(joyport_device.fd, &send_byte, 1) == 1) ? 1 : 0;
    } else {
        // Notify mode - send and wait for acknowledgment
        if (write(joyport_device.fd, &send_byte, 1) == 1) {
            // Wait for acknowledgment
            for (int i = 0; i < 50000; i++) {
                pthread_mutex_lock(&joyport_device.mutex);
                if (joyport_device.recv_end_mark) {
                    pthread_mutex_unlock(&joyport_device.mutex);
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
	
	// Initialize JoyportU device state
	joyport_device.active = 0;
	joyport_device.fd = -1;
	joyport_device.notify_mode = 0;
	joyport_device.recv_end_mark = 0;
	joyport_device.port_a = 0xFF;
	joyport_device.port_b = 0xFF;
	joyport_device.port_c = 0x00;
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
				// Send upper 4 bits to JoyportU
				SendJoyportUData(0x4C, data >> 4);
				// Keep lower 4 bits for internal use
				ppi_portc = (ppi_portc & 0xF0) | (data & 0x0F);
			} else {
				ppi_portc = data;
			}
			break;
			
		case 3:  // Control register
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
				// Mode control (not fully implemented)
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

