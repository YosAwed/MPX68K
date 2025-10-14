// ---------------------------------------------------------------------------------------
//  SCC_ENHANCED.C - Z8530 SCC with Real Serial Communication Support
//  X68000 hardware-compliant version: Port A for Mouse, Port B for Serial
//  
//  CHANGES:
//  - Port A: Mouse only (always enabled)
//  - Port B: Serial communication (PTY, TCP, File)
//  - Both ports operate independently and simultaneously
//  - Double-click detection fixed
// ---------------------------------------------------------------------------------------

#include "../m68000/common.h"
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
#include <sys/time.h>
#include <sys/termios.h>
#include <pthread.h>

// ============================================================================
// Port A: Mouse (always enabled)
// ============================================================================
signed char MouseX = 0;
signed char MouseY = 0;
BYTE MouseSt = 0;
BYTE SCC_Dat[3] = {0, 0, 0};
BYTE SCC_DatNum = 0;

// ============================================================================
// SCC Registers
// ============================================================================
BYTE SCC_RegsA[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
BYTE SCC_RegNumA = 0;
BYTE SCC_RegSetA = 0;
BYTE SCC_RegsB[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
BYTE SCC_RegNumB = 0;
BYTE SCC_RegSetB = 0;
BYTE SCC_Vector = 0;

// ============================================================================
// Port B: Serial Communication
// ============================================================================
typedef enum {
    SCC_MODE_MOUSE_ONLY = 0,      // Serial disabled (for backward compatibility)
    SCC_MODE_SERIAL_PTY = 1,
    SCC_MODE_SERIAL_TCP = 2,
    SCC_MODE_SERIAL_FILE = 3,
    SCC_MODE_SERIAL_TCP_SERVER = 4
} SCC_Mode;

typedef struct {
    SCC_Mode mode;
    int master_fd;
    int server_fd;
    int client_fd;
    char slave_path[256];
    pthread_t rx_thread;
    pthread_t accept_thread;
    pthread_mutex_t tx_mutex;
    pthread_mutex_t rx_mutex;
    BYTE tx_buffer[1024];
    int tx_head, tx_tail;
    BYTE rx_buffer[1024];
    int rx_head, rx_tail;
    int baud_rate;
    int data_bits;
    int stop_bits;
    int parity;
    int tcp_port;
    BYTE tx_ready;
    BYTE rx_ready;
    BYTE connected;
    int rx_thread_active;
} SCC_SerialPort;

static SCC_SerialPort scc_port_b;  // Port B for serial communication

// ============================================================================
// Forward declarations
// ============================================================================
static void* SCC_ReceiveThread(void* arg);
static void* SCC_AcceptThread(void* arg);
static int SCC_CreatePTY(SCC_SerialPort* port);
static int SCC_ConnectTCP(SCC_SerialPort* port, const char* host, int port_num);
static int SCC_StartTCPServer(SCC_SerialPort* port, int port_num);
static int SCC_OpenFile(SCC_SerialPort* port, const char* path);
static int SCC_SendByte(SCC_SerialPort* port, BYTE data);
static int SCC_ReceiveByte(SCC_SerialPort* port, BYTE* data);

// ============================================================================
// Interrupt handling
// ============================================================================
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
    // Port A (Mouse) interrupt check
    if ( (SCC_DatNum) && ((SCC_RegsB[1]&0x18)==0x10) && (SCC_RegsB[9]&0x08) ) {
        IRQH_Int(5, &SCC_Int);
    } else if ( (SCC_DatNum==3) && ((SCC_RegsB[1]&0x18)==0x08) && (SCC_RegsB[9]&0x08) ) {
        IRQH_Int(5, &SCC_Int);
    }
    
    // Port B (Serial) interrupt check
    if (scc_port_b.connected && scc_port_b.rx_ready) {
        if (((SCC_RegsB[1]&0x18)==0x10) && (SCC_RegsB[9]&0x08)) {
            IRQH_Int(5, &SCC_Int);
        }
    }
}

// ============================================================================
// Serial port functions
// ============================================================================
static int SCC_CreatePTY(SCC_SerialPort* port)
{
    int master_fd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (master_fd < 0) return -1;
    if (grantpt(master_fd) < 0) { close(master_fd); return -1; }
    if (unlockpt(master_fd) < 0) { close(master_fd); return -1; }
    char* slave_name = ptsname(master_fd);
    if (!slave_name) { close(master_fd); return -1; }

    port->master_fd = master_fd;
    strncpy(port->slave_path, slave_name, sizeof(port->slave_path)-1);
    port->slave_path[sizeof(port->slave_path)-1] = '\0';
    port->connected = 1;
    int flags = fcntl(master_fd, F_GETFL);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    
    printf("SCC_CreatePTY: Created PTY with slave path: %s\n", port->slave_path);
    
    return master_fd;
}

static int SCC_ConnectTCP(SCC_SerialPort* port, const char* host, int port_num)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) return -1;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        close(sock_fd);
        return -1;
    }
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock_fd);
        return -1;
    }
    port->master_fd = sock_fd;
    port->connected = 1;
    int flags = fcntl(sock_fd, F_GETFL);
    fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);
    return sock_fd;
}

