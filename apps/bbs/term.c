/* GEOS-W128 BBS client: the session.

   CONNECT goes to a full screen terminal in VDC text mode: rows 0-23 text,
   row 24 status. PETSCII BBSes get 40 columns in colour, as they expect;
   ASCII BBSes get the full 80. In 40 column GEOS it is the VIC in text mode,
   40 columns for both. Received bytes go through the Telnet parser to
   the screen; keys go to the BBS as PETSCII, F7 opens the session menu. */

#include <geos.h>
#include <string.h>
#include <stdio.h>
#include "../common/wic64.h"
#include "bbs.h"

#define TM_ROWS   24
#define TM_STAT   24
#define TM_TXMAX  64
#define ATTR      0x0800u
#define RXSIZE    8192          /* the most the firmware sends at once */

#define KM_SHIFT  1
#define KM_CBM    2
#define KM_CTRL   4

#define TN_IAC    0xff
#define TN_DONT   0xfe
#define TN_DO     0xfd
#define TN_WONT   0xfc
#define TN_WILL   0xfb
#define TN_SB     0xfa
#define TN_SE     0xf0
#define TO_BINARY 0
#define TO_ECHO   1
#define TO_SGA    3
#define TO_TTYPE  24
#define TO_NAWS   31
#define TN_OPTS   32
#define TN_SBMAX  8

enum { TS_DATA, TS_IAC, TS_OPT, TS_SB, TS_SBIAC };

/* C64 colour number -> VDC RGBI */
static const unsigned char rgbi[16] = {
    0, 15, 8, 7, 11, 4, 2, 13, 10, 12, 9, 1, 6, 5, 3, 14
};
static const unsigned char pcCode[16] = {
    0x90, 0x05, 0x1c, 0x9f, 0x9c, 0x1e, 0x1f, 0x9e,
    0x81, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b
};

/* keyboard matrix -> PETSCII: plain, SHIFT, C=, CTRL (0 = nothing) */
static const unsigned char keyPlain[64] = {
    0x14, 0x0d, 0x1d, 0x88, 0x85, 0x86, 0x87, 0x11,
    '3', 'W', 'A', '4', 'Z', 'S', 'E', 0,
    '5', 'R', 'D', '6', 'C', 'F', 'T', 'X',
    '7', 'Y', 'G', '8', 'B', 'H', 'U', 'V',
    '9', 'I', 'J', '0', 'M', 'K', 'O', 'N',
    '+', 'P', 'L', '-', '.', ':', '@', ',',
    0x5c, '*', ';', 0x13, 0, '=', 0x5e, '/',
    '1', 0x5f, 0, '2', ' ', 0, 'Q', 0x03
};
static const unsigned char keyShift[64] = {
    0x94, 0x8d, 0x9d, 0x8c, 0x89, 0x8a, 0x8b, 0x91,
    '#', 0xd7, 0xc1, '$', 0xda, 0xd3, 0xc5, 0,
    '%', 0xd2, 0xc4, '&', 0xc3, 0xc6, 0xd4, 0xd8,
    0x27, 0xd9, 0xc7, '(', 0xc2, 0xc8, 0xd5, 0xd6,
    ')', 0xc9, 0xca, '0', 0xcd, 0xcb, 0xcf, 0xce,
    0xdb, 0xd0, 0xcc, 0xdd, '>', '[', 0xba, '<',
    0xa9, 0xc0, ']', 0x93, 0, '=', 0xde, '?',
    '!', 0x5f, 0, '"', 0xa0, 0, 0xd1, 0x03
};
static const unsigned char keyCbm[64] = {
    0x94, 0x8d, 0x9d, 0x8c, 0x89, 0x8a, 0x8b, 0x91,
    0x96, 0xb3, 0xb0, 0x97, 0xad, 0xae, 0xb1, 0,
    0x98, 0xb2, 0xac, 0x99, 0xbc, 0xbb, 0xa3, 0xbd,
    0x9a, 0xb7, 0xa5, 0x9b, 0xbf, 0xb4, 0xb8, 0xbe,
    ')', 0xa2, 0xb5, '0', 0xa7, 0xa1, 0xb9, 0xaa,
    0xa6, 0xaf, 0xb6, 0xdc, '>', '[', 0xa4, '<',
    0xa8, 0xdf, ']', 0x93, 0, '=', 0xde, '?',
    0x81, 0x5f, 0, 0x95, 0xa0, 0, 0xab, 0x03
};
static const unsigned char keyCtrl[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0x1c, 0x17, 0x01, 0x9f, 0x1a, 0x13, 0x05, 0,
    0x9c, 0x12, 0x04, 0x1e, 0x03, 0x06, 0x14, 0x18,
    0x1f, 0x19, 0x07, 0x9e, 0x02, 0x08, 0x15, 0x16,
    0x12, 0x09, 0x0a, 0x92, 0x0d, 0x0b, 0x0f, 0x0e,
    0, 0x10, 0x0c, 0, 0, 0x1b, 0, 0,
    0x1c, 0, 0x1d, 0, 0, 0x1f, 0x1e, 0,
    0x90, 0x06, 0, 0x05, 0, 0, 0x11, 0
};
/* the C128 extra keys: keypad, ESC, TAB, LINE FEED, cursor row */
static const unsigned char keyExt[24] = {
    0, '8', '5', 0x09, '2', '4', '7', '1',
    0x1b, '+', '-', 0x0a, 0x0d, '6', '9', '3',
    0, '0', '.', 0x91, 0x11, 0x9d, 0x1d, 0
};

