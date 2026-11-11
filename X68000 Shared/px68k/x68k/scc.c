// ---------------------------------------------------------------------------------------
//  SCC.C - Z8530 SCC
//  Channel A ($E98005/$E98007): RS-232C. Channel B ($E98001/$E98003): mouse.
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
#include <stdint.h>
#include <stdatomic.h>

// ============================================================================
// Port A: Mouse (always enabled)
// ============================================================================
signed char MouseX = 0;
signed char MouseY = 0;
BYTE MouseSt = 0;
BYTE SCC_Dat[3] = {0, 0, 0};
BYTE SCC_DatNum = 0;

// Compatibility mode: when enabled, mimic original px68k SCC mouse behavior
// - Only generate mouse packet on RTS rising edge when RX enabled and no data pending
// - Port A control reads return original bit patterns (RR0=0x04, RR3=(SCC_DatNum?0x04:0))
static int g_scc_mouse_compat_mode = 0;
static int g_scc_trace = 0; // verbose tracing (env SCC_MOUSE_TRACE=1)
static int g_scc_edgelog = 0; // lightweight edge log (env SCC_MOUSE_EDGELOG=1)

// Last published snapshot (compat mode coalescing)
static BYTE g_last_pub_st = 0;
static signed char g_last_pub_x = 0;
static signed char g_last_pub_y = 0;

static long long scc_now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + (tv.tv_usec / 1000);
}

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
// Host serial backends for the RS-232C port (pty, TCP, device file)
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

static SCC_SerialPort scc_port_b;  // host backend of the RS-232C port (historical name)

// ----------------------------------------------------------------------------
// Mouse packet edge queue (preserve rapid press/release transitions)
// We publish at most one packet per RTS rising edge. If another edge occurs
// while a packet is still pending, we latch the latest button state as a
// button-only packet (X=0,Y=0) for publication on the next RTS rising.
#define SCC_MOUSE_QSIZE 8
static BYTE g_mouse_q[SCC_MOUSE_QSIZE][3]; // [][2]=Status, [][1]=X, [][0]=Y
static int g_mouse_q_head = 0, g_mouse_q_tail = 0, g_mouse_q_cnt = 0;
static inline int mouse_q_empty(void){ return g_mouse_q_cnt == 0; }
static inline int mouse_q_full(void){ return g_mouse_q_cnt >= SCC_MOUSE_QSIZE; }
static inline void mouse_q_enq(BYTE st, BYTE x, BYTE y){
    if (mouse_q_full()) { g_mouse_q_head = (g_mouse_q_head + 1) % SCC_MOUSE_QSIZE; g_mouse_q_cnt--; }
    g_mouse_q[g_mouse_q_tail][2] = st;
    g_mouse_q[g_mouse_q_tail][1] = x;
    g_mouse_q[g_mouse_q_tail][0] = y;
    g_mouse_q_tail = (g_mouse_q_tail + 1) % SCC_MOUSE_QSIZE;
    g_mouse_q_cnt++;
}
static inline int mouse_q_deq(BYTE* st, BYTE* x, BYTE* y){
    if (mouse_q_empty()) return 0;
    *st = g_mouse_q[g_mouse_q_head][2];
    *x  = g_mouse_q[g_mouse_q_head][1];
    *y  = g_mouse_q[g_mouse_q_head][0];
    g_mouse_q_head = (g_mouse_q_head + 1) % SCC_MOUSE_QSIZE;
    g_mouse_q_cnt--;
    return 1;
}

// Public API: latch a mouse packet (typically button-only) at event time.
// This decouples host edge timing from SCC RTS sampling.
void SCC_LatchMouseStatus(BYTE st, signed char x, signed char y)
{
    if (g_scc_mouse_compat_mode) {
        // In strict compat mode, do not use the queue at all
        return;
    }
    // Only store minimal changes; queue is small
    mouse_q_enq(st, (BYTE)x, (BYTE)y);
}

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

// ----------------------------------------------------------------------------
// Compatibility mode control (public C API)
void SCC_SetCompatMode(int enable) {
    g_scc_mouse_compat_mode = (enable ? 1 : 0);
    // Inform mouse layer to switch behavior as well
    extern void Mouse_SetCompatMode(int enable);
    Mouse_SetCompatMode(g_scc_mouse_compat_mode);
}

int SCC_GetCompatMode(void) {
    return g_scc_mouse_compat_mode;
}

// ============================================================================
// Channel A: RS-232C
//
// In the X68000 the Z8530's channel A ($E98005 command, $E98007 data) is the
// RS-232C port and channel B is the mouse. Channel A is emulated here as the
// chip behaves: a 3-deep receive FIFO, a transmit buffer plus shifter timed by
// the baud rate generator, the modem inputs in RR0 with external/status
// latching, and the three interrupt sources with their vectors.
//
// What is plugged into the connector is a "link": an ordered stream of events
// (characters, RTS/DTR/break changes, line settings) exchanged with another
// emulator, which wires the two ports like a cross cable:
//     TxD -> RxD,  RTS -> CTS,  DTR -> DSR and DCD
// The host application only moves the wire bytes (SCC_LinkDrain/SCC_LinkFeed).
// ============================================================================
#define SCCA_PCLK        5000000    // SCC clock in the X68000 (Hz)
#define SCC_TIMER_HZ     10000000   // SCC_Timer() counts 10 MHz base clocks

