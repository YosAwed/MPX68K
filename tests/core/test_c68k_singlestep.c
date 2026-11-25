/* Runs the SingleStepTests/680x0 vectors (https://github.com/SingleStepTests/680x0,
 * 68000/v1/*.json.gz) against the c68k core. Each vector gives the CPU and RAM
 * state before and after one instruction; this loads the first, executes one
 * instruction and compares with the second.
 *
 * c68k models neither address errors nor trace, so vectors that end in an
 * address error exception or start with T set are counted apart from the rest.
 * PC is compared on its low 24 bits: c68k does not keep the upper byte, which
 * the 68000 never puts on the bus.
 *
 * usage: test_c68k_singlestep [-v N] FILE.json.gz...
 *   -v N  print the first N mismatches of each file in detail
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "c68k/c68k.h"

#define MEM_SIZE 0x1000000
static unsigned char *mem;                 /* bytes swapped in pairs, as the emulator keeps RAM */
static u32 dirty[4096];
static int ndirty;

static void touch(u32 adr) { if (ndirty < (int)(sizeof(dirty) / sizeof(dirty[0]))) dirty[ndirty++] = adr; }
static void put8(u32 adr, u32 v) { adr &= 0xffffff; mem[adr ^ 1] = (unsigned char)v; touch(adr); }
static u32 get8(u32 adr) { return mem[(adr & 0xffffff) ^ 1]; }

static u32 FASTCALL read_byte(const u32 adr) { return get8(adr); }
static u32 FASTCALL read_word(const u32 adr) { return (get8(adr) << 8) | get8(adr + 1); }
static void FASTCALL write_byte(const u32 adr, u32 data) { put8(adr, data); }
static void FASTCALL write_word(const u32 adr, u32 data) { put8(adr, data >> 8); put8(adr + 1, data); }
static s32 FASTCALL int_ack(s32 level) { (void)level; return C68K_INT_ACK_AUTOVECTOR; }

/* ---- a cursor over the JSON text; the vectors use a small fixed shape ---- */
static const char *p;

static void ws(void) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',' || *p == ':') p++; }
static void expect(char c)
{
    ws();
    if (*p != c) { fprintf(stderr, "parse error: expected '%c' near \"%.40s\"\n", c, p); exit(2); }
    p++;
}
static void string(char *out, size_t n)
{
    size_t i = 0;
    expect('"');
    while (*p && *p != '"') { if (i + 1 < n) out[i++] = *p; p++; }
    out[i] = 0;
    p++;
}
static u32 number(void) { char *end; ws(); u32 v = (u32)strtoul(p, &end, 10); p = end; return v; }
static void skip(void)
{
    char tmp[256];
    ws();
    if (*p == '"') { string(tmp, sizeof(tmp)); return; }
    if (*p == '[' || *p == '{') {
        char close = *p == '[' ? ']' : '}';
        p++;
        for (ws(); *p != close; ws()) skip();
        p++;
        return;
    }
    number();
}

typedef struct { u32 d[8], a[7], usp, ssp, sr, pc, prefetch[2]; int nram; u32 ram[512][2]; } state_t;
typedef struct { char name[128]; state_t in, out; u32 length; int address_error; } test_t;

static void parse_state(state_t *s)
{
    char key[32];
    memset(s, 0, sizeof(*s));
    expect('{');
    for (ws(); *p != '}'; ws()) {
        string(key, sizeof(key));
        if (key[0] == 'd' && key[1] >= '0' && key[1] <= '7' && !key[2]) s->d[key[1] - '0'] = number();
        else if (key[0] == 'a' && key[1] >= '0' && key[1] <= '6' && !key[2]) s->a[key[1] - '0'] = number();
        else if (!strcmp(key, "usp")) s->usp = number();
        else if (!strcmp(key, "ssp")) s->ssp = number();
        else if (!strcmp(key, "sr")) s->sr = number();
        else if (!strcmp(key, "pc")) s->pc = number();
        else if (!strcmp(key, "prefetch")) { expect('['); s->prefetch[0] = number(); s->prefetch[1] = number(); expect(']'); }
        else if (!strcmp(key, "ram")) {
            expect('[');
            for (ws(); *p != ']'; ws()) {
                expect('[');
                u32 adr = number(), v = number();
                if (s->nram < 512) { s->ram[s->nram][0] = adr; s->ram[s->nram][1] = v; s->nram++; }
                expect(']');
            }
            p++;
        }
        else skip();
    }
    p++;
}