unsigned char tmCols;
unsigned char tnSeen;

static unsigned rowTab[25];
static unsigned char tmX, tmY, tmFg, tmRev, tmLower, tmAscii, tmEsc;
static unsigned char tmTxLen, tmMenu, tmLastKey, tmNextRep;
static unsigned char tmTx[TM_TXMAX];
static unsigned char keyQ[8], keyHead, keyTail;
static unsigned long tmRxN, tmTxN;
static unsigned tmSecs;
static unsigned char tmFr, tmFrLast, tmHold;
static unsigned char tnState, tnVerb, tnOpt, tnSbLen;
static unsigned char tnSb[TN_SBMAX], tnHim[TN_OPTS], tnUs[TN_OPTS];
/* $6000-$7FFF: free as long as nothing draws to the background buffer */
#define rx ((char *)0x6000)
static char line[90];
static struct { void_func top; } saveVec;

/* ------------------------------------------------------ the screen ---- */

static unsigned char __fastcall__ attr_now(void)
{
    return tmFg | (tmLower ? 0x80 : 0);
}

static void clr_row(unsigned char y)
{
    vdc_fill(rowTab[y], 0x20, tmCols);
    vdc_fill(rowTab[y] + ATTR, attr_now(), tmCols);
}

static void scroll(void)
{
    unsigned n = rowTab[TM_ROWS - 1];

    vdc_copy(0, tmCols, n);
    vdc_copy(ATTR, ATTR + tmCols, n);
    clr_row(TM_ROWS - 1);
}

static void cls(void)
{
    unsigned n = rowTab[TM_ROWS];

    vdc_fill(0, 0x20, n);
    vdc_fill(ATTR, attr_now(), n);
    tmX = tmY = 0;
}

static void down(void)
{
    if (++tmY >= TM_ROWS) {
        tmY = TM_ROWS - 1;
        scroll();
    }
}

static void newline(void)
{
    tmX = 0;
    down();
}

static void advance(void)
{
    if (++tmX >= tmCols)
        newline();
}

static void left(void)
{
    if (tmX)
        --tmX;
    else if (tmY) {
        --tmY;
        tmX = tmCols - 1;
    }
}

static void __fastcall__ put(unsigned char c)
{
    vdc_putc(rowTab[tmY] + tmX, c, attr_now());
    advance();
}

static void show_cursor(void)
{
    vdc_cursor(rowTab[tmY] + tmX);
}

/* a local ASCII string as capitals, whichever font is on */
static unsigned char __fastcall__ to_screen(unsigned char c)
{
    if (c >= 'a' && c <= 'z')
        c -= 0x20;
    if (c >= 'A' && c <= 'Z')
        c = tmLower ? c : c - 0x40;
    return c;
}

static void __fastcall__ local(const char *s)
{
    while (*s)
        put(to_screen(*s++));
}

static void __fastcall__ local_line(const char *s)
{
    local(s);
    newline();
}

/* ------------------------------------------------------ status row ---- */

