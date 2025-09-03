#ifndef _winx68k_scc_enhanced
#define _winx68k_scc_enhanced

#include "common.h"
#include <pthread.h>
#include <termios.h>

// Original SCC functions
void SCC_IntCheck(void);
void SCC_Init(void);
BYTE FASTCALL SCC_Read(DWORD adr);
void FASTCALL SCC_Write(DWORD adr, BYTE data);

// Enhanced serial communication
typedef enum {
    SCC_MODE_MOUSE_ONLY = 0,
    SCC_MODE_SERIAL_PTY = 1,
    SCC_MODE_SERIAL_TCP = 2,
    SCC_MODE_SERIAL_FILE = 3
} SCC_Mode;

typedef struct {
    SCC_Mode mode;
    int master_fd;
    char slave_path[256];
    pthread_t rx_thread;
    pthread_mutex_t tx_mutex;
    pthread_mutex_t rx_mutex;

    BYTE tx_buffer[1024];
    int tx_head, tx_tail;

    BYTE rx_buffer[1024];
    int rx_head, rx_tail;

    int baud_rate;
    int data_bits;
    int stop_bits;
    int parity; // 0=none, 1=odd, 2=even

    BYTE tx_ready;
    BYTE rx_ready;
    BYTE connected;
} SCC_SerialPort;

// New APIs
int SCC_SetMode(SCC_Mode mode, const char* config);
int SCC_CreatePTY(void);
int SCC_ConnectTCP(const char* host, int port);
int SCC_OpenFile(const char* path);
void SCC_CloseSerial(void);
int SCC_SendByte(BYTE data);
int SCC_ReceiveByte(BYTE* data);
void* SCC_ReceiveThread(void* arg);
int SCC_ConfigureSerial(int baud, int data_bits, int stop_bits, int parity);

// Globals
extern SCC_SerialPort scc_port_a;
extern SCC_SerialPort scc_port_b;

// Mouse compatibility
extern signed char MouseX;
extern signed char MouseY;
extern BYTE MouseSt;

#endif
