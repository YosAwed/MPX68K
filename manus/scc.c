//  SCC_ENHANCED.C - Z8530 SCC with Real Serial Communication Support
//  Integrated enhanced version supporting PTY, TCP, and real serial ports
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

// Latch queue for pending mouse packets when RTS rises while a packet is still pending
#define SCC_MOUSE_QSIZE 4
static BYTE SCC_MouseQueue[SCC_MOUSE_QSIZE][3]; // [][2]=Status, [][1]=X, [][0]=Y
static int SCC_MouseQHead = 0;
static int SCC_MouseQTail = 0;
static int SCC_MouseQCount = 0;

static inline int SCC_MouseQIsEmpty(void) { return SCC_MouseQCount == 0; }
static inline int SCC_MouseQIsFull(void) { return SCC_MouseQCount >= SCC_MOUSE_QSIZE; }
static inline void SCC_MouseQEnqueue(BYTE st, BYTE x, BYTE y) {
    if (SCC_MouseQIsFull()) {
        // Drop oldest
        SCC_MouseQHead = (SCC_MouseQHead + 1) % SCC_MOUSE_QSIZE;
        SCC_MouseQCount--;
    }
    SCC_MouseQueue[SCC_MouseQTail][2] = st;
    SCC_MouseQueue[SCC_MouseQTail][1] = x;
    SCC_MouseQueue[SCC_MouseQTail][0] = y;
    SCC_MouseQTail = (SCC_MouseQTail + 1) % SCC_MOUSE_QSIZE;
    SCC_MouseQCount++;
}
static inline void SCC_MouseQDequeue(BYTE* st, BYTE* x, BYTE* y) {
    if (SCC_MouseQIsEmpty()) return;
    *st = SCC_MouseQueue[SCC_MouseQHead][2];
    *x  = SCC_MouseQueue[SCC_MouseQHead][1];
    *y  = SCC_MouseQueue[SCC_MouseQHead][0];
    SCC_MouseQHead = (SCC_MouseQHead + 1) % SCC_MOUSE_QSIZE;
    SCC_MouseQCount--;
}

// Enhanced serial ports
typedef enum {
    SCC_MODE_MOUSE_ONLY = 0,
    SCC_MODE_SERIAL_PTY = 1,
    SCC_MODE_SERIAL_TCP = 2,
    SCC_MODE_SERIAL_FILE = 3,
    SCC_MODE_SERIAL_TCP_SERVER = 4
} SCC_Mode;
typedef struct {
    SCC_Mode mode;
    int master_fd;
    int server_fd;  // TCPサーバー用のリスニングソケット
    int client_fd;  // TCPサーバーで接続されたクライアントソケット
    char slave_path[256];
    pthread_t rx_thread;
    pthread_t accept_thread;  // TCPサーバー用のaccept待機スレッド
    pthread_mutex_t tx_mutex;
    pthread_mutex_t rx_mutex;
    BYTE tx_buffer[1024]; int tx_head, tx_tail;
    BYTE rx_buffer[1024]; int rx_head, rx_tail;
    int baud_rate; int data_bits; int stop_bits; int parity;
    int tcp_port;  // TCPサーバーのポート番号
    BYTE tx_ready; BYTE rx_ready; BYTE connected;
    int rx_thread_active;  // 受信スレッドが動作中かどうか
} SCC_SerialPort;
static SCC_SerialPort scc_port_a;
static SCC_SerialPort scc_port_b;

static DWORD FASTCALL SCC_Int(BYTE irq)
{