static void __fastcall__ stat_at(unsigned char col, const char *s)
{
    unsigned a = rowTab[TM_STAT];
    unsigned char at = rgbi[14] | (tmLower ? 0x80 : 0);

    while (*s && col < tmCols) {
        vdc_putc(a + col, to_screen(*s++) | 0x80, at);
        ++col;
    }
}

void tm_status(const char *text)
{
    tmHold = 6;                 /* seconds before the counters come back */
    vdc_fill(rowTab[TM_STAT], 0xa0, tmCols);
    vdc_fill(rowTab[TM_STAT] + ATTR, rgbi[14] | (tmLower ? 0x80 : 0), tmCols);
    stat_at(1, text);
}

static void stat_info(void)
{
    unsigned rk = tmRxN >> 10, tk = tmTxN >> 10;

    if (rk > 999)
        rk = 999;
    if (tk > 999)
        tk = 999;
    sprintf(line, " RX %uK TX %uK %02u:%02u      ", rk, tk,
            (tmSecs / 60) > 99 ? 99 : tmSecs / 60, tmSecs % 60);
    line[20] = 0;
    stat_at(tmCols - 27, line);
    stat_at(tmCols - 7, "F7=MENU");
}

static void stat_online(void)
{
    tm_status(bb_name(cfgDefault));
    tmHold = 0;
    stat_info();
}

/* ------------------------------------------------------ the output ---- */

static void __fastcall__ set_case(unsigned char lower)
{
    if (tmLower != lower) {
        tmLower = lower;
        vdc_case(lower, rowTab[TM_STAT] + tmCols);
    }
}

static void __fastcall__ show_pet(unsigned char c)
{
    unsigned char i;
    unsigned a;

    if (c >= 0x20 && c < 0x80) {
        if (c >= 0x60)
            c -= 0x20;
        else if (c >= 0x40)
            c &= 0x3f;
    } else if (c >= 0xa0) {
        if (c == 0xff)
            c = 0x5e;
        else if (c < 0xc0)
            c -= 0x40;
        else
            c &= 0x7f;
    } else {
        for (i = 0; i < 16; ++i)
            if (c == pcCode[i]) {
                tmFg = rgbi[i];
                return;
            }
        switch (c) {
        case 0x0d:
        case 0x8d:
            tmRev = 0;
            newline();
            break;
        case 0x93:
            cls();
            break;
        case 0x13:
            tmX = tmY = 0;
            break;
        case 0x14:
            left();
            put(0x20);
            left();
            break;
        case 0x94:              /* INST: rest of the line to the right */
            a = rowTab[tmY];
            for (i = tmCols - 2; i >= tmX && i < tmCols; --i) {
                vdc_copy(a + i + 1, a + i, 1);
                vdc_copy(a + ATTR + i + 1, a + ATTR + i, 1);
                if (i == 0)
                    break;
            }
            vdc_putc(a + tmX, 0x20, attr_now());
            break;
        case 0x11:
            down();
            break;
        case 0x91:
            if (tmY)
                --tmY;
            break;
        case 0x1d:
            advance();
            break;
        case 0x9d:
            left();
            break;
        case 0x12:
            tmRev = 0x80;
            break;
        case 0x92:
            tmRev = 0;
            break;
        case 0x0e:
            set_case(1);
            break;
        case 0x8e:
            set_case(0);
            break;
        }
        return;
    }
    put(c | tmRev);
}

/* ASCII in the lower case font; ANSI sequences are skipped, only ESC[..J
   (clear) and ESC[..H (home) do something */
static void __fastcall__ show_asc(unsigned char c)
{
    if (tmEsc == 1) {
        tmEsc = c == 0x5b ? 2 : 0;
        return;
    }
    if (tmEsc == 2) {
        if (c >= 0x40) {
            tmEsc = 0;
            if (c == 'J')
                cls();
            else if (c == 'H')
                tmX = tmY = 0;
        }
        return;
    }
    if (c >= 0x20) {
        if (c >= 0x7f)
            return;
        if (c == 0x40)
            c = 0;
        else if (c >= 0x5b && c < 0x60)
            c &= 0x1f;
        else if (c >= 0x60 && c < 0x7b)
            c &= 0x1f;
        else if (c >= 0x7b)
            c -= 0x20;
        put(c);
        return;
    }
    switch (c) {
    case 0x1b:
        tmEsc = 1;
        break;
    case 0x0d:
        tmX = 0;
        break;
    case 0x0a:
        down();
        break;
    case 0x08:
        left();
        break;
    case 0x0c:
        cls();
        break;
    case 0x09:
        tmX |= 7;
        advance();
        break;
    }
}

