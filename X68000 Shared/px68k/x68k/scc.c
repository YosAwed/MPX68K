// ---------------------------------------------------------------------------------------
//  SCC_ENHANCED.C - Z8530 SCC with Real Serial Communication Support
//  Integrated enhanced version supporting PTY, TCP, and real serial ports
// ---------------------------------------------------------------------------------------

#include "common.h"
#include "scc.h"
#include "m68000.h"
#include "irqh.h"
#include "mouse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <pthread.h>

// Mouse compatibility variables
signed char MouseX = 0;
signed char MouseY = 0;
BYTE MouseSt = 0;

// Original SCC registers/state
BYTE SCC_RegsA[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
BYTE SCC_RegNumA = 0;
BYTE SCC_RegSetA = 0;
BYTE SCC_RegsB[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
BYTE SCC_RegNumB = 0;
BYTE SCC_RegSetB = 0;
BYTE SCC_Vector = 0;
BYTE SCC_Dat[3] = {0, 0, 0};
BYTE SCC_DatNum = 0;

// Enhanced serial ports
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
    BYTE tx_buffer[1024]; int tx_head, tx_tail;
    BYTE rx_buffer[1024]; int rx_head, rx_tail;
    int baud_rate; int data_bits; int stop_bits; int parity;
    BYTE tx_ready; BYTE rx_ready; BYTE connected;
} SCC_SerialPort;
static SCC_SerialPort scc_port_a;
static SCC_SerialPort scc_port_b;

static DWORD FASTCALL SCC_Int(BYTE irq)
{
    DWORD ret = (DWORD)(-1);
    IRQH_IRQCallBack(irq);
    if ( (irq==5)&&(!(SCC_RegsB[9]&2)) ) {
        if (SCC_RegsB[9]&1) {
            if (SCC_RegsB[9]&0x10)
                ret = ((DWORD)(SCC_Vector&0x8f)+0x20);
            else
                ret = ((DWORD)(SCC_Vector&0xf1)+4);
        } else {
            ret = ((DWORD)SCC_Vector);
        }
    }
    return ret;
}

void SCC_IntCheck(void)
{
    if (scc_port_a.mode == SCC_MODE_MOUSE_ONLY) {
        if ( (SCC_DatNum) && ((SCC_RegsB[1]&0x18)==0x10) && (SCC_RegsB[9]&0x08) ) {
            IRQH_Int(5, &SCC_Int);
        } else if ( (SCC_DatNum==3) && ((SCC_RegsB[1]&0x18)==0x08) && (SCC_RegsB[9]&0x08) ) {
            IRQH_Int(5, &SCC_Int);
        }
    } else {
        if (scc_port_a.rx_ready && ((SCC_RegsB[1]&0x18)==0x10) && (SCC_RegsB[9]&0x08)) {
            IRQH_Int(5, &SCC_Int);
        }
    }
}

static int SCC_CreatePTY(void)
{
    int master_fd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (master_fd < 0) return -1;
    if (grantpt(master_fd) < 0) { close(master_fd); return -1; }
    if (unlockpt(master_fd) < 0) { close(master_fd); return -1; }
    char* slave_name = ptsname(master_fd);
    if (!slave_name) { close(master_fd); return -1; }

    scc_port_a.master_fd = master_fd;
    strncpy(scc_port_a.slave_path, slave_name, sizeof(scc_port_a.slave_path)-1);
    scc_port_a.slave_path[sizeof(scc_port_a.slave_path)-1] = '\0';
    scc_port_a.connected = 1;
    int flags = fcntl(master_fd, F_GETFL);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Debug: Print the PTY slave path
    printf("SCC_CreatePTY: Created PTY with slave path: %s\n", scc_port_a.slave_path);
    
    return master_fd;
}

static int SCC_ConnectTCP(const char* host, int port)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) return -1;
    struct sockaddr_in server_addr; memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) { close(sock_fd); return -1; }
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) { close(sock_fd); return -1; }
    scc_port_a.master_fd = sock_fd; scc_port_a.connected = 1;
    int flags = fcntl(sock_fd, F_GETFL); fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);
    return sock_fd;
}

static int SCC_OpenFile(const char* path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios tty; if (tcgetattr(fd, &tty) < 0) { close(fd); return -1; }
    cfmakeraw(&tty); cfsetispeed(&tty, B9600); cfsetospeed(&tty, B9600);
    tty.c_cflag &= ~PARENB; tty.c_cflag &= ~CSTOPB; tty.c_cflag &= ~CSIZE; tty.c_cflag |= CS8;
    if (tcsetattr(fd, TCSANOW, &tty) < 0) { close(fd); return -1; }
    scc_port_a.master_fd = fd;
    strncpy(scc_port_a.slave_path, path, sizeof(scc_port_a.slave_path)-1);
    scc_port_a.slave_path[sizeof(scc_port_a.slave_path)-1] = '\0';
    scc_port_a.connected = 1; return fd;
}

