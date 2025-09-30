// ---------------------------------------------------------------------------------------
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

// Gradual mouse data transmission (like zsrc)
static BYTE SCC_MouseBuffer[3] = {0, 0, 0};
static int SCC_MouseBufferPos = 3;  // 3 means no data
static unsigned int SCC_MouseSendCycles = 0;
static const unsigned int SCC_MOUSE_CYCLES_PER_BYTE = 100;  // Much faster timing

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
        // Temporary: Disable gradual transmission - causing mouse lockup
        /*
        if (SCC_MouseBufferPos < 3 && !SCC_DatNum) {
            SCC_MouseSendCycles++;
            if (SCC_MouseSendCycles >= SCC_MOUSE_CYCLES_PER_BYTE) {
                // Send one byte from buffer
                SCC_DatNum = 1;
                SCC_Dat[0] = SCC_MouseBuffer[SCC_MouseBufferPos];
                SCC_MouseBufferPos++;
                SCC_MouseSendCycles = 0;

                printf("SCC: Gradual send byte %d: %02X\n", SCC_MouseBufferPos-1, SCC_Dat[0]);
            }
        }
        */

        if ( (SCC_DatNum) && ((SCC_RegsB[1]&0x18)==0x10) && (SCC_RegsB[9]&0x08) ) {
            IRQH_Int(5, &SCC_Int);
        } else if ( (SCC_DatNum==3) && ((SCC_RegsB[1]&0x18)==0x08) && (SCC_RegsB[9]&0x08) ) {
            IRQH_Int(5, &SCC_Int);
        }
    } else {
        if (scc_port_a.rx_ready && ((SCC_RegsB[1]&0x18)==0x10) && (SCC_RegsB[9]&0x08)) {
            printf("SCC_IntCheck: SERIAL INTERRUPT TRIGGERED!\n");
            IRQH_Int(5, &SCC_Int);
        } else if (scc_port_a.rx_ready) {
            printf("SCC_IntCheck: rx_ready=1 but interrupt not triggered (RegB1=0x%02X, RegB9=0x%02X)\n",
                   SCC_RegsB[1], SCC_RegsB[9]);
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

// 前方宣言
static void* SCC_ReceiveThread(void* arg);

// TCPサーバー機能：ポートでリスニングしてクライアントの接続を待機
static void* SCC_AcceptThread(void* arg)
{
    SCC_SerialPort* port = (SCC_SerialPort*)arg;
    printf("SCC_AcceptThread: Started, waiting for connections on fd %d\n", port->server_fd);
    printf("SCC_AcceptThread: Initial state - connected=%d, server_fd=%d\n", port->connected, port->server_fd);
    
    int accept_count = 0;
    while (port->connected && port->server_fd >= 0) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        accept_count++;
        if (accept_count % 5000 == 0) {
            printf("SCC_AcceptThread: Still waiting for connections (count=%d)\n", accept_count);
        }
        
        int client_fd = accept(port->server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (accept_count % 1000 == 0) {
            printf("SCC_AcceptThread: accept() returned %d (errno=%d: %s)\n", client_fd, errno, strerror(errno));
        }
        if (client_fd >= 0) {
            printf("SCC_AcceptThread: Client connected from %s:%d\n", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            
            // 既存のクライアント接続がある場合は先に受信スレッドを終了
            if (port->client_fd >= 0) {
                close(port->client_fd);
                if (port->rx_thread_active) {
                    pthread_join(port->rx_thread, NULL);
                    port->rx_thread_active = 0;
                }
            }
            
            port->client_fd = client_fd;
            port->master_fd = client_fd;  // 受信スレッドが使用するFDを更新
            
            // ノンブロッキングに設定
            int flags = fcntl(client_fd, F_GETFL);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            
            // クライアント接続後に受信スレッドを開始
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
                printf("SCC_AcceptThread: Accept error: %s (errno=%d)\n", strerror(errno), errno);
                break;  // サーバーソケットが閉じられた場合など
            }
        }
        
        usleep(100000);  // 100ms待機
    }
    
    printf("SCC_AcceptThread: Exiting - connected=%d, server_fd=%d\n", port->connected, port->server_fd);
    return NULL;
}

static int SCC_StartTCPServer(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;
    
    // SO_REUSEADDRを設定（ポートの再利用を許可）
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // 全てのIPアドレスでリスニング
    server_addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 1) < 0) {  // 最大1つの接続を待機
        close(server_fd);
        return -1;
    }
    
    // サーバーソケットをノンブロッキングに設定
    int flags = fcntl(server_fd, F_GETFL);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    scc_port_a.server_fd = server_fd;
    scc_port_a.client_fd = -1;
    scc_port_a.master_fd = -1;
    scc_port_a.tcp_port = port;
    scc_port_a.connected = 1;
    
    printf("SCC_StartTCPServer: Listening on port %d\n", port);
    
    return server_fd;
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
    printf("SCC_ReceiveThread: Started, monitoring fd %d\n", port->master_fd);
    
    int poll_count = 0;
    while (port->connected) {
        n = read(port->master_fd, buffer, sizeof(buffer));
        poll_count++;
        
        if (n > 0) {
            printf("SCC_ReceiveThread: Received %zd bytes: ", n);
            for (ssize_t i = 0; i < n; i++) {
                printf("%02X ", buffer[i]);
            }
            printf("\n");
            
            pthread_mutex_lock(&port->rx_mutex);
            for (ssize_t i = 0; i < n; i++) {
                int next_head = (port->rx_head + 1) % (int)sizeof(port->rx_buffer);
                if (next_head != port->rx_tail) { port->rx_buffer[port->rx_head] = buffer[i]; port->rx_head = next_head; port->rx_ready = 1; }
            }
            pthread_mutex_unlock(&port->rx_mutex);
            SCC_IntCheck();
        } else if (n == 0) {
            printf("SCC_ReceiveThread: Client disconnected (EOF)\n");
            break;
        } else if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("SCC_ReceiveThread: Read error: %s (errno=%d)\n", strerror(errno), errno);
                break;
            }
            // ノンブロッキングの場合は正常
        }
        
        // 5秒ごとにalive状況を報告
        if (poll_count % 5000 == 0) {
            printf("SCC_ReceiveThread: Still alive, polling fd %d (poll_count=%d)\n", port->master_fd, poll_count);
        }
        
        usleep(1000);
    }
    
    printf("SCC_ReceiveThread: Exited\n");
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
        case SCC_MODE_SERIAL_TCP_SERVER: {
            if (!config) return -1; int port;
            if (sscanf(config, "%d", &port) != 1) return -1;
            if (SCC_StartTCPServer(port) < 0) return -1; break; }
        case SCC_MODE_SERIAL_FILE: if (!config || SCC_OpenFile(config) < 0) return -1; break;
        default: return -1;
    }
    if (mode != SCC_MODE_MOUSE_ONLY && mode != SCC_MODE_SERIAL_TCP_SERVER) {
        pthread_mutex_init(&scc_port_a.tx_mutex, NULL);
        pthread_mutex_init(&scc_port_a.rx_mutex, NULL);
        if (pthread_create(&scc_port_a.rx_thread, NULL, SCC_ReceiveThread, &scc_port_a) != 0) { SCC_CloseSerial(); return -1; }
        scc_port_a.rx_thread_active = 1;
    }
    
    // TCPサーバーモードの場合はaccept待機スレッドのみ作成（受信スレッドはクライアント接続後）
    if (mode == SCC_MODE_SERIAL_TCP_SERVER) {
        pthread_mutex_init(&scc_port_a.tx_mutex, NULL);
        pthread_mutex_init(&scc_port_a.rx_mutex, NULL);
        
        // シリアル通信の標準設定でSCCレジスタを初期化
        // 9600 baud, 8 data bits, 1 stop bit, no parity
        SCC_RegsB[1] = 0x10;  // 受信割り込みイネーブル
        SCC_RegsB[3] = 0xC1;  // 受信イネーブル, 8ビット
        SCC_RegsB[4] = 0x44;  // x16クロック, 1ストップビット, パリティなし  
        SCC_RegsB[5] = 0x6A;  // 送信イネーブル, 8ビット, DTR, RTS
        SCC_RegsB[9] = 0x08;  // マスター割り込みイネーブル
        SCC_RegsB[11] = 0x50; // 送受信共にボーレートジェネレータを使用
        SCC_RegsB[12] = 0x18; // 9600baud用の下位バイト
        SCC_RegsB[13] = 0x00; // 9600baud用の上位バイト
        SCC_RegsB[14] = 0x01; // ボーレートジェネレータイネーブル
        
        printf("SCC_SetMode: Initialized SCC registers for TCP server mode\n");
        printf("SCC_SetMode: About to create accept thread - connected=%d, server_fd=%d\n", scc_port_a.connected, scc_port_a.server_fd);
        if (pthread_create(&scc_port_a.accept_thread, NULL, SCC_AcceptThread, &scc_port_a) != 0) { 
            printf("SCC_SetMode: Failed to create accept thread\n");
            SCC_CloseSerial(); 
            return -1; 
        }
        printf("SCC_SetMode: Accept thread created successfully\n");
    }
    return 0;
}