static void __fastcall__ tm_show(unsigned char c)
{
    if (tmAscii)
        show_asc(c);
    else
        show_pet(c);
}

/* ------------------------------------------------------ the sending -- */

void tm_txput(unsigned char c)
{
    if (tmTxLen < TM_TXMAX)
        tmTx[tmTxLen++] = c;
}

static unsigned char flush(void)
{
    static char sent[TM_TXMAX];
    unsigned char n = tmTxLen, st;

    memcpy(sent, tmTx, n);
    tmTxLen = 0;
    tmTxN += n;
    st = wic_call(WIC_TCP_WRITE, sent, n, line, 2);
    return st == WIC_OK;
}

/* ------------------------------------------------------ Telnet -------- */

static void __fastcall__ tn_reply(unsigned char verb)
{
    tm_txput(TN_IAC);
    tm_txput(verb);
    tm_txput(tnOpt);
}

static void tn_opt(void)
{
    unsigned char o = tnOpt;

    switch (tnVerb) {
    case TN_WILL:               /* what the server may do */
        if (o != TO_BINARY && o != TO_ECHO && o != TO_SGA) {
            tn_reply(TN_DONT);
        } else if (!tnHim[o]) {
            tnHim[o] = 1;
            tn_reply(TN_DO);
        }
        break;
    case TN_WONT:
        if (o < TN_OPTS && tnHim[o]) {
            tnHim[o] = 0;
            tn_reply(TN_DONT);
        }
        break;
    case TN_DO:                 /* what we do */
        if (o != TO_BINARY && o != TO_SGA && o != TO_TTYPE && o != TO_NAWS) {
            tn_reply(TN_WONT);
        } else if (!tnUs[o]) {
            tnUs[o] = 1;
            tn_reply(TN_WILL);
            if (o == TO_NAWS) {
                tm_txput(TN_IAC);
                tm_txput(TN_SB);
                tm_txput(TO_NAWS);
                tm_txput(0);
                tm_txput(tmCols);
                tm_txput(0);
                tm_txput(TM_ROWS);
                tm_txput(TN_IAC);
                tm_txput(TN_SE);
            }
        }
        break;
    default:                    /* DONT */
        if (o < TN_OPTS && tnUs[o]) {
            tnUs[o] = 0;
            tn_reply(TN_WONT);
        }
        break;
    }
}

static void tn_sbdone(void)
{
    const char *s;

    if (tnSbLen < 2 || tnSb[0] != TO_TTYPE || tnSb[1] != 1)
        return;
    tm_txput(TN_IAC);
    tm_txput(TN_SB);
    tm_txput(TO_TTYPE);
    tm_txput(0);
    /* always in ASCII, whatever the screen shows */
    for (s = tmAscii ? "\x44\x55\x4d\x42" : "\x50\x45\x54\x53\x43\x49\x49"; *s; ++s)
        tm_txput(*s);
    tm_txput(TN_IAC);
    tm_txput(TN_SE);
}

/* 1 = c is data for the screen, 0 = it belonged to a Telnet command */
static unsigned char __fastcall__ tn_byte(unsigned char c)
{
    switch (tnState) {
    case TS_DATA:
        if (c != TN_IAC)
            return 1;
        tnSeen = 1;             /* the BBS speaks Telnet (for XMODEM) */
        tnState = TS_IAC;
        return 0;
    case TS_IAC:
        tnState = TS_DATA;
        if (c == TN_IAC)
            return 1;
        if (c == TN_SB) {
            tnState = TS_SB;
            tnSbLen = 0;
        } else if (c >= TN_WILL) {
            tnVerb = c;
            tnState = TS_OPT;
        }
        return 0;
    case TS_OPT:
        tnOpt = c;
        tnState = TS_DATA;
        tn_opt();
        return 0;
    case TS_SB:
        if (c == TN_IAC) {
            tnState = TS_SBIAC;
            return 0;
        }
        break;
    default:                    /* TS_SBIAC */
        if (c != TN_IAC) {
            tnState = TS_DATA;
            if (c == TN_SE)
                tn_sbdone();
            return 0;
        }
        tnState = TS_SB;
        break;
    }
    if (tnSbLen < TN_SBMAX)
        tnSb[tnSbLen++] = c;
    return 0;
}

