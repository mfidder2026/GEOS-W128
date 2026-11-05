/* GEOS-W128 BBS client: XMODEM download.

   In the terminal: F7, then X. First the file name on the status row, then
   the C128 receives what the BBS sends with XMODEM and writes it as a PRG to
   the current disk.
     - XMODEM-CRC ('C'); after 3 times without an answer the plain checksum
     - blocks of 128 (SOH) and 1024 bytes (STX, XMODEM-1K)
     - stop and wait: a block is on disk before its ACK goes out
     - the last block is written at EOT, without the $1A padding
     - RUN/STOP cancels (CAN CAN CAN)
   The file goes through GEOS's own disk routines, a block at a time, so it
   may be larger than the memory. A cancelled download stays on disk with what
   came in so far. */

#include <geos.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "bbs.h"

#define dirHead ((unsigned char *)0x8200)

#define XM_SOH  0x01
#define XM_STX  0x02
#define XM_EOT  0x04
#define XM_ACK  0x06
#define XM_NAK  0x15
#define XM_CAN  0x18
#define XM_C    0x43

unsigned char xmOn;

static unsigned char xmCrc, xmExp, xmSt, xmPkt, xmTries, xmT0;
static unsigned xmNeed, xmGot, xmHeld, xmBlocks;
static char xmBuf[3 + 1024 + 2];
static char xmHold[1024];
static char msg[42];

/* the file being written */
static char fName[16];
static char sector[256];
static unsigned char secFill;
static struct tr_se firstTs, curTs, dirTs;
static unsigned char dirOff, fileOpen;
static unsigned fileBlocks;

static void timer(void)
{
    xmT0 = frames;
}

static void ask(void)
{
    tm_txput(xmCrc ? XM_C : XM_NAK);
    timer();
}

static void show(void)
{
    sprintf(msg, "XMODEM (RUN/STOP=CANCEL): %u BLOCKS", xmBlocks);
    tm_status(msg);
}

/* the directory: is the name free, and where does it go? 0 = it can't */
static unsigned char find_slot(void)
{
    static char dir[256];
    struct tr_se ts;
    unsigned char i, have = 0;

    ts.track = dirHead[0];
    ts.sector = dirHead[1];
    while (ts.track) {
        if (GetBlock(&ts, dir))
            return 0;
        for (i = 0; i < 8; ++i) {
            char *e = dir + 2 + i * 32;

            if (!e[0]) {
                if (!have) {
                    have = 1;
                    dirTs = ts;
                    dirOff = 2 + i * 32;
                }
            } else if (!memcmp(e + 3, fName, 16)) {
                strcpy(msg, "THAT FILE ALREADY EXISTS");
                return 0;
            }
        }
        ts.track = dir[0];
        ts.sector = dir[1];
    }
    if (!have)
        strcpy(msg, "THE DIRECTORY IS FULL");
    return have;
}

/* a new block on the disk; 0 = disk full */
static unsigned char next_block(struct tr_se *out)
{
    struct tr_se from;

    from = curTs;
    *out = SetNextFree(&from);
    return __oserror == 0 && out->track != 0;
}

static unsigned char file_open(void)
{
    msg[0] = 0;
    if (OpenDisk()) {
        strcpy(msg, "DISK ERROR");
        return 0;
    }
    if (!find_slot()) {
        if (!msg[0])
            strcpy(msg, "DISK ERROR");
        return 0;
    }
    curTs.track = 1;
    curTs.sector = 0;
    if (!next_block(&firstTs)) {
        strcpy(msg, "THE DISK IS FULL");
        return 0;
    }
    curTs = firstTs;
    secFill = 2;
    fileBlocks = 1;
    fileOpen = 1;
    return 1;
}

static unsigned char file_put(const char *p, unsigned n)
{
    struct tr_se nx;

    while (n) {
        if (secFill == 0) {     /* full (wrapped from 255): on to the next */
            if (!next_block(&nx))
                return 0;
            sector[0] = nx.track;
            sector[1] = nx.sector;
            if (PutBlock(&curTs, sector))
                return 0;
            curTs = nx;
            secFill = 2;
            ++fileBlocks;
        }
        sector[secFill++] = *p++;
        --n;
    }
    return 1;
}

/* last block, directory entry, BAM */
static void file_close(void)
{
    static char dir[256];
    char *e;

    if (!fileOpen)
        return;
    fileOpen = 0;
    sector[0] = 0;
    sector[1] = secFill ? secFill - 1 : 255;
    PutBlock(&curTs, sector);
    if (!GetBlock(&dirTs, dir)) {
        e = dir + dirOff;
        memset(e, 0, 30);
        e[0] = 0x82;            /* PRG, closed */
        e[1] = firstTs.track;
        e[2] = firstTs.sector;
        memcpy(e + 3, fName, 16);
        e[28] = fileBlocks;
        e[29] = fileBlocks >> 8;
        PutBlock(&dirTs, dir);
    }
    PutDirHead();
}

void xm_close(void)
{
    file_close();
}