/* An address error pushes a 14-byte frame (7 word writes) and then reads
 * vector 3 as the words at $00000C and $00000E. Requiring the whole sequence
 * keeps an ordinary read of address 12 in the normal accounting. */
static int parse_transactions(void)
{
    char kind[8], size[8];
    int writes = 0, read_c = 0, ae = 0;
    expect('[');
    for (ws(); *p != ']'; ws()) {
        expect('[');
        string(kind, sizeof(kind));
        if (kind[0] == 'n') { number(); }
        else {
            number();                    /* cycles */
            number();                    /* function code */
            u32 adr = number() & 0xffffff;
            string(size, sizeof(size));
            number();
            int word_read = kind[0] == 'r' && size[1] == 'w';
            if (word_read && read_c && adr == 0x00000e) ae = 1;
            read_c = word_read && adr == 0x00000c && writes >= 7;
            if (kind[0] == 'w') writes++;
        }
        expect(']');
    }
    p++;
    return ae;
}

static int parse_test(test_t *t)
{
    char key[32];
    ws();
    if (*p != '{') return 0;
    memset(t, 0, sizeof(*t));
    p++;
    for (ws(); *p != '}'; ws()) {
        string(key, sizeof(key));
        if (!strcmp(key, "name")) string(t->name, sizeof(t->name));
        else if (!strcmp(key, "initial")) parse_state(&t->in);
        else if (!strcmp(key, "final")) parse_state(&t->out);
        else if (!strcmp(key, "length")) t->length = number();
        else if (!strcmp(key, "transactions")) t->address_error = parse_transactions();
        else skip();
    }
    p++;
    return 1;
}

/* ---- running one vector ---- */
typedef struct { int pass, fail, ae_total, ae_fail, trace_total, trace_fail, cycles_off; int field[24]; } stats_t;
static const char *field_names[] = { "d0","d1","d2","d3","d4","d5","d6","d7","a0","a1","a2","a3","a4","a5","a6","usp","ssp","sr","pc","ram" };

static void load(const state_t *s)
{
    for (int i = 0; i < ndirty; i++) mem[dirty[i] ^ 1] = 0;
    ndirty = 0;
    for (int i = 0; i < s->nram; i++) put8(s->ram[i][0], s->ram[i][1]);
    put8(s->pc, s->prefetch[0] >> 8); put8(s->pc + 1, s->prefetch[0]);
    put8(s->pc + 2, s->prefetch[1] >> 8); put8(s->pc + 3, s->prefetch[1]);

    C68K.Status = 0;
    C68K.BusError = 0;
    C68K.IRQLine = 0;
    for (int i = 0; i < 8; i++) C68k_Set_DReg(&C68K, i, s->d[i]);
    for (int i = 0; i < 7; i++) C68k_Set_AReg(&C68K, i, s->a[i]);
    C68k_Set_SR(&C68K, s->sr);
    if (s->sr & 0x2000) { C68K.A[7] = s->ssp; C68K.USP = s->usp; }
    else { C68K.A[7] = s->usp; C68K.USP = s->ssp; }
    C68k_Set_PC(&C68K, s->pc);
}

/* Returns a bitmask of the fields that differ (bit 19 = RAM). */
static u32 compare(const state_t *s, u32 got[19])
{
    u32 want[19], bad = 0;
    int sup = (C68k_Get_SR(&C68K) & 0x2000) != 0;
    for (int i = 0; i < 8; i++) { got[i] = C68k_Get_DReg(&C68K, i); want[i] = s->d[i]; }
    for (int i = 0; i < 7; i++) { got[8 + i] = C68k_Get_AReg(&C68K, i); want[8 + i] = s->a[i]; }
    got[15] = sup ? C68K.USP : C68K.A[7];
    got[16] = sup ? C68K.A[7] : C68K.USP;
    got[17] = C68k_Get_SR(&C68K);
    got[18] = C68k_Get_PC(&C68K) & 0xffffff;
    want[15] = s->usp; want[16] = s->ssp; want[17] = s->sr; want[18] = s->pc & 0xffffff;   /* c68k keeps only the 24 bits on the bus */
    for (int i = 0; i < 19; i++) if (got[i] != want[i]) bad |= 1u << i;
    for (int i = 0; i < s->nram; i++) if (get8(s->ram[i][0]) != s->ram[i][1]) bad |= 1u << 19;
    return bad;
}