static void* SCC_AcceptThread(void* arg)
{
    SCC_SerialPort* port = (SCC_SerialPort*)arg;
    printf("SCC_AcceptThread: Started, waiting for connections on fd %d\n", port->server_fd);
    
    while (port->connected && port->server_fd >= 0) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(port->server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            printf("SCC_AcceptThread: Client connected from %s:%d\n", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            
            if (port->client_fd >= 0) {
                close(port->client_fd);
                if (port->rx_thread_active) {
                    pthread_join(port->rx_thread, NULL);
                    port->rx_thread_active = 0;
                }
            }
            
            port->client_fd = client_fd;
            port->master_fd = client_fd;
            
            int flags = fcntl(client_fd, F_GETFL);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            
            if (pthread_create(&port->rx_thread, NULL, SCC_ReceiveThread, port) != 0) {
                printf("SCC_AcceptThread: Failed to create receive thread\n");
                close(client_fd);
                port->client_fd = -1;
                port->master_fd = -1;
                port->rx_thread_active = 0;
            } else {
                printf("SCC_AcceptThread: Receive thread started for client\n");
                port->rx_thread_active = 1;
            }
        } else {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                printf("SCC_AcceptThread: Accept error: %s\n", strerror(errno));
                break;
            }
        }
        
        usleep(100000);
    }
    
    printf("SCC_AcceptThread: Exiting\n");
    return NULL;
}

static int SCC_StartTCPServer(SCC_SerialPort* port, int port_num)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_num);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 1) < 0) {
        close(server_fd);
        return -1;
    }
    
    int flags = fcntl(server_fd, F_GETFL);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    port->server_fd = server_fd;
    port->client_fd = -1;
    port->master_fd = -1;
    port->tcp_port = port_num;
    port->connected = 1;
    
    printf("SCC_StartTCPServer: Listening on port %d\n", port_num);
    
    return server_fd;
}

static int SCC_OpenFile(SCC_SerialPort* port, const char* path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    
    struct termios tty;
    if (tcgetattr(fd, &tty) < 0) {
        close(fd);
        return -1;
    }
    
    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    
    if (tcsetattr(fd, TCSANOW, &tty) < 0) {
        close(fd);
        return -1;
    }
    
    port->master_fd = fd;
    strncpy(port->slave_path, path, sizeof(port->slave_path)-1);
    port->slave_path[sizeof(port->slave_path)-1] = '\0';
    port->connected = 1;
    
    return fd;
}