/* the name, on the status row; 0 = nothing */
static unsigned char ask_name(void)
{
    static char name[17];
    unsigned char n = 0, c, i;

    for (;;) {
        name[n] = 0;
        sprintf(msg, "SAVE AS: %s", name);
        tm_status(msg);
        do {
#ifdef BBS_TEST
            extern unsigned char __fastcall__ test_script(void);
            if ((c = test_script()) == 0)
#endif
            c = term_key();
        } while (!c);
        if (c == 0x0d)
            break;
        if (c == 0x03) {
            n = 0;
            break;
        }
        if (c == 0x14) {
            if (n)
                --n;
        } else if (c >= 0x20 && c < 0x60 && c != ',' && c != ':' && c != '*' &&
                   c != '?' && c != '=' && c != '"' && n < 16) {
            name[n++] = c;
        }
    }
    if (!n)
        return 0;
    for (i = 0; i < 16; ++i)
        fName[i] = i < n ? name[i] : 0xa0;
    return 1;
}

void xm_start(void)
{
    msg[0] = 0;
    xmOn = 0;
    if (!ask_name()) {
        tm_status("");
        return;
    }
    if (!file_open()) {
        tm_status(msg);
        return;
    }
    xmOn = 1;
    xmCrc = 1;
    xmExp = 1;
    xmSt = 0;
    xmPkt = 0;
    xmHeld = 0;
    xmBlocks = 0;
    xmTries = 0;
    ask();
    show();
}

/* a received data byte */
void xm_byte(unsigned char c)
{
    if (xmPkt)                  /* the last packet is not done yet */
        return;
    xmT0 = frames;
    if (!xmSt) {
        if (c == XM_EOT || c == XM_CAN) {
            xmBuf[0] = c;
            xmPkt = 1;
            return;
        }
        if (c == XM_SOH)
            xmNeed = 3 + 128;
        else if (c == XM_STX)
            xmNeed = 3 + 1024;
        else
            return;             /* noise between the blocks */
        xmNeed += xmCrc + 1;
        xmGot = 0;
        xmSt = 1;
    }
    xmBuf[xmGot++] = c;
    if (xmGot == xmNeed) {
        xmSt = 0;
        xmPkt = 1;
    }
}

static unsigned __fastcall__ pkt_size(void)
{
    return xmBuf[0] == XM_STX ? 1024 : 128;
}

static unsigned char check_ok(void)
{
    unsigned n = pkt_size(), i, crc;
    unsigned char sum = 0;

    if (xmCrc) {
        crc = crc16(xmBuf + 3, n);
        return (unsigned char)xmBuf[3 + n] == (unsigned char)(crc >> 8) &&
               (unsigned char)xmBuf[4 + n] == (unsigned char)crc;
    }
    for (i = 0; i < n; ++i)
        sum += xmBuf[3 + i];
    return (unsigned char)xmBuf[3 + n] == sum;
}

static void finish(const char *text)
{
    xmOn = 0;
    file_close();
    sprintf(msg, "%s %u BLOCKS", text, xmBlocks);
    tm_status(msg);
}

static void __fastcall__ cancel(const char *text)
{
    tm_txput(XM_CAN);
    tm_txput(XM_CAN);
    tm_txput(XM_CAN);
    finish(text);
}

void xm_stopkey(void)
{
    cancel("DOWNLOAD CANCELLED,");
}

static unsigned char write_held(void)
{
    unsigned char ok = file_put(xmHold, xmHeld);

    xmHeld = 0;
    return ok;
}

static void eot(void)
{
    tm_txput(XM_ACK);
    while (xmHeld && xmHold[xmHeld - 1] == 0x1a)
        --xmHeld;
    write_held();
    finish("DOWNLOAD COMPLETE,");
}

/* from the session loop: a packet to handle, or time-outs */
void xm_tick(void)
{
    unsigned char a, wait;

    if (!xmPkt) {
        wait = frames - xmT0;
        if (xmSt) {
            if (wait < 50)      /* in the middle of a block: 1 s */
                return;
            xmSt = 0;
            tm_txput(XM_NAK);
            timer();
            return;
        }
        if (wait < 150)         /* waiting for a block: 3 s */
            return;
        if (++xmTries >= 10) {
            cancel("NO XMODEM FROM THE BBS,");
            return;
        }
        if (xmBlocks) {
            tm_txput(XM_NAK);
            timer();
            return;
        }
        if (xmTries == 3)
            xmCrc = 0;
        ask();
        return;
    }
    xmPkt = 0;
    timer();
    a = xmBuf[0];
    if (a == XM_EOT) {
        eot();
        return;
    }
    if (a == XM_CAN) {
        finish("CANCELLED BY THE BBS,");
        return;
    }
    if (((unsigned char)xmBuf[1] ^ (unsigned char)xmBuf[2]) != 0xff || !check_ok()) {
        tm_txput(XM_NAK);
        return;
    }
    a = xmBuf[1];
    if (a == xmExp) {
        if (!write_held()) {
            cancel("DISK ERROR,");
            return;
        }
        xmHeld = pkt_size();
        memcpy(xmHold, xmBuf + 3, xmHeld);
        ++xmExp;
        ++xmBlocks;
        xmTries = 0;
        show();
        tm_txput(XM_ACK);
    } else if ((unsigned char)(a + 1) == xmExp) {
        tm_txput(XM_ACK);       /* the last one again: only confirm */
    } else {
        tm_txput(XM_NAK);
    }
}