#define RR0_RX_AVAIL     0x01
#define RR0_TX_EMPTY     0x04
#define RR0_DCD          0x08
#define RR0_SYNC         0x10       // /SYNCA carries DSR in the X68000
#define RR0_CTS          0x20
#define RR0_TX_UNDERRUN  0x40
#define RR0_BREAK        0x80
#define RR0_PINS         (RR0_DCD | RR0_SYNC | RR0_CTS | RR0_BREAK)

#define RXERR_PARITY     0x10
#define RXERR_OVERRUN    0x20
#define RXERR_FRAMING    0x40

// Link events: type in the top byte, payload in the low 24 bits. On the wire
// each event is these four bytes, most significant first.
#define LEV_DATA         1          // payload: character
#define LEV_LINES        2          // payload: LINE_* of the sender
#define LEV_BAUD         3          // payload: bits per second
#define LEV_FORMAT       4          // payload: data bits << 16 | parity << 8 | stop bits * 2
#define LEV_CONNECT      0x10       // local only
#define LEV_DISCONNECT   0x11       // local only

#define LINE_RTS         1
#define LINE_DTR         2
#define LINE_BREAK       4

// While our RTS is negated a peer that honours CTS would not have sent. The
// link has latency, so characters that were already on their way are held back
// until RTS is asserted again. A program that never raises RTS is not using it
// for flow control, so after this long the characters are let through.
#define RTS_HOLD_CLK     (SCC_TIMER_HZ / 10)

#define LINK_RXQ         16384      // events
#define LINK_TXQ         65536      // bytes

typedef struct {
    BYTE ptr;                 // register pointer for the next command access
    BYTE rx_data[3];
    BYTE rx_err[3];
    int  rx_count;
    BYTE rr1_err;             // parity/overrun, latched until Error Reset
    BYTE rx_first_armed;
    BYTE rx_first_hit;
    BYTE tx_buf;
    BYTE tx_buf_full;
    BYTE tx_shift;
    BYTE tx_busy;
    int  tx_clk;              // clocks left for the character in the shifter
    BYTE tx_ip;
    BYTE ext_ip;
    BYTE pins;                // live modem inputs, RR0 bit positions
    BYTE rr0_latch;
    BYTE rr0_latched;
    BYTE irq_raised;
    int  rx_gap;              // clocks until the next character can arrive
    int  rx_full_clk;         // how long the receive FIFO has been full
    int  rts_off_clk;         // how long RTS has been negated
} SCC_ChannelA;

static SCC_ChannelA scca;
#define WRA SCC_RegsA

static pthread_mutex_t s_link_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct {
    // guarded by s_link_mutex
    uint32_t rxq[LINK_RXQ];
    int rx_head, rx_count;
    BYTE txq[LINK_TXQ];
    int tx_head, tx_count;
    BYTE partial[4];
    int partial_len;
    void (*notify)(void);
    atomic_int rx_pending;
    // emulation thread only
    int online;
    BYTE peer_lines;
    int peer_baud;            // 0 = unknown
    int peer_format;
    int sent_lines, sent_baud, sent_format;
    unsigned int tx_chars, rx_chars, mismatches;
} s_link;

static DWORD FASTCALL SCC_Int(BYTE irq);

// Register-level trace of the RS-232C channel (env SCC_RS232C_TRACE=1), for
// finding out what a program expects of the port. Identical lines in a row
// are counted rather than repeated.
static int g_scca_trace = 0;
static void scca_trace(const char *what, int reg, int value)
{
    static char last[48];
    static int repeats = 0;
    char line[48];
    if (!g_scca_trace) return;
    snprintf(line, sizeof(line), "%s%d=%02X", what, reg, value & 0xff);
    if (strcmp(line, last) == 0) {
        repeats++;
        return;
    }
    if (repeats) printf("[scc-a]   (x%d more)\n", repeats);
    repeats = 0;
    strcpy(last, line);
    printf("[scc-a] %s\n", line);
    fflush(stdout);
}

static void link_push_locked(uint32_t ev)
{
    if (s_link.rx_count < LINK_RXQ) {
        s_link.rxq[(s_link.rx_head + s_link.rx_count) % LINK_RXQ] = ev;
        s_link.rx_count++;
        atomic_store(&s_link.rx_pending, s_link.rx_count);
    }
}

static void link_inject(uint32_t ev)
{
    pthread_mutex_lock(&s_link_mutex);
    link_push_locked(ev);
    pthread_mutex_unlock(&s_link_mutex);
}

static void link_send(uint32_t ev)
{
    void (*notify)(void);
    if (!s_link.online) return;
    pthread_mutex_lock(&s_link_mutex);
    if (s_link.tx_count + 4 <= LINK_TXQ) {
        int i;
        for (i = 0; i < 4; i++) {
            s_link.txq[(s_link.tx_head + s_link.tx_count) % LINK_TXQ] = (BYTE)(ev >> (24 - 8 * i));
            s_link.tx_count++;
        }
    }
    notify = s_link.notify;
    pthread_mutex_unlock(&s_link_mutex);
    if (notify) notify();
}