static void* SCC_ReceiveThread(void* arg)
{
    SCC_SerialPort* port = (SCC_SerialPort*)arg;
    BYTE buffer[256];
    ssize_t n;
    
    printf("SCC_ReceiveThread: Started, monitoring fd %d\n", port->master_fd);
    
    while (port->connected) {
        n = read(port->master_fd, buffer, sizeof(buffer));
        
        if (n > 0) {
            pthread_mutex_lock(&port->rx_mutex);
            for (ssize_t i = 0; i < n; i++) {
                int next_head = (port->rx_head + 1) % (int)sizeof(port->rx_buffer);
                if (next_head != port->rx_tail) {
                    port->rx_buffer[port->rx_head] = buffer[i];
                    port->rx_head = next_head;
                    port->rx_ready = 1;
                }
            }
            pthread_mutex_unlock(&port->rx_mutex);
            SCC_IntCheck();
        } else if (n == 0) {
            printf("SCC_ReceiveThread: Client disconnected (EOF)\n");
            break;
        } else if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("SCC_ReceiveThread: Read error: %s\n", strerror(errno));
                break;
            }
        }
        
        usleep(1000);
    }
    
    printf("SCC_ReceiveThread: Exited\n");
    return NULL;
}

static int SCC_SendByte(SCC_SerialPort* port, BYTE data)
{
    if (!port->connected) return -1;
    
    pthread_mutex_lock(&port->tx_mutex);
    ssize_t w = write(port->master_fd, &data, 1);
    pthread_mutex_unlock(&port->tx_mutex);
    
    return (w == 1) ? 0 : -1;
}

static int SCC_ReceiveByte(SCC_SerialPort* port, BYTE* data)
{
    if (!port->connected) return -1;
    
    pthread_mutex_lock(&port->rx_mutex);
    if (port->rx_head != port->rx_tail) {
        *data = port->rx_buffer[port->rx_tail];
        port->rx_tail = (port->rx_tail + 1) % (int)sizeof(port->rx_buffer);
        if (port->rx_head == port->rx_tail) {
            port->rx_ready = 0;
        }
        pthread_mutex_unlock(&port->rx_mutex);
        return 0;
    }
    pthread_mutex_unlock(&port->rx_mutex);
    return -1;
}

// ============================================================================
// Public API
// ============================================================================
int SCC_SetMode(int imode, const char* config)
{
    SCC_Mode mode = (SCC_Mode)imode;
    
    if (mode == SCC_MODE_MOUSE_ONLY) {
        // Mouse is always enabled on Port A
        // Just disable serial communication on Port B
        SCC_CloseSerial();
        return 0;
    }
    
    // Set up serial communication on Port B
    SCC_CloseSerial();
    scc_port_b.mode = mode;
    
    switch (mode) {
        case SCC_MODE_SERIAL_PTY:
            if (SCC_CreatePTY(&scc_port_b) < 0) return -1;
            break;
            
        case SCC_MODE_SERIAL_TCP: {
            if (!config) return -1;
            char host[256];
            int port;
            if (sscanf(config, "%255[^:]:%d", host, &port) != 2) return -1;
            if (SCC_ConnectTCP(&scc_port_b, host, port) < 0) return -1;
            break;
        }
        
        case SCC_MODE_SERIAL_TCP_SERVER: {
            if (!config) return -1;
            int port;
            if (sscanf(config, "%d", &port) != 1) return -1;
            if (SCC_StartTCPServer(&scc_port_b, port) < 0) return -1;
            break;
        }
        
        case SCC_MODE_SERIAL_FILE:
            if (!config || SCC_OpenFile(&scc_port_b, config) < 0) return -1;
            break;
            
        default:
            return -1;
    }
    
    // Start receive thread for non-server modes
    if (mode != SCC_MODE_SERIAL_TCP_SERVER) {
        pthread_mutex_init(&scc_port_b.tx_mutex, NULL);
        pthread_mutex_init(&scc_port_b.rx_mutex, NULL);
        if (pthread_create(&scc_port_b.rx_thread, NULL, SCC_ReceiveThread, &scc_port_b) != 0) {
            SCC_CloseSerial();
            return -1;
        }
        scc_port_b.rx_thread_active = 1;
    }
    
    // Start accept thread for server mode
    if (mode == SCC_MODE_SERIAL_TCP_SERVER) {
        pthread_mutex_init(&scc_port_b.tx_mutex, NULL);
        pthread_mutex_init(&scc_port_b.rx_mutex, NULL);
        
        // Initialize SCC registers for serial communication
        SCC_RegsB[1] = 0x10;  // RX interrupt enable
        SCC_RegsB[3] = 0xC1;  // RX enable, 8 bits
        SCC_RegsB[4] = 0x44;  // x16 clock, 1 stop bit, no parity
        SCC_RegsB[5] = 0x6A;  // TX enable, 8 bits, DTR, RTS
        SCC_RegsB[9] = 0x08;  // Master interrupt enable
        SCC_RegsB[11] = 0x50; // TX/RX clock from BRG
        SCC_RegsB[12] = 0x18; // 9600 baud lower byte
        SCC_RegsB[13] = 0x00; // 9600 baud upper byte
        SCC_RegsB[14] = 0x01; // BRG enable
        
        if (pthread_create(&scc_port_b.accept_thread, NULL, SCC_AcceptThread, &scc_port_b) != 0) {
            SCC_CloseSerial();
            return -1;
        }
    }
    
    return 0;
}