/* ------------------------------------------------------ the keyboard -- */

static unsigned char __fastcall__ decode(unsigned char k)
{
    if (k >= 64)
        return keyExt[k - 64];
    if (kbdMods & KM_CTRL)
        return keyCtrl[k];
    if (kbdMods & KM_CBM)
        return keyCbm[k];
    if (kbdMods & KM_SHIFT)
        return keyShift[k];
    return keyPlain[k];
}

/* the keys that repeat, as on the C64: DEL, space, cursor */
static unsigned char __fastcall__ repeats(unsigned char c)
{
    return c == 0x14 || c == 0x94 || c == 0x20 || c == 0x1d || c == 0x9d ||
           c == 0x11 || c == 0x91;
}

/* look at the keyboard; a new key goes into the queue */
static void kbd_poll(void)
{
    unsigned char k = kbd_scan(), c, next;

    if (k == 0xff) {
        tmLastKey = 0xff;
        return;
    }
    if (k == tmLastKey) {
        if ((unsigned char)(frames - tmNextRep) >= 128)
            return;
        tmNextRep = frames + 3;
        c = decode(k);
        if (!repeats(c))
            return;
    } else {
        tmLastKey = k;
        tmNextRep = frames + 25;
        c = decode(k);
    }
    if (!c)
        return;
    next = (keyHead + 1) & 7;
    if (next != keyTail) {
        keyQ[keyHead] = c;
        keyHead = next;
    }
}

unsigned char term_key(void)
{
    unsigned char c;

    kbd_poll();
    if (keyHead == keyTail)
        return 0;
    c = keyQ[keyTail];
    keyTail = (keyTail + 1) & 7;
    return c;
}

/* a key to the BBS */
static void __fastcall__ send_key(unsigned char c)
{
    unsigned char mods = kbdMods;

    if (tmAscii) {
        switch (c) {
        case 0x14:
        case 0x94:
            c = 0x08;
            break;
        case 0x8d:
            c = 0x0d;
            break;
        case 0x93:
            c = 0x0c;
            break;
        case 0x1d:
        case 0x9d:
        case 0x11:
        case 0x91:
        case 0x13:              /* ANSI: ESC [ x, no echo */
            tm_txput(0x1b);
            tm_txput(0x5b);
            tm_txput(c == 0x1d ? 'C' - 0 : c == 0x9d ? 'D' - 0 :
                     c == 0x11 ? 'B' - 0 : c == 0x91 ? 'A' - 0 : 'H' - 0);
            return;
        case 0x5f:
            c = '_';
            break;
        case 0xde:
            c = '~';
            break;
        case 0xa9:
            c = '|';
            break;
        case 0xba:
            c = '@';
            break;
        case 0xc0:
            c = '`';
            break;
        default:
            if (mods & KM_CTRL) {
                if (c >= 0x20)
                    return;
            } else if (c >= 0x41 && c <= 0x5a) {
                c += 0x20;      /* PETSCII letter -> a-z */
            } else if (c >= 0xc1 && c <= 0xda) {
                c -= 0x80;      /* SHIFT letter -> A-Z */
            } else if (c >= 0x80) {
                return;         /* F keys, colours, graphics */
            }
            break;
        }
    }
    tm_txput(c);
    if (cfgEcho && !tnHim[TO_ECHO])
        tm_show(c);
}

/* ------------------------------------------------------ the session -- */

static void reset(void)
{
    unsigned char i;
    unsigned a = 0;

    tmAscii = bb_mode(cfgDefault) == BM_ASCII;
    tmCols = tmAscii && (graphMode & 0x80) ? 80 : 40;
    tmLower = 1;                /* start in upper/lower case */
    for (i = 0; i < 25; ++i) {
        rowTab[i] = a;
        a += tmCols;
    }
    tmFg = rgbi[15];
    tmX = tmY = tmRev = tmEsc = tmTxLen = tmMenu = 0;
    tnState = TS_DATA;
    tnSeen = 0;
    memset(tnHim, 0, TN_OPTS);
    memset(tnUs, 0, TN_OPTS);
    tmRxN = tmTxN = 0;
    tmSecs = 0;
    tmFr = 0;
    tmFrLast = frames;
    tmLastKey = 0xff;
    keyHead = keyTail = 0;
    xmOn = 0;
}