static void* SCC_ReceiveThread(void* arg)
{
    SCC_SerialPort* port = (SCC_SerialPort*)arg; BYTE buffer[256]; ssize_t n;
    while (port->connected) {
        n = read(port->master_fd, buffer, sizeof(buffer));
        if (n > 0) {
            pthread_mutex_lock(&port->rx_mutex);
            for (ssize_t i = 0; i < n; i++) {
                int next_head = (port->rx_head + 1) % (int)sizeof(port->rx_buffer);
                if (next_head != port->rx_tail) { port->rx_buffer[port->rx_head] = buffer[i]; port->rx_head = next_head; port->rx_ready = 1; }
            }
            pthread_mutex_unlock(&port->rx_mutex);
            SCC_IntCheck();
        }
        usleep(1000);
    }
    return NULL;
}

int SCC_SetMode(int imode, const char* config)
{
    SCC_Mode mode = (SCC_Mode)imode;
    SCC_CloseSerial(); scc_port_a.mode = mode;
    switch (mode) {
        case SCC_MODE_MOUSE_ONLY: return 0;
        case SCC_MODE_SERIAL_PTY: if (SCC_CreatePTY() < 0) return -1; break;
        case SCC_MODE_SERIAL_TCP: {
            if (!config) return -1; char host[256]; int port;
            if (sscanf(config, "%255[^:]:%d", host, &port) != 2) return -1;
            if (SCC_ConnectTCP(host, port) < 0) return -1; break; }
        case SCC_MODE_SERIAL_FILE: if (!config || SCC_OpenFile(config) < 0) return -1; break;
        default: return -1;
    }
    if (mode != SCC_MODE_MOUSE_ONLY) {
        pthread_mutex_init(&scc_port_a.tx_mutex, NULL);
        pthread_mutex_init(&scc_port_a.rx_mutex, NULL);
        if (pthread_create(&scc_port_a.rx_thread, NULL, SCC_ReceiveThread, &scc_port_a) != 0) { SCC_CloseSerial(); return -1; }
    }
    return 0;
}

void SCC_CloseSerial(void)
{
    if (scc_port_a.connected) {
        scc_port_a.connected = 0;
        if (scc_port_a.mode != SCC_MODE_MOUSE_ONLY) {
            pthread_join(scc_port_a.rx_thread, NULL);
            pthread_mutex_destroy(&scc_port_a.tx_mutex);
            pthread_mutex_destroy(&scc_port_a.rx_mutex);
        }
        if (scc_port_a.master_fd >= 0) { close(scc_port_a.master_fd); scc_port_a.master_fd = -1; }
        // Clear slave path
        scc_port_a.slave_path[0] = '\0';
    }
}

const char* SCC_GetSlavePath(void)
{
    if (scc_port_a.connected && scc_port_a.mode == SCC_MODE_SERIAL_PTY && scc_port_a.slave_path[0] != '\0') {
        return scc_port_a.slave_path;
    }
    return NULL;
}

int SCC_SendByte(BYTE data)
{
    if (!scc_port_a.connected || scc_port_a.mode == SCC_MODE_MOUSE_ONLY) return -1;
    pthread_mutex_lock(&scc_port_a.tx_mutex);
    ssize_t w = write(scc_port_a.master_fd, &data, 1);
    pthread_mutex_unlock(&scc_port_a.tx_mutex);
    return (w == 1) ? 0 : -1;
}

int SCC_ReceiveByte(BYTE* data)
{
    if (!scc_port_a.connected || scc_port_a.mode == SCC_MODE_MOUSE_ONLY) return -1;
    pthread_mutex_lock(&scc_port_a.rx_mutex);
    if (scc_port_a.rx_head != scc_port_a.rx_tail) {
        *data = scc_port_a.rx_buffer[scc_port_a.rx_tail];
        scc_port_a.rx_tail = (scc_port_a.rx_tail + 1) % (int)sizeof(scc_port_a.rx_buffer);
        if (scc_port_a.rx_head == scc_port_a.rx_tail) scc_port_a.rx_ready = 0;
        pthread_mutex_unlock(&scc_port_a.rx_mutex); return 0;
    }
    pthread_mutex_unlock(&scc_port_a.rx_mutex); return -1;
}