void SCC_CloseSerial(void)
{
    if (scc_port_b.connected) {
        scc_port_b.connected = 0;
        
        if (scc_port_b.mode == SCC_MODE_SERIAL_TCP_SERVER) {
            pthread_join(scc_port_b.accept_thread, NULL);
            if (scc_port_b.rx_thread_active) {
                pthread_join(scc_port_b.rx_thread, NULL);
                scc_port_b.rx_thread_active = 0;
            }
            pthread_mutex_destroy(&scc_port_b.tx_mutex);
            pthread_mutex_destroy(&scc_port_b.rx_mutex);
        } else if (scc_port_b.mode != SCC_MODE_MOUSE_ONLY) {
            if (scc_port_b.rx_thread_active) {
                pthread_join(scc_port_b.rx_thread, NULL);
                scc_port_b.rx_thread_active = 0;
            }
            pthread_mutex_destroy(&scc_port_b.tx_mutex);
            pthread_mutex_destroy(&scc_port_b.rx_mutex);
        }
        
        if (scc_port_b.master_fd >= 0) {
            close(scc_port_b.master_fd);
            scc_port_b.master_fd = -1;
        }
        if (scc_port_b.server_fd >= 0) {
            close(scc_port_b.server_fd);
            scc_port_b.server_fd = -1;
        }
        if (scc_port_b.client_fd >= 0) {
            close(scc_port_b.client_fd);
            scc_port_b.client_fd = -1;
        }
        
        scc_port_b.slave_path[0] = '\0';
    }
}

const char* SCC_GetSlavePath(void)
{
    if (scc_port_b.connected && 
        scc_port_b.mode == SCC_MODE_SERIAL_PTY && 
        scc_port_b.slave_path[0] != '\0') {
        return scc_port_b.slave_path;
    }
    return NULL;
}

void SCC_Init(void)
{
    // Mouse initialization (Port A)
    MouseX = 0;
    MouseY = 0;
    MouseSt = 0;
    SCC_DatNum = 0;
    
    // Register initialization
    SCC_RegNumA = 0;
    SCC_RegSetA = 0;
    SCC_RegNumB = 0;
    SCC_RegSetB = 0;
    SCC_Vector = 0;
    
    // Serial port initialization (Port B)
    memset(&scc_port_b, 0, sizeof(scc_port_b));
    scc_port_b.mode = SCC_MODE_MOUSE_ONLY;
    scc_port_b.master_fd = -1;
    scc_port_b.server_fd = -1;
    scc_port_b.client_fd = -1;
    scc_port_b.rx_thread_active = 0;
    scc_port_b.baud_rate = 9600;
    scc_port_b.data_bits = 8;
    scc_port_b.stop_bits = 1;
    scc_port_b.parity = 0;
    scc_port_b.slave_path[0] = '\0';
}