// ---- public link API (any thread) ------------------------------------------
void SCC_LinkSetConnected(int connected)
{
    pthread_mutex_lock(&s_link_mutex);
    s_link.tx_head = s_link.tx_count = 0;
    s_link.partial_len = 0;
    link_push_locked((uint32_t)(connected ? LEV_CONNECT : LEV_DISCONNECT) << 24);
    pthread_mutex_unlock(&s_link_mutex);
}

void SCC_LinkFeed(const unsigned char *data, int len)
{
    int i;
    pthread_mutex_lock(&s_link_mutex);
    for (i = 0; i < len; i++) {
        s_link.partial[s_link.partial_len++] = data[i];
        if (s_link.partial_len == 4) {
            uint32_t ev = ((uint32_t)s_link.partial[0] << 24) | ((uint32_t)s_link.partial[1] << 16)
                        | ((uint32_t)s_link.partial[2] << 8) | s_link.partial[3];
            int type = (int)(ev >> 24);
            s_link.partial_len = 0;
            if (type >= LEV_DATA && type <= LEV_FORMAT) link_push_locked(ev);
        }
    }
    pthread_mutex_unlock(&s_link_mutex);
}

int SCC_LinkDrain(unsigned char *buf, int max)
{
    int n = 0;
    pthread_mutex_lock(&s_link_mutex);
    while (n < max && s_link.tx_count > 0) {
        buf[n++] = s_link.txq[s_link.tx_head];
        s_link.tx_head = (s_link.tx_head + 1) % LINK_TXQ;
        s_link.tx_count--;
    }
    pthread_mutex_unlock(&s_link_mutex);
    return n;
}

void SCC_LinkSetTxNotify(void (*notify)(void))
{
    pthread_mutex_lock(&s_link_mutex);
    s_link.notify = notify;
    pthread_mutex_unlock(&s_link_mutex);
}

// ---- line settings ---------------------------------------------------------
static int scca_data_bits(BYTE code)
{
    switch (code & 3) {
        case 0: return 5;
        case 1: return 7;
        case 2: return 6;
        default: return 8;
    }
}

static int scca_baud(void)
{
    static const int clock_mode[4] = {1, 16, 32, 64};
    int tc = WRA[12] | (WRA[13] << 8);
    int baud;
    if (!(WRA[14] & 0x01)) return 9600;      // baud rate generator off
    baud = SCCA_PCLK / (2 * (tc + 2) * clock_mode[WRA[4] >> 6]);
    return (baud > 0) ? baud : 9600;
}

static int scca_parity(void)
{
    if (!(WRA[4] & 0x01)) return 0;
    return (WRA[4] & 0x02) ? 2 : 1;          // 1 = odd, 2 = even
}

static int scca_stop_x2(void)
{
    switch ((WRA[4] >> 2) & 3) {
        case 2: return 3;
        case 3: return 4;
        default: return 2;
    }
}

static int scca_format(int data_bits)
{
    return (data_bits << 16) | (scca_parity() << 8) | scca_stop_x2();
}

// Clocks one character takes on the line.
static int scca_char_clk(void)
{
    int bits_x2 = 2 * (1 + scca_data_bits(WRA[5] >> 5) + (scca_parity() ? 1 : 0)) + scca_stop_x2();
    return (int)(((long long)bits_x2 * SCC_TIMER_HZ) / (2LL * scca_baud()));
}

static int scca_out_lines(void)
{
    return ((WRA[5] & 0x02) ? LINE_RTS : 0) | ((WRA[5] & 0x80) ? LINE_DTR : 0)
         | ((WRA[5] & 0x10) ? LINE_BREAK : 0);
}

// Tell the peer about our output pins and line settings when they change.
static void scca_sync_outputs(int force)
{
    int baud, format, lines;
    if (!s_link.online) return;
    baud = scca_baud();
    format = scca_format(scca_data_bits(WRA[5] >> 5));
    lines = scca_out_lines();
    if (force || baud != s_link.sent_baud) {
        s_link.sent_baud = baud;
        link_send(((uint32_t)LEV_BAUD << 24) | ((uint32_t)baud & 0xffffff));
    }
    if (force || format != s_link.sent_format) {
        s_link.sent_format = format;
        link_send(((uint32_t)LEV_FORMAT << 24) | (uint32_t)format);
    }
    if (force || lines != s_link.sent_lines) {
        s_link.sent_lines = lines;
        link_send(((uint32_t)LEV_LINES << 24) | (uint32_t)lines);
    }
}

// ---- interrupts ------------------------------------------------------------
static int scca_rx_special(void)
{
    BYTE mask = RXERR_OVERRUN | RXERR_FRAMING | ((WRA[1] & 0x04) ? RXERR_PARITY : 0);
    return scca.rx_count > 0 && (scca.rx_err[0] & mask);
}