void SCC_Init(void)
{
    MouseX = 0; MouseY = 0; MouseSt = 0;
    SCC_RegNumA = SCC_RegSetA = SCC_RegNumB = SCC_RegSetB = 0; SCC_Vector = 0; SCC_DatNum = 0;
    memset(&scc_port_a, 0, sizeof(scc_port_a)); memset(&scc_port_b, 0, sizeof(scc_port_b));
    scc_port_a.mode = SCC_MODE_MOUSE_ONLY; scc_port_a.master_fd = -1;
    scc_port_a.baud_rate = 9600; scc_port_a.data_bits = 8; scc_port_a.stop_bits = 1; scc_port_a.parity = 0;
    scc_port_a.slave_path[0] = '\0';  // Explicitly clear slave path
}

void FASTCALL SCC_Write(DWORD adr, BYTE data)
{
    if (adr>=0xe98008) return;
    if ((adr&7) == 1) {
        if (SCC_RegSetB) {
            if (SCC_RegNumB == 5) {
                if (scc_port_a.mode == SCC_MODE_MOUSE_ONLY) {
                    if ( (!(SCC_RegsB[5]&2)) && (data&2) && (SCC_RegsB[3]&1) && (!SCC_DatNum) ) {
                        Mouse_SetData(); SCC_DatNum = 3; SCC_Dat[2] = MouseSt; SCC_Dat[1] = MouseX; SCC_Dat[0] = MouseY;
                    }
                } else {
                    if ((data & 2) && scc_port_a.connected) { scc_port_a.tx_ready = 1; }
                }
            } else if (SCC_RegNumB == 2) {
                SCC_Vector = data;
            }
            SCC_RegSetB = 0; SCC_RegsB[SCC_RegNumB] = data; SCC_RegNumB = 0;
        } else {
            if (!(data&0xf0)) { data &= 15; SCC_RegSetB = 1; SCC_RegNumB = data; }
            else { SCC_RegSetB = 0; SCC_RegNumB = 0; }
        }
    } else if ((adr&7) == 3) {
        if (scc_port_a.mode != SCC_MODE_MOUSE_ONLY && scc_port_a.connected) { SCC_SendByte(data); }
    } else if ((adr&7) == 5) {
        if (SCC_RegSetA) {
            SCC_RegSetA = 0; switch (SCC_RegNumA) { case 2: SCC_RegsB[2] = data; SCC_Vector = data; break; case 9: SCC_RegsB[9] = data; break; }
        } else {
            data &= 15; if (data) { SCC_RegSetA = 1; SCC_RegNumA = data; } else { SCC_RegSetA = 0; SCC_RegNumA = 0; }
        }
    } else if ((adr&7) == 7) {
        if (scc_port_a.mode != SCC_MODE_MOUSE_ONLY && scc_port_a.connected) { SCC_SendByte(data); }
    }
}

BYTE FASTCALL SCC_Read(DWORD adr)
{
    BYTE ret = 0; if (adr>=0xe98008) return ret;
    if ((adr&7) == 1) {
        if (!SCC_RegNumB) {
            if (scc_port_a.mode == SCC_MODE_MOUSE_ONLY) { ret = (SCC_DatNum ? 1 : 0); }
            else { ret = 0; if (scc_port_a.tx_ready) ret |= 4; if (scc_port_a.rx_ready) ret |= 1; }
        }
        SCC_RegNumB = 0; SCC_RegSetB = 0;
    } else if ((adr&7) == 3) {
        if (scc_port_a.mode == SCC_MODE_MOUSE_ONLY) {
            if (SCC_DatNum) { SCC_DatNum--; ret = SCC_Dat[SCC_DatNum]; }
        } else {
            BYTE d; if (SCC_ReceiveByte(&d) == 0) ret = d;
        }
    } else if ((adr&7) == 5) {
        switch (SCC_RegNumA) {
            case 0:
                if (scc_port_a.mode == SCC_MODE_MOUSE_ONLY) ret = 4;
                else { ret = 0; if (scc_port_a.tx_ready) ret |= 4; if (scc_port_a.rx_ready) ret |= 1; }
                break;
            case 3:
                if (scc_port_a.mode == SCC_MODE_MOUSE_ONLY) ret = (SCC_DatNum ? 4 : 0);
                else ret = (scc_port_a.rx_ready ? 4 : 0);
                break;
        }
        SCC_RegNumA = 0; SCC_RegSetA = 0;
    } else if ((adr&7) == 7) {
        if (scc_port_a.mode != SCC_MODE_MOUSE_ONLY) { BYTE d; if (SCC_ReceiveByte(&d) == 0) ret = d; }
    }
    return ret;
}