// ============================================================================
// I/O Functions
// ============================================================================
void FASTCALL SCC_Write(DWORD adr, BYTE data)
{
    if (adr>=0xe98008) return;
    
    // Port B Control (0xE98001)
    if ((adr&7) == 1) {
        if (SCC_RegSetB) {
            if (SCC_RegNumB == 5) {
                // Port B: Serial communication RTS control
                if ((data & 2) && scc_port_b.connected) {
                    scc_port_b.tx_ready = 1;
                }
            } else if (SCC_RegNumB == 2) {
                SCC_Vector = data;
            }
            SCC_RegSetB = 0;
            SCC_RegsB[SCC_RegNumB] = data;
            SCC_RegNumB = 0;
        } else {
            if (!(data&0xf0)) {
                data &= 15;
                SCC_RegSetB = 1;
                SCC_RegNumB = data;
            } else {
                SCC_RegSetB = 0;
                SCC_RegNumB = 0;
            }
        }
    }
    // Port B Data (0xE98003)
    else if ((adr&7) == 3) {
        if (scc_port_b.connected) {
            SCC_SendByte(&scc_port_b, data);
        }
    }
    // Port A Control (0xE98005)
    else if ((adr&7) == 5) {
        if (SCC_RegSetA) {
            if (SCC_RegNumA == 5) {
                // Port A: Mouse RTS control
                // Detect RTS 0->1 transition with receiver enabled
                int rtsRising = (!(SCC_RegsA[5]&2)) && (data&2);
                int rxEnabled = (SCC_RegsA[3]&1);
                
                // DOUBLE-CLICK FIX: Only generate mouse data when no data is pending
                // This ensures each polling cycle gets exactly one mouse state
                if (rtsRising && rxEnabled && (!SCC_DatNum)) {
                    Mouse_SetData();
                    SCC_DatNum = 3;
                    SCC_Dat[2] = MouseSt;
                    SCC_Dat[1] = (BYTE)MouseX;
                    SCC_Dat[0] = (BYTE)MouseY;
                }
            } else if (SCC_RegNumA == 2) {
                SCC_RegsB[2] = data;
                SCC_Vector = data;
            } else if (SCC_RegNumA == 9) {
                SCC_RegsB[9] = data;
            }
            SCC_RegSetA = 0;
            SCC_RegsA[SCC_RegNumA] = data;
            SCC_RegNumA = 0;
        } else {
            data &= 15;
            if (data) {
                SCC_RegSetA = 1;
                SCC_RegNumA = data;
            } else {
                SCC_RegSetA = 0;
                SCC_RegNumA = 0;
            }
        }
    }
    // Port A Data (0xE98007)
    else if ((adr&7) == 7) {
        // Port A is mouse-only, no data write
    }
}

BYTE FASTCALL SCC_Read(DWORD adr)
{
    BYTE ret = 0;
    if (adr>=0xe98008) return ret;
    
    // Port B Control (0xE98001)
    if ((adr&7) == 1) {
        if (!SCC_RegNumB) {
            ret = 0;
            if (scc_port_b.tx_ready) ret |= 4;  // TX ready
            if (scc_port_b.rx_ready) ret |= 1;  // RX data available
        }
        SCC_RegNumB = 0;
        SCC_RegSetB = 0;
    }
    // Port B Data (0xE98003)
    else if ((adr&7) == 3) {
        BYTE d;
        if (SCC_ReceiveByte(&scc_port_b, &d) == 0) {
            ret = d;
        }
    }
    // Port A Control (0xE98005)
    else if ((adr&7) == 5) {
        switch(SCC_RegNumA) {
            case 0:
                ret = 4;  // TX buffer empty (mouse always ready)
                break;
            case 3:
                ret = (SCC_DatNum ? 4 : 0);  // RX data available
                break;
        }
        SCC_RegNumA = 0;
        SCC_RegSetA = 0;
    }
    // Port A Data (0xE98007)
    else if ((adr&7) == 7) {
        // Read mouse data
        if (SCC_DatNum) {
            SCC_DatNum--;
            ret = SCC_Dat[SCC_DatNum];
        }
    }
    
    return ret;
}