static int scca_rx_ip(void)
{
    switch ((WRA[1] >> 3) & 3) {
        case 1: return scca.rx_count > 0 && (scca.rx_first_hit || scca_rx_special());
        case 2: return scca.rx_count > 0;
        case 3: return scca_rx_special();
        default: return 0;
    }
}

// Status code of the highest priority channel A interrupt (as in the vector),
// or -1 when channel A is not requesting one.
static int scca_irq_code(void)
{
    if (!(SCC_RegsB[9] & 0x08)) return -1;                 // master interrupt enable
    if (scca_rx_ip()) return scca_rx_special() ? 7 : 6;
    if (scca.tx_ip && (WRA[1] & 0x02)) return 4;
    if (scca.ext_ip && (WRA[1] & 0x01)) return 5;
    return -1;
}

static int sccb_mouse_irq_wanted(void)
{
    if (!(SCC_RegsB[9] & 0x08)) return 0;
    if (SCC_DatNum && ((SCC_RegsB[1] & 0x18) == 0x10)) return 1;
    if ((SCC_DatNum == 3) && ((SCC_RegsB[1] & 0x18) == 0x08)) return 1;
    return 0;
}

// Keep the level 5 request in step with channel A, without disturbing a
// request the mouse channel has made.
static void scca_int_update(void)
{
    if (scca_irq_code() >= 0) {
        if (!IRQH_IRQ[5]) IRQH_Int(5, &SCC_Int);
        scca.irq_raised = 1;
    } else if (scca.irq_raised) {
        scca.irq_raised = 0;
        if (IRQH_IRQ[5] && !sccb_mouse_irq_wanted()) IRQH_IRQCallBack(5);
    }
}

// ---- modem inputs ----------------------------------------------------------
static void scca_set_pins(BYTE pins)
{
    BYTE changed = (BYTE)((scca.pins ^ pins) & WRA[15] & RR0_PINS);
    scca.pins = pins;
    if (changed && (WRA[1] & 0x01) && !scca.rr0_latched) {
        scca.rr0_latch = pins;
        scca.rr0_latched = 1;
        scca.ext_ip = 1;
    }
}

static void scca_apply_peer_lines(BYTE lines)
{
    BYTE pins = 0;
    s_link.peer_lines = lines;
    if (lines & LINE_RTS) pins |= RR0_CTS;
    if (lines & LINE_DTR) pins |= RR0_DCD | RR0_SYNC;
    if (lines & LINE_BREAK) pins |= RR0_BREAK;
    scca_set_pins(pins);
}

static void scca_reset_ext_status(void)
{
    scca.rr0_latched = 0;
    scca.ext_ip = 0;
    // A change that happened while the latch was closed is reported now.
    if (((scca.rr0_latch ^ scca.pins) & WRA[15] & RR0_PINS) && (WRA[1] & 0x01)) {
        scca.rr0_latch = scca.pins;
        scca.rr0_latched = 1;
        scca.ext_ip = 1;
    }
}

// ---- transmitter -----------------------------------------------------------
static int scca_tx_allowed(void)
{
    if (!(WRA[5] & 0x08)) return 0;                              // Tx enable
    if ((WRA[3] & 0x20) && !(scca.pins & RR0_CTS)) return 0;     // auto enables: CTS gates Tx
    return 1;
}

static void scca_tx_load(void)
{
    if (scca.tx_busy || !scca.tx_buf_full || !scca_tx_allowed()) return;
    scca.tx_shift = (BYTE)(scca.tx_buf & ((1 << scca_data_bits(WRA[5] >> 5)) - 1));
    scca.tx_buf_full = 0;
    scca.tx_busy = 1;
    scca.tx_clk += scca_char_clk();
    scca.tx_ip = 1;                                              // buffer became empty
}

static void scca_emit_char(BYTE c)
{
    s_link.tx_chars++;
    link_send(((uint32_t)LEV_DATA << 24) | c);
    if (scc_port_b.connected && scc_port_b.mode != SCC_MODE_MOUSE_ONLY) {
        SCC_SendByte(&scc_port_b, c);
    }
}

static void scca_write_data(BYTE data)
{
    scca.tx_buf = data;
    scca.tx_buf_full = 1;
    scca.tx_ip = 0;
    if (!scca.tx_busy) scca.tx_clk = 0;
    scca_tx_load();
}

// ---- receiver --------------------------------------------------------------
static int scca_rx_enabled(void)
{
    if (!(WRA[3] & 0x01)) return 0;                              // Rx enable
    if ((WRA[3] & 0x20) && !(scca.pins & RR0_DCD)) return 0;     // auto enables: DCD gates Rx
    return 1;
}

// The peer's settings differ from ours: on a real line the character would
// arrive broken.
static int scca_peer_mismatch(void)
{
    int baud, mine, theirs;
    if (!s_link.peer_baud || !s_link.peer_format) return 0;
    baud = scca_baud();
    if (abs(s_link.peer_baud - baud) * 16 > baud) return 1;      // more than about 6% apart
    mine = scca_format(scca_data_bits(WRA[3] >> 6)) >> 8;
    theirs = s_link.peer_format >> 8;                            // stop bits do not matter
    return mine != theirs;
}