static void enter(void)
{
    MouseOff();
    saveVec.top = intTopVector;
    intTopVector = frame_tick;
    vdc_enter(tmCols);
    vdc_font();
    if (!(graphMode & 0x80))
        vdc_case(tmLower, 0);   /* the VIC: one font for the whole screen */
    vdc_fill(rowTab[TM_STAT], 0xa0, tmCols);
    cls();
    tm_status("");
    show_cursor();
}

static void leave(void)
{
    vdc_leave();
    intTopVector = saveVec.top;
    MouseUp();
}

static void close_tcp(void)
{
    if (xmOn) {
        xmOn = 0;
        xm_close();
    }
    wic_call(WIC_TCP_CLOSE, "", 0, line, 2);
}

static void __fastcall__ wait_return(void)
{
    unsigned char c;

    do {
        c = term_key();
    } while (c != 0x0d && c != 0x03);
}

/* 1 = go on, 0 = the connection is gone */
static unsigned char poll_net(void)
{
    unsigned n, i;
    unsigned char st, c;

    R0 = (unsigned)"";
    R1 = 0;
    R2 = (unsigned)rx;
    R3 = RXSIZE;
    st = wic_exec(WIC_TCP_READ);
    n = R4;
    if (st != WIC_OK)
        return 0;
    for (i = 0; i < n; ++i) {
        c = rx[i];
        if ((i & 0x7f) == 0x7f)
            kbd_poll();         /* no key gets lost in a long burst */
        if (xmOn) {
            if (!tnSeen || tn_byte(c))
                xm_byte(c);
        } else if (tn_byte(c)) {
            tm_show(c);
        }
    }
    if (n) {
        tmRxN += n;
        show_cursor();
    }
    return 1;
}

unsigned char term_run(void)
{
    static char target[BK_HOSTMAX + 8];
    unsigned char c, st, quit = 0;

    reset();
    enter();
    sprintf(target, "%s:%u", bb_host(cfgDefault), bb_port(cfgDefault));
    local_line("CONNECTING TO");
    newline();
    local_line(bb_name(cfgDefault));
    local_line(target);
    newline();
    local_line("CONNECTING...");
    show_cursor();

    st = wic_call(WIC_TCP_OPEN, target, strlen(target), line, 2);
    if (st != WIC_OK) {
        bbMessage = st == WIC_NO_DEVICE ? "No WiC64 on the userport" :
                                          "No connection to the BBS";
        leave();
        return 0;
    }
    local_line("CONNECTED");
    newline();
    stat_online();
    show_cursor();

    for (;;) {
#ifdef BBS_TEST
        extern unsigned char __fastcall__ test_script(void);
        if ((c = test_script()) == 0)
#endif
        c = term_key();
        if (c) {
            if (xmOn) {
                if (c == 0x03)
                    xm_stopkey();
            } else if (tmMenu) {
                tmMenu = 0;
                if (c == 'X') {
                    xm_start();
                } else if (c == 'D') {
                    close_tcp();
                    bbMessage = "Disconnected";
                    break;
                } else if (c == 'Q') {
                    close_tcp();
                    quit = 1;
                    break;
                } else {
                    stat_online();
                }
            } else if (c == 0x88) {
                tmMenu = 1;
                tm_status("D=HANG UP  X=XMODEM DOWNLOAD  Q=DESKTOP");
            } else {
                send_key(c);
                show_cursor();
            }
        }
        if (frames != tmFrLast) {
            tmFr += (unsigned char)(frames - tmFrLast);
            tmFrLast = frames;
            if (tmFr >= 50) {
                tmFr -= 50;
                ++tmSecs;
                if (!tmMenu && !xmOn) {
                    if (!tmHold)
                        stat_info();
                    else if (!--tmHold)
                        stat_online();
                }
            }
        }
        if (xmOn)
            xm_tick();
        if (tmTxLen && !flush()) {
            close_tcp();
            bbMessage = "Connection lost";
            tm_status("CONNECTION CLOSED - PRESS RETURN");
            wait_return();
            break;
        }
        if (!poll_net()) {
            close_tcp();
            bbMessage = "Closed by the remote host";
            tm_status("CONNECTION CLOSED - PRESS RETURN");
            wait_return();
            break;
        }
    }
    leave();
    return quit;
}