void SCC_CloseSerial(void)
{
    if (scc_port_a.connected) {
        scc_port_a.connected = 0;
        
        if (scc_port_a.mode == SCC_MODE_SERIAL_TCP_SERVER) {
            // TCPサーバーモードの場合
            
            // accept待機スレッドを終了
            pthread_join(scc_port_a.accept_thread, NULL);
            
            // クライアントが接続されている場合は受信スレッドも終了
            if (scc_port_a.rx_thread_active) {
                pthread_join(scc_port_a.rx_thread, NULL);
                scc_port_a.rx_thread_active = 0;
            }
            
            pthread_mutex_destroy(&scc_port_a.tx_mutex);
            pthread_mutex_destroy(&scc_port_a.rx_mutex);
        } else if (scc_port_a.mode != SCC_MODE_MOUSE_ONLY) {
            // 他のシリアルモードの場合
            if (scc_port_a.rx_thread_active) {
                pthread_join(scc_port_a.rx_thread, NULL);
                scc_port_a.rx_thread_active = 0;
            }
            pthread_mutex_destroy(&scc_port_a.tx_mutex);
            pthread_mutex_destroy(&scc_port_a.rx_mutex);
        }
        
        if (scc_port_a.master_fd >= 0) { close(scc_port_a.master_fd); scc_port_a.master_fd = -1; }
        
        // TCPサーバー関連のソケットもクローズ
        if (scc_port_a.server_fd >= 0) { close(scc_port_a.server_fd); scc_port_a.server_fd = -1; }
        if (scc_port_a.client_fd >= 0) { close(scc_port_a.client_fd); scc_port_a.client_fd = -1; }
        
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
    printf("SCC_SendByte: Sending byte %02X ('%c') to fd %d, mode=%d\n", 
           data, (data >= 32 && data < 127) ? data : '.', scc_port_a.master_fd, scc_port_a.mode);
    pthread_mutex_lock(&scc_port_a.tx_mutex);
    ssize_t w = write(scc_port_a.master_fd, &data, 1);
    pthread_mutex_unlock(&scc_port_a.tx_mutex);
    printf("SCC_SendByte: Write result = %zd\n", w);
    return (w == 1) ? 0 : -1;
}

int SCC_ReceiveByte(BYTE* data)
{
    if (!scc_port_a.connected || scc_port_a.mode == SCC_MODE_MOUSE_ONLY) return -1;
    pthread_mutex_lock(&scc_port_a.rx_mutex);
    if (scc_port_a.rx_head != scc_port_a.rx_tail) {
        *data = scc_port_a.rx_buffer[scc_port_a.rx_tail];
        printf("SCC_ReceiveByte: Read byte %02X ('%c') from buffer\n", *data, (*data >= 32 && *data < 127) ? *data : '.');
        scc_port_a.rx_tail = (scc_port_a.rx_tail + 1) % (int)sizeof(scc_port_a.rx_buffer);
        if (scc_port_a.rx_head == scc_port_a.rx_tail) {
            scc_port_a.rx_ready = 0;
            printf("SCC_ReceiveByte: Buffer empty, rx_ready=0\n");
        } else {
            printf("SCC_ReceiveByte: More data available, rx_ready=1\n");
        }
        pthread_mutex_unlock(&scc_port_a.rx_mutex); return 0;
    }
    printf("SCC_ReceiveByte: No data available (head=%d, tail=%d)\n", scc_port_a.rx_head, scc_port_a.rx_tail);
    pthread_mutex_unlock(&scc_port_a.rx_mutex); return -1;
}

void SCC_Init(void)
{
    MouseX = 0; MouseY = 0; MouseSt = 0;
    SCC_RegNumA = SCC_RegSetA = SCC_RegNumB = SCC_RegSetB = 0; SCC_Vector = 0; SCC_DatNum = 0;

    // Initialize gradual mouse transmission variables
    SCC_MouseBufferPos = 3;  // No data
    SCC_MouseSendCycles = 0;
    memset(SCC_MouseBuffer, 0, sizeof(SCC_MouseBuffer));

    memset(&scc_port_a, 0, sizeof(scc_port_a)); memset(&scc_port_b, 0, sizeof(scc_port_b));
    scc_port_a.mode = SCC_MODE_MOUSE_ONLY; scc_port_a.master_fd = -1;
    scc_port_a.server_fd = -1; scc_port_a.client_fd = -1;  // TCPサーバー関連フィールドを初期化
    scc_port_a.rx_thread_active = 0;  // 受信スレッド状態を初期化
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
                    // 修正: XEiJ互換のRTS変化検出 - 0→1への変化を確実にキャッチ
                    if ( (!(SCC_RegsB[5]&2)) && (data&2) && (SCC_RegsB[3]&1) && (!SCC_DatNum) ) {
                        Mouse_SetData();

                        // 重複データ送信防止：前回と同じデータの場合はスキップ
                        static BYTE lastSentMouseSt = 0xFF;
                        static signed char lastSentMouseX = 127;
                        static signed char lastSentMouseY = 127;

                        if (MouseSt == lastSentMouseSt && MouseX == lastSentMouseX && MouseY == lastSentMouseY) {
                            return; // 重複データはスキップ
                        }

                        // X68000 mouse protocol order: Y -> X -> status
                        SCC_DatNum = 3;
                        SCC_Dat[0] = (BYTE)MouseY;
                        SCC_Dat[1] = (BYTE)MouseX;
                        SCC_Dat[2] = MouseSt;

                        // 今回送信したデータを記録
                        lastSentMouseSt = MouseSt;
                        lastSentMouseX = MouseX;
                        lastSentMouseY = MouseY;

                        // ログ出力を削除（ノイズ削減のため）
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
        if (scc_port_a.mode != SCC_MODE_MOUSE_ONLY && scc_port_a.connected) { 
            printf("SCC_Write: Port A data register write (adr=%08X, data=%02X)\n", adr, data);
            SCC_SendByte(data); 
        }
    } else if ((adr&7) == 5) {
        if (SCC_RegSetA) {
            SCC_RegSetA = 0; switch (SCC_RegNumA) { case 2: SCC_RegsB[2] = data; SCC_Vector = data; break; case 9: SCC_RegsB[9] = data; break; }
        } else {
            data &= 15; if (data) { SCC_RegSetA = 1; SCC_RegNumA = data; } else { SCC_RegSetA = 0; SCC_RegNumA = 0; }
        }
    } else if ((adr&7) == 7) {
        if (scc_port_a.mode != SCC_MODE_MOUSE_ONLY && scc_port_a.connected) { 
            printf("SCC_Write: Port B data register write (adr=%08X, data=%02X)\n", adr, data);
            SCC_SendByte(data); 
        }
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
            BYTE d; 
            if (SCC_ReceiveByte(&d) == 0) {
                ret = d;
                printf("SCC_Read: Port A data read, returning byte %02X ('%c')\n", ret, (ret >= 32 && ret < 127) ? ret : '.');
            } else {
                printf("SCC_Read: Port A data read, but no data available\n");
            }
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
        if (scc_port_a.mode != SCC_MODE_MOUSE_ONLY) { 
            BYTE d; 
            if (SCC_ReceiveByte(&d) == 0) {
                ret = d;
                printf("SCC_Read: Port B data read, returning byte %02X ('%c')\n", ret, (ret >= 32 && ret < 127) ? ret : '.');
            } else {
                printf("SCC_Read: Port B data read, but no data available\n");
            }
        }
    }
    return ret;
}