// Take one character from the line. Returns 0 when it cannot arrive yet.
static int scca_accept_char(BYTE c, int backlog)
{
    int char_clk = scca_char_clk();
    BYTE err = 0;
    if (scca.rx_gap > 0) return 0;
    if (s_link.online && !(WRA[5] & 0x02) && scca.rts_off_clk < RTS_HOLD_CLK) return 0;
    if (scca_rx_enabled()) {
        if (scca.rx_count >= 3) {
            // The link waits for room rather than overrunning at once, but a
            // program that has stopped reading does lose characters.
            if (scca.rx_full_clk < 16 * char_clk) return 0;
            scca.rx_count = 2;
            err |= RXERR_OVERRUN;
        }
        if (scca_peer_mismatch()) {
            err |= RXERR_FRAMING;
            s_link.mismatches++;
        }
        scca.rx_data[scca.rx_count] = (BYTE)(c & ((1 << scca_data_bits(WRA[3] >> 6)) - 1));
        scca.rx_err[scca.rx_count] = err;
        scca.rx_count++;
        scca.rx_full_clk = 0;
        if (scca.rx_first_armed) {
            scca.rx_first_armed = 0;
            scca.rx_first_hit = 1;
        }
        s_link.rx_chars++;
    }
    // Characters follow each other at the line rate; catch up a little when
    // the two machines' clocks have drifted and a backlog has built up.
    scca.rx_gap += (backlog > 32) ? (char_clk - char_clk / 4) : char_clk;
    return 1;
}

static BYTE scca_read_data(void)
{
    BYTE data;
    if (scca.rx_count == 0) return 0;
    data = scca.rx_data[0];
    scca.rr1_err |= scca.rx_err[0] & (RXERR_PARITY | RXERR_OVERRUN);
    scca.rx_data[0] = scca.rx_data[1]; scca.rx_err[0] = scca.rx_err[1];
    scca.rx_data[1] = scca.rx_data[2]; scca.rx_err[1] = scca.rx_err[2];
    scca.rx_count--;
    if (scca.rx_count == 0) scca.rx_first_hit = 0;
    return data;
}

static void scca_process_link(void)
{
    for (;;) {
        uint32_t ev;
        int backlog;
        pthread_mutex_lock(&s_link_mutex);
        backlog = s_link.rx_count;
        ev = backlog ? s_link.rxq[s_link.rx_head] : 0;
        pthread_mutex_unlock(&s_link_mutex);
        if (!backlog) break;

        switch (ev >> 24) {
            case LEV_DATA:
                if (!scca_accept_char((BYTE)ev, backlog)) return;    // stays queued
                break;
            case LEV_LINES:
                scca_apply_peer_lines((BYTE)ev);
                break;
            case LEV_BAUD:
                s_link.peer_baud = (int)(ev & 0xffffff);
                break;
            case LEV_FORMAT:
                s_link.peer_format = (int)(ev & 0xffffff);
                break;
            case LEV_CONNECT:
                s_link.online = 1;
                s_link.peer_baud = s_link.peer_format = 0;
                s_link.tx_chars = s_link.rx_chars = s_link.mismatches = 0;
                scca_apply_peer_lines(0);
                scca_sync_outputs(1);
                break;
            case LEV_DISCONNECT:
                s_link.online = 0;
                s_link.peer_baud = s_link.peer_format = 0;
                scca_apply_peer_lines(0);                            // the cable was pulled out
                break;
        }

        pthread_mutex_lock(&s_link_mutex);
        s_link.rx_head = (s_link.rx_head + 1) % LINK_RXQ;
        s_link.rx_count--;
        atomic_store(&s_link.rx_pending, s_link.rx_count);
        pthread_mutex_unlock(&s_link_mutex);
    }
}

// ---- registers -------------------------------------------------------------
static void scca_reset(void)
{
    BYTE pins = scca.pins;
    memset(&scca, 0, sizeof(scca));
    memset(WRA, 0, sizeof(SCC_RegsA));
    WRA[4] = 0x04;
    WRA[15] = 0xf8;
    scca.pins = pins;                                            // still wired to the peer
    scca_sync_outputs(0);
}

static BYTE scca_rr0(void)
{
    BYTE ret = (BYTE)(((scca.rr0_latched ? scca.rr0_latch : scca.pins) & RR0_PINS) | RR0_TX_UNDERRUN);
    if (scca.rx_count) ret |= RR0_RX_AVAIL;
    if (!scca.tx_buf_full) ret |= RR0_TX_EMPTY;
    return ret;
}

static BYTE scca_rr1(void)
{
    BYTE ret = 0x06;                                             // residue code, as in async modes
    if (!scca.tx_busy && !scca.tx_buf_full) ret |= 0x01;         // all sent
    ret |= scca.rr1_err;
    if (scca.rx_count) ret |= scca.rx_err[0];
    return ret;
}