static void report(const test_t *t, u32 bad, const u32 got[19], s32 cycles)
{
    const u32 *w = t->out.d;
    printf("  FAIL %s\n", t->name);
    for (int i = 0; i < 19; i++) {
        if (!(bad & (1u << i))) continue;
        u32 want = i < 8 ? w[i] : i < 15 ? t->out.a[i - 8] : i == 15 ? t->out.usp : i == 16 ? t->out.ssp : i == 17 ? t->out.sr : t->out.pc;
        printf("    %-4s want %08x got %08x\n", field_names[i], want, got[i]);
    }
    if (bad & (1u << 19))
        for (int i = 0; i < t->out.nram; i++)
            if (get8(t->out.ram[i][0]) != t->out.ram[i][1])
                printf("    ram[%06x] want %02x got %02x\n", t->out.ram[i][0], t->out.ram[i][1], get8(t->out.ram[i][0]));
    printf("    cycles want %u got %d\n", t->length, cycles);
}

static char *slurp_gz(const char *path)
{
    gzFile f = gzopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    size_t cap = 1 << 24, len = 0;
    char *buf = malloc(cap);
    int n;
    while ((n = gzread(f, buf + len, (unsigned)(cap - len - 1))) > 0) {
        len += n;
        if (cap - len < (1 << 20)) buf = realloc(buf, cap *= 2);
    }
    gzclose(f);
    buf[len] = 0;
    return buf;
}

int main(int argc, char **argv)
{
    int verbose = 0, files = 0, input_errors = 0;
    stats_t total = {0};
    static test_t t;

    mem = calloc(MEM_SIZE + 16, 1);
    C68k_Init(&C68K, int_ack);
    C68k_Set_ReadB(&C68K, read_byte);
    C68k_Set_ReadW(&C68K, read_word);
    C68k_Set_WriteB(&C68K, write_byte);
    C68k_Set_WriteW(&C68K, write_word);
    C68k_Set_Fetch(&C68K, 0x000000, 0xffffff, (pointer)mem);

    printf("%-14s %6s %6s %6s %9s %9s %8s  %s\n", "file", "tests", "pass", "fail", "addr-err", "trace", "cyc-off", "fields that differ (plain fails)");
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) { verbose = atoi(argv[++i]); continue; }
        char *text = slurp_gz(argv[i]);
        if (!text) { input_errors++; continue; }
        stats_t st = {0};
        int shown = 0;
        p = text;
        expect('[');
        while (parse_test(&t)) {
            u32 got[19];
            load(&t.in);
            s32 cycles = C68k_Exec(&C68K, 1);
            u32 bad = compare(&t.out, got);
            int trace = (t.in.sr & 0x8000) != 0;
            if (cycles != (s32)t.length) st.cycles_off++;
            if (t.address_error) { st.ae_total++; if (bad) st.ae_fail++; continue; }
            if (trace) { st.trace_total++; if (bad) st.trace_fail++; continue; }
            if (!bad) { st.pass++; continue; }
            st.fail++;
            for (int f = 0; f < 20; f++) if (bad & (1u << f)) st.field[f]++;
            if (shown < verbose) { report(&t, bad, got, cycles); shown++; }
        }
        ws();
        if (*p != ']') { fprintf(stderr, "%s: truncated or malformed near \"%.40s\"\n", argv[i], p); input_errors++; }
        free(text);

        const char *base = strrchr(argv[i], '/');
        char name[64];
        snprintf(name, sizeof(name), "%s", base ? base + 1 : argv[i]);
        char *dot = strstr(name, ".json"); if (dot) *dot = 0;
        printf("%-14s %6d %6d %6d %4d/%-4d %4d/%-4d %8d ", name,
               st.pass + st.fail + st.ae_total + st.trace_total, st.pass, st.fail,
               st.ae_fail, st.ae_total, st.trace_fail, st.trace_total, st.cycles_off);
        for (int f = 0; f < 20; f++) if (st.field[f]) printf(" %s:%d", field_names[f], st.field[f]);
        printf("\n");
        fflush(stdout);

        total.pass += st.pass; total.fail += st.fail;
        total.ae_total += st.ae_total; total.ae_fail += st.ae_fail;
        total.trace_total += st.trace_total; total.trace_fail += st.trace_fail;
        total.cycles_off += st.cycles_off;
        files++;
    }
    printf("TOTAL %d files: pass %d, fail %d (address-error vectors failing %d/%d, trace vectors failing %d/%d), cycle count differs in %d\n",
           files, total.pass, total.fail, total.ae_fail, total.ae_total, total.trace_fail, total.trace_total, total.cycles_off);
    if (input_errors) printf("%d input file(s) could not be read completely\n", input_errors);
    return (total.fail || input_errors || files == 0) ? 1 : 0;
}