static BYTE scca_rr3(void)
{
    BYTE ret = 0;
    if (scca_rx_ip()) ret |= 0x20;
    if (scca.tx_ip) ret |= 0x10;
    if (scca.ext_ip) ret |= 0x08;
    // Mouse channel bits, as this core has always reported them.
    if (SCC_DatNum) ret |= g_scc_mouse_compat_mode ? 0x04 : 0x01;
    return ret;
}

static BYTE scca_read_reg(int reg)
{
    switch (reg & 15) {
        case 0: case 4:   return scca_rr0();
        case 1: case 5:   return scca_rr1();
        case 2: case 6:   return SCC_Vector;
        case 3: case 7:   return scca_rr3();
        case 8:           return scca_read_data();
        case 12:          return WRA[12];
        case 9: case 13:  return WRA[13];
        case 11: case 15: return (BYTE)(WRA[15] & 0xfa);
        default:          return 0;
    }
}

// WR9 is shared by both channels; its top bits reset them.
static void scc_write_wr9(BYTE data)
{
    SCC_RegsB[9] = data;
    if (data & 0x80) scca_reset();                               // channel A or hardware reset
}

static void scca_write_reg(int reg, BYTE data)
{
    scca_trace("WR", reg, data);
    switch (reg) {
        case 2:
            SCC_RegsB[2] = data;
            SCC_Vector = data;
            break;
        case 8:
            scca_write_data(data);
            return;
        case 9:
            WRA[9] = data;
            scc_write_wr9(data);
            return;
        default:
            break;
    }
    WRA[reg] = data;
    switch (reg) {
        case 1:
            if (((data >> 3) & 3) == 1) scca.rx_first_armed = 1;
            break;
        case 3:
        case 5:
            scca_tx_load();                                      // Tx enable or auto enables changed
            break;
        default:
            break;
    }
    scca_sync_outputs(0);
}

static void scca_write_command(BYTE data)
{
    if (scca.ptr) {
        int reg = scca.ptr;
        scca.ptr = 0;
        scca_write_reg(reg, data);
        return;
    }
    scca.ptr = data & 7;
    if (data & 0xf8) scca_trace("WR", 0, data);
    switch ((data >> 3) & 7) {
        case 1: scca.ptr |= 8; break;                            // point high
        case 2: scca_reset_ext_status(); break;
        case 4: scca.rx_first_armed = 1; break;                  // interrupt on next Rx character
        case 5: scca.tx_ip = 0; break;                           // reset Tx interrupt pending
        case 6: scca.rr1_err = 0; break;                         // error reset
        default: break;
    }
}

// ---- time ------------------------------------------------------------------
// Called from the main loop with the 10 MHz base clocks the CPU just ran.
void SCC_Timer(int clk)
{
    if (clk <= 0) return;

    if (scca.tx_busy) {
        scca.tx_clk -= clk;
        if (scca.tx_clk <= 0) {
            scca.tx_busy = 0;
            scca_emit_char(scca.tx_shift);
            scca_tx_load();
            if (!scca.tx_busy) scca.tx_clk = 0;
        }
    } else if (scca.tx_buf_full) {
        scca_tx_load();                                          // CTS may have come back
    }

    if (WRA[5] & 0x02) scca.rts_off_clk = 0;
    else if (scca.rts_off_clk < RTS_HOLD_CLK) scca.rts_off_clk += clk;
    if (scca.rx_gap > 0) scca.rx_gap -= clk;
    if (scca.rx_count < 3) scca.rx_full_clk = 0;
    else if (scca.rx_full_clk < 0x40000000) scca.rx_full_clk += clk;

    if (atomic_load(&s_link.rx_pending)) scca_process_link();
    scca_int_update();
}

// ---- status for the host UI ------------------------------------------------
int SCC_LinkGetInfo(int item)
{
    switch (item) {
        case SCC_LINK_INFO_ONLINE:      return s_link.online;
        case SCC_LINK_INFO_BAUD:        return scca_baud();
        case SCC_LINK_INFO_FORMAT:      return scca_format(scca_data_bits(WRA[5] >> 5));
        case SCC_LINK_INFO_PEER_BAUD:   return s_link.peer_baud;
        case SCC_LINK_INFO_PEER_FORMAT: return s_link.peer_format;
        case SCC_LINK_INFO_LINES:       return scca_out_lines();
        case SCC_LINK_INFO_PEER_LINES:  return s_link.peer_lines;
        case SCC_LINK_INFO_TX_CHARS:    return (int)s_link.tx_chars;
        case SCC_LINK_INFO_RX_CHARS:    return (int)s_link.rx_chars;
        case SCC_LINK_INFO_MISMATCHES:  return (int)s_link.mismatches;
        default:                        return 0;
    }
}

// ============================================================================
// Interrupt handling
// ============================================================================
static DWORD FASTCALL SCC_Int(BYTE irq)
{
    DWORD ret = (DWORD)(-1);
    IRQH_IRQCallBack(irq);
    if ( (irq==5)&&(!(SCC_RegsB[9]&2)) ) {
        // Channel A outranks channel B; 2 is "channel B receive" (the mouse).
        int code = scca_irq_code();
        if (code >= 0) scca_trace("IRQ", code, 0);
        if (code < 0) code = 2;
        if (SCC_RegsB[9]&1) {
            if (SCC_RegsB[9]&0x10)
                ret = (DWORD)((SCC_Vector&0x8f) | ((code&4)<<2) | ((code&2)<<4) | ((code&1)<<6));
            else
                ret = (DWORD)((SCC_Vector&0xf1) | (code<<1));
        } else {
            ret = ((DWORD)SCC_Vector);
        }
    }
    return ret;
}

void SCC_IntCheck(void)
{
    // Channel B (mouse) interrupt check
    if (sccb_mouse_irq_wanted()) {
        IRQH_Int(5, &SCC_Int);
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
            // Channel A takes them at the line rate on the emulation thread.
            for (ssize_t i = 0; i < n; i++) {
                link_inject(((uint32_t)LEV_DATA << 24) | buffer[i]);
            }
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
        
        if (pthread_create(&scc_port_b.accept_thread, NULL, SCC_AcceptThread, &scc_port_b) != 0) {
            SCC_CloseSerial();
            return -1;
        }
    }

    // A host device is attached: it is always ready (CTS, DSR and DCD asserted).
    link_inject(((uint32_t)LEV_LINES << 24) | LINE_RTS | LINE_DTR);
    return 0;
}

void SCC_CloseSerial(void)
{
    if (scc_port_b.connected) {
        scc_port_b.connected = 0;
        link_inject((uint32_t)LEV_LINES << 24);
        
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
    
    // Host serial backend: set up once. SCC_Init() also runs on every reset,
    // which must not forget an open backend or the link to another machine.
    static int backend_initialized = 0;
    if (!backend_initialized) {
        backend_initialized = 1;
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

    // RS-232C (channel A): the reset drops RTS and DTR, which the peer sees.
    scca_reset();

    // Optional: enable compat mode via environment variable
    // Set SCC_MOUSE_COMPAT=1 to force original px68k behavior
    const char* compatEnv = getenv("SCC_MOUSE_COMPAT");
    if (compatEnv && (compatEnv[0] == '1' || compatEnv[0] == 't' || compatEnv[0] == 'T' || compatEnv[0] == 'y' || compatEnv[0] == 'Y')) {
        g_scc_mouse_compat_mode = 1;
    }
    const char* traceEnv = getenv("SCC_MOUSE_TRACE");
    if (traceEnv && (traceEnv[0] == '1' || traceEnv[0] == 't' || traceEnv[0] == 'T' || traceEnv[0] == 'y' || traceEnv[0] == 'Y')) {
        g_scc_trace = 1;
        printf("[scc.c] SCC mouse trace enabled\n");
    }
    const char* edgeEnv = getenv("SCC_MOUSE_EDGELOG");
    if (edgeEnv && (edgeEnv[0] == '1' || edgeEnv[0] == 't' || edgeEnv[0] == 'T' || edgeEnv[0] == 'y' || edgeEnv[0] == 'Y')) {
        g_scc_edgelog = 1;
        printf("[scc.c] SCC mouse edge log enabled\n");
    }
    const char* rs232cTraceEnv = getenv("SCC_RS232C_TRACE");
    g_scca_trace = (rs232cTraceEnv && rs232cTraceEnv[0] == '1');
    // Clear mouse queue
    g_mouse_q_head = g_mouse_q_tail = g_mouse_q_cnt = 0;
    g_last_pub_st = 0;
    g_last_pub_x = 0;
    g_last_pub_y = 0;
}

// ============================================================================
// I/O Functions
// ============================================================================
// Reduce SCC verbose logging by default
// #define SCC_DEBUG 1
void FASTCALL SCC_Write(DWORD adr, BYTE data)
{
    if (adr>=0xe98008) return;
    
    // Port B Control (0xE98001) - Mouse channel (compat with original px68k)
    if ((adr&7) == 1) {
        if (SCC_RegSetB) {
            if (SCC_RegNumB == 5) {
                // Mouse RTS control on Port B (raise edge)
                int rtsRising = (!(SCC_RegsB[5]&2)) && (data&2);
                if (g_scc_trace) {
                    printf("[scc.c] WR5 write data=0x%02X rtsRising=%d pend=%d q=%d compat=%d\n",
                           data, rtsRising, SCC_DatNum, g_mouse_q_cnt, g_scc_mouse_compat_mode);
                }
                if (g_scc_mouse_compat_mode) {
                    // Strict original behavior: require RX enabled and no pending data
                    int rxEnabled = (SCC_RegsB[3] & 0x01) != 0;
                    if (rtsRising && rxEnabled && (!SCC_DatNum)) {
                        Mouse_SetData();
                        // In compat mode, publish packet if there's any change (movement or button)
                        if (MouseSt != g_last_pub_st || MouseX != g_last_pub_x || MouseY != g_last_pub_y) {
                            SCC_DatNum = 3;
                            SCC_Dat[2] = MouseSt;
                            SCC_Dat[1] = (BYTE)MouseX;
                            SCC_Dat[0] = (BYTE)MouseY;
                            g_last_pub_st = MouseSt;
                            g_last_pub_x = MouseX;
                            g_last_pub_y = MouseY;
                            if (g_scc_trace || g_scc_edgelog) {
                                printf("[scc.c] PUBLISH t=%lld st=0x%02X x=%d y=%d\n", scc_now_ms(), MouseSt, MouseX, MouseY);
                            }
                            SCC_IntCheck();
                        } else {
                            if (g_scc_trace || g_scc_edgelog) {
                                printf("[scc.c] PUBLISH skip (no change) t=%lld st=0x%02X x=%d y=%d\n", scc_now_ms(), MouseSt, MouseX, MouseY);
                            }
                        }
                    }
                } else {
                    // Enhanced behavior with edge queue
                    if (rtsRising) {
                        if (!SCC_DatNum) {
                            BYTE st, x, y;
                            if (mouse_q_deq(&st, &x, &y)) {
                                // Drain movement before publishing queued button-only
                                Mouse_SetData();
                                SCC_DatNum = 3;
                                SCC_Dat[2] = st;
                                SCC_Dat[1] = 0;
                                SCC_Dat[0] = 0;
                                if (g_scc_trace) {
                                    printf("[scc.c] RTS: publish QUEUED button-only st=0x%02X\n", st);
                                }
                            } else {
                                Mouse_SetData();
                                SCC_DatNum = 3;
                                SCC_Dat[2] = MouseSt;
                                SCC_Dat[1] = (BYTE)MouseX;
                                SCC_Dat[0] = (BYTE)MouseY;
                                if (g_scc_trace) {
                                    printf("[scc.c] RTS: publish FRESH st=0x%02X x=%d y=%d\n", MouseSt, MouseX, MouseY);
                                }
                            }
                            SCC_IntCheck();
                        } else {
                            BYTE st = MouseSt;
                            mouse_q_enq(st, 0, 0);
                            if (g_scc_trace) {
                                printf("[scc.c] RTS: pending present -> LATCH st=0x%02X (q=%d)\n", st, g_mouse_q_cnt);
                            }
                        }
                    }
                }
            } else if (SCC_RegNumB == 2) {
                SCC_Vector = data;
            } else if (SCC_RegNumB == 9) {
                scc_write_wr9(data);
                scca_int_update();
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
    // Channel B data (0xE98003): nothing is sent to the mouse
    else if ((adr&7) == 3) {
    }
    // Channel A command (0xE98005): RS-232C
    else if ((adr&7) == 5) {
        scca_write_command(data);
        scca_int_update();
    }
    // Channel A data (0xE98007): RS-232C
    else if ((adr&7) == 7) {
        scca_trace("TX", 8, data);
        scca_write_data(data);
        scca_int_update();
    }
}

BYTE FASTCALL SCC_Read(DWORD adr)
{
    BYTE ret = 0;
    if (adr>=0xe98008) return ret;
    
    // Port B Control (0xE98001)
    if ((adr&7) == 1) {
        if (!SCC_RegNumB) {
            // For mouse (Port A) availability, mirror original behavior: bit0 reflects SCC_DatNum
            ret = (SCC_DatNum ? 1 : 0);
#ifdef SCC_DEBUG
            printf("[scc.c] Read PortB CTRL (RR0) -> ret=0x%02X (SCC_DatNum=%d)\n", ret, SCC_DatNum);
#endif
        }
        SCC_RegNumB = 0;
        SCC_RegSetB = 0;
    }
    // Port B Data (0xE98003) - Return mouse packet bytes (compat) when in mouse-only mode
    else if ((adr&7) == 3) {
//        if (SCC_DatNum == 0) {
            if (SCC_DatNum) {
            SCC_DatNum--;
            ret = SCC_Dat[SCC_DatNum];
            if (g_scc_trace) {
                printf("[scc.c] PB DATA read -> idx=%d val=0x%02X pend=%d\n", SCC_DatNum, ret, SCC_DatNum);
            }

            // Populate packet on demand
            // Mouse_SetData();
            // SCC_DatNum = 3;
            // SCC_Dat[2] = MouseSt;
            // SCC_Dat[1] = (BYTE)MouseX;
            // SCC_Dat[0] = (BYTE)MouseY;

             #ifdef SCC_DEBUG
            printf("[scc.c] PB DATA populate: MouseSt=0x%02X X=%d Y=%d\n", MouseSt, MouseX, MouseY);
#endif
        }

//        SCC_DatNum--;
//        ret = SCC_Dat[SCC_DatNum];

#ifdef SCC_DEBUG
        printf("[scc.c] PB DATA read -> byte=%d value=0x%02X (remaining=%d)\n", SCC_DatNum, ret, SCC_DatNum);
#endif
    }
    // Channel A command (0xE98005): RS-232C
    else if ((adr&7) == 5) {
        ret = scca_read_reg(scca.ptr);
        scca_trace("RR", scca.ptr, ret);
        scca.ptr = 0;
        scca_int_update();
    }
    // Channel A data (0xE98007): RS-232C
    else if ((adr&7) == 7) {
        ret = scca_read_data();
        scca_trace("RX", 8, ret);
        scca_int_update();
    }
    
    return ret;
}
