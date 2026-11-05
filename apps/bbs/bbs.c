/* GEOS-W128 BBS client for the WiC64, in 80 columns on GEOS 128.

   The main screen shows the default BBS, with buttons for CONNECT, DISCONNECT,
   ADDRESS BOOK, LOCAL ECHO and BACK TO DESKTOP, as on Commodore Desk 64. The
   address book has ten built in BBSes and six of your own; a click makes one
   the default. BBS.CFG and BBS.BOOK are GEOS data files on the disk. */

#include <geos.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/wic64.h"
#include "bbs.h"

#define BBS_DEFAULT 2           /* The Oasis BBS */
#define mouseXPos (*(unsigned *)0x3a)
#define mouseYPos (*(unsigned char *)0x3c)
#define BOOK_VERSION 1
#define CFG_VERSION 1

struct rec {
    char name[BK_NAMEMAX + 1];
    char host[BK_HOSTMAX + 1];
    unsigned port;
    unsigned char mode;
};
struct book {
    char magic[2];
    unsigned char version, count;
    struct rec r[BBS_CUSTOM];
};
struct cfg {
    char magic[2];
    unsigned char version, deflt, echo, term, reserved, sum;
};

static const char *const names[BBS_COUNT] = {
    "Commodore BBS Outpost", "Genetic-PET", "The Oasis BBS", "Microtown BBS",
    "Centronian BBS", "Particles! BBS", "RapidFire", "Dead Zone BBS",
    "C64 Ultimate Club", "Mutiny Community"
};
static const char *const hosts[BBS_COUNT] = {
    "www.commodorebbs.com", "g-point.tunk.org", "oasisbbs.hopto.org",
    "microtownbbs.com", "bbs.centronian.ca", "particlesbbs.dyndns.org",
    "rapidfire.hopto.org", "dzbbs.hopto.org", "c64u.club", "mutinybbs.com"
};
static const unsigned ports[BBS_COUNT] = {
    0, 1025, 6400, 6400, 6400, 6400, 64128u, 64128u, 6400, 2300
};
static const unsigned char modes[BBS_COUNT] = {
    BM_DIRECTORY, BM_PETSCII, BM_PETSCII, BM_PETSCII, BM_PETSCII,
    BM_PETSCII, BM_PETSCII, BM_PETSCII, BM_PETSCII, BM_AUTO
};

static struct book book;
static struct cfg cfg;
unsigned char cfgDefault, cfgEcho;
const char *bbMessage;
static const char *status = "Not connected";
static struct window box;
static char text[80];
static unsigned char inBook;

static const char cfgName[] = "BBS.CFG";
static const char bookName[] = "BBS.BOOK";

/* ------------------------------------------------------ the book ----- */

static unsigned char count(void)
{
    return BBS_COUNT + book.count;
}

const char *bb_name(unsigned char i)
{
    return i < BBS_COUNT ? names[i] : book.r[i - BBS_COUNT].name;
}

const char *bb_host(unsigned char i)
{
    return i < BBS_COUNT ? hosts[i] : book.r[i - BBS_COUNT].host;
}

unsigned bb_port(unsigned char i)
{
    return i < BBS_COUNT ? ports[i] : book.r[i - BBS_COUNT].port;
}

unsigned char bb_mode(unsigned char i)
{
    return i < BBS_COUNT ? modes[i] : book.r[i - BBS_COUNT].mode;
}

/* ------------------------------------------------------ the files ---- */

static unsigned char __fastcall__ cfg_sum(void)
{
    unsigned char s = 0, i;

    for (i = 0; i < 7; ++i)
        s += ((unsigned char *)&cfg)[i];
    return s;
}

/* a GEOS data file from memory; 0 = it worked */
static unsigned char save_file(const char *name, void *data, unsigned size)
{
    static struct fileheader h;

    memset(&h, 0, sizeof h);
    *(const char **)&h.n_block = name;
    h.icon_desc[0] = 0x03;
    h.icon_desc[1] = 0x15;
    h.icon_desc[2] = 0xbf;
    h.dostype = 0x83;           /* USR */
    h.type = APPL_DATA;
    h.structure = SEQUENTIAL;
    h.load_address = (unsigned)data;
    h.end_address = (unsigned)data + size;
    strcpy(h.class_name, "BBS data    V1.0");
    DeleteFile(name);
    return SaveFile(0, &h);
}

static unsigned char load_file(const char *name, void *data, unsigned size)
{
    if (FindFile(name))
        return 1;
    return ReadFile(&dirEntryBuf.n_block, data, size);
}

static void cfg_save(void)
{
    cfg.magic[0] = cfg.magic[1] = 'B';
    cfg.version = CFG_VERSION;
    cfg.deflt = cfgDefault;
    cfg.echo = cfgEcho;
    cfg.term = cfg.reserved = 0;
    cfg.sum = cfg_sum();
    bbMessage = save_file(cfgName, &cfg, sizeof cfg) ? "Could not save BBS.CFG" :
                                                       "Default saved in BBS.CFG";
}

static void book_save(void)
{
    if (save_file(bookName, &book, sizeof book))
        bbMessage = "Could not save BBS.BOOK";
}

static void load_all(void)
{
    if (load_file(bookName, &book, sizeof book) || book.magic[0] != 'B' ||
        book.magic[1] != 'K' || book.version != BOOK_VERSION ||
        book.count > BBS_CUSTOM) {
        memset(&book, 0, sizeof book);
        book.magic[0] = 'B';
        book.magic[1] = 'K';
        book.version = BOOK_VERSION;
    }
    cfgDefault = BBS_DEFAULT;
    cfgEcho = 0;
    if (!load_file(cfgName, &cfg, sizeof cfg) && cfg.magic[0] == 'B' &&
        cfg.magic[1] == 'B' && cfg.version == CFG_VERSION &&
        cfg.sum == cfg_sum() && cfg.deflt < count()) {
        cfgDefault = cfg.deflt;
        cfgEcho = cfg.echo;
    }
}

/* ------------------------------------------------------ the screen --- */

static void __fastcall__ fill(char top, char bot, unsigned l, unsigned r,
                              char pattern)
{
    box.top = top;
    box.bot = bot;
    box.left = l;
    box.right = r;
    InitDrawWindow(&box);
    SetPattern(pattern);
    Rectangle();
}

static void __fastcall__ frame(char top, char bot, unsigned l, unsigned r)
{
    box.top = top;
    box.bot = bot;
    box.left = l;
    box.right = r;
    InitDrawWindow(&box);
    FrameRectangle(0xff);
}

static void __fastcall__ button(char y, unsigned x, unsigned w, const char *t)
{
    fill(y, y + 12, x, x + w, 0);
    frame(y, y + 12, x, x + w);
    PutString((char *)t, y + 9, x + 8);
}

static const char *__fastcall__ mode_text(unsigned char m)
{
    return m == BM_DIRECTORY ? "Directory (info only)" :
           m == BM_ASCII ? "ASCII, 80 columns" :
           m == BM_AUTO ? "Auto (PETSCII, 40 columns)" : "PETSCII, 40 columns";
}

enum { B_CONNECT, B_DISC, B_BOOK, B_ECHO, B_BACK, BTNS };
#define BTNX 32
#define BTNW 160
static const char btnY[BTNS] = { 96, 112, 128, 144, 160 };
static const char *const btnText[BTNS] = {
    "Connect", "Disconnect", "Address book", "Local echo", "Back to deskTop"
};

static void show_msg(void)
{
    fill(180, 193, 16, 623, 0);
    if (bbMessage)
        PutString((char *)bbMessage, 190, 24);
}

static void show_echo(void)
{
    fill(btnY[B_ECHO], btnY[B_ECHO] + 12, BTNX + BTNW + 8, BTNX + BTNW + 60, 0);
    PutString(cfgEcho ? "on" : "off", btnY[B_ECHO] + 9, BTNX + BTNW + 12);
}

static void draw_main(void)
{
    unsigned char i, d = cfgDefault;

    inBook = 0;
    dispBufferOn = ST_WR_FORE;
    fill(0, 199, 0, 639, 0);
    fill(16, 198, 8, 631, 9);
    fill(20, 176, 16, 623, 0);
    fill(180, 193, 16, 623, 0);
    PutString(CBOLDON "BBS" CPLAINTEXT "  through the WiC64", 32, 32);
    PutString("Default BBS:", 48, 32);
    PutString((char *)bb_name(d), 48, 160);
    if (bb_mode(d) == BM_DIRECTORY)
        strcpy(text, bb_host(d));
    else
        sprintf(text, "%s:%u", bb_host(d), bb_port(d));
    PutString("Host:", 60, 32);
    PutString(text, 60, 160);
    PutString("Terminal:", 72, 32);
    PutString((char *)mode_text(bb_mode(d)), 72, 160);
    PutString("Status:", 84, 32);
    PutString((char *)status, 84, 160);
    for (i = 0; i < BTNS; ++i)
        button(btnY[i], BTNX, BTNW, btnText[i]);
    show_echo();
    show_msg();
    PutString("Keys: C, D, A, E, Q", 170, 400);
}

/* ------------------------------------------------------ the actions -- */

static void connect(void)
{
    unsigned char quit;

    GotoFirstMenu();
    if (bb_mode(cfgDefault) == BM_DIRECTORY) {
        bbMessage = "Directory only - no connect";
        show_msg();
        return;
    }
    bbMessage = NULL;
    quit = term_run();
    if (quit)
        EnterDeskTop();
    draw_main();
    ReDoMenu();
}

static void disconnect(void)
{
    GotoFirstMenu();
    bbMessage = "Not connected";
    show_msg();
}

static void toggle_echo(void)
{
    GotoFirstMenu();
    cfgEcho ^= 1;
    cfg_save();
    show_echo();
    show_msg();
}

static void quit_app(void)
{
    EnterDeskTop();
}

/* ------------------------------------------------------ address book - */

#define BK_TOP   34
#define BK_STEP  10
enum { K_ADD, K_EDIT, K_DEL, K_BACK, KBTNS };
#define KX 440
#define KW 120
static const char kY[KBTNS] = { 40, 58, 76, 112 };
static const char *const kText[KBTNS] = { "Add", "Edit", "Delete", "Back" };

static void draw_book(void)
{
    unsigned char i, n = count(), y;

    inBook = 1;
    fill(0, 199, 0, 639, 0);
    fill(16, 198, 8, 631, 9);
    fill(20, 193, 16, 623, 0);
    PutString(CBOLDON "BBS address book" CPLAINTEXT, 30, 32);
    for (i = 0; i < n; ++i) {
        y = BK_TOP + i * BK_STEP + 8;
        if (i == cfgDefault)
            PutString(">", y, 24);
        if (i >= BBS_COUNT)
            PutString(CBOLDON, y, 40);
        PutString((char *)bb_name(i), y, 40);
        if (bb_mode(i) == BM_DIRECTORY)
            PutString(CPLAINTEXT " (info only)", y, 220);
        else
            PutString(bb_mode(i) == BM_ASCII ? CPLAINTEXT " ASCII" : CPLAINTEXT " PETSCII",
                      y, 220);
        PutString(CPLAINTEXT, y, 40);
    }
    for (i = 0; i < KBTNS; ++i)
        button(kY[i], KX, KW, kText[i]);
    PutString("Click a BBS: new default", 150, KX - 16);
    if (bbMessage)
        PutString((char *)bbMessage, 170, KX - 16);
}

/* the form, in GEOS dialogs; 1 = saved */
static unsigned char __fastcall__ form(unsigned char slot, unsigned char isnew)
{
    static char name[BK_NAMEMAX + 1], host[BK_HOSTMAX + 1], port[6];
    unsigned long p;
    struct rec *r = &book.r[slot];

    if (isnew) {
        name[0] = host[0] = 0;
        strcpy(port, "23");
    } else {
        strcpy(name, r->name);
        strcpy(host, r->host);
        sprintf(port, "%u", r->port);
    }
    if (DlgBoxGetString(name, BK_NAMEMAX, CBOLDON "Name of the BBS", "") == CANCEL)
        return 0;
    if (DlgBoxGetString(host, BK_HOSTMAX, CBOLDON "Host", "") == CANCEL)
        return 0;
    if (DlgBoxGetString(port, 5, CBOLDON "Port", "") == CANCEL)
        return 0;
    p = atol(port);
    if (!name[0] || !host[0] || !p || p > 65535ul) {
        bbMessage = !name[0] ? "Fill in a name" : !host[0] ? "Fill in a host" :
                                                             "Port: 1 to 65535";
        return 0;
    }
    r->mode = DlgBoxYesNo(CBOLDON "Terminal: PETSCII?",
                          "No = ASCII, 80 columns") == NO ? BM_ASCII : BM_PETSCII;
    strcpy(r->name, name);
    strcpy(r->host, host);
    r->port = (unsigned)p;
    if (isnew)
        ++book.count;
    cfgDefault = BBS_COUNT + slot;
    book_save();
    cfg_save();
    return 1;
}

static void delete_own(void)
{
    unsigned char i, slot = cfgDefault - BBS_COUNT;

    for (i = slot; i + 1 < book.count; ++i)
        book.r[i] = book.r[i + 1];
    --book.count;
    cfgDefault = BBS_DEFAULT;
    book_save();
    cfg_save();
    bbMessage = "Deleted";
}

static void book_button(unsigned char b)
{
    bbMessage = NULL;
    if (b == K_BACK) {
        draw_main();
        ReDoMenu();
        return;
    }
    if (b == K_ADD) {
        if (book.count >= BBS_CUSTOM)
            bbMessage = "The address book is full";
        else
            form(book.count, 1);
    } else if (cfgDefault < BBS_COUNT) {
        bbMessage = "Only your own BBS can change";
    } else if (b == K_EDIT) {
        form(cfgDefault - BBS_COUNT, 0);
    } else {
        delete_own();
    }
    draw_book();
    ReDoMenu();
}

static void open_book(void)
{
    GotoFirstMenu();
    bbMessage = NULL;
    draw_book();
    ReDoMenu();
}

/* ------------------------------------------------------ the mouse ---- */

static void pressed(void)
{
    unsigned x = mouseXPos;
    unsigned char y = mouseYPos, i;

    if (mouseData & 0x80)       /* released */
        return;
    if (inBook) {
        if (x >= KX && x <= KX + KW)
            for (i = 0; i < KBTNS; ++i)
                if (y >= kY[i] && y <= kY[i] + 12) {
                    book_button(i);
                    return;
                }
        if (x < KX - 24 && y >= BK_TOP) {
            i = (y - BK_TOP) / BK_STEP;
            if (i < count()) {
                cfgDefault = i;
                cfg_save();
                draw_book();
                ReDoMenu();
            }
        }
        return;
    }
    if (x < BTNX || x > BTNX + BTNW)
        return;
    for (i = 0; i < BTNS; ++i)
        if (y >= btnY[i] && y <= btnY[i] + 12)
            switch (i) {
            case B_CONNECT:
                connect();
                return;
            case B_DISC:
                disconnect();
                return;
            case B_BOOK:
                open_book();
                return;
            case B_ECHO:
                toggle_echo();
                return;
            default:
                quit_app();
            }
}

static void key_pressed(void)
{
    unsigned char k = keyData;

    if (inBook) {
        if (k == 'b' || k == 27)
            book_button(K_BACK);
        else if (k == 'a')
            book_button(K_ADD);
        else if (k == 'e')
            book_button(K_EDIT);
        else if (k >= '0' && k <= '9' && k - '0' < count()) {
            cfgDefault = k - '0';
            cfg_save();
            draw_book();
            ReDoMenu();
        }
        return;
    }
    switch (k) {
    case 'c':
        connect();
        break;
    case 'd':
        disconnect();
        break;
    case 'a':
        open_book();
        break;
    case 'e':
        toggle_echo();
        break;
    case 'q':
        quit_app();
        break;
    }
}

/* ------------------------------------------------------ the menus ---- */

static void about(void)
{
    GotoFirstMenu();
    DlgBoxOk(CBOLDON "BBS V1.0" CPLAINTEXT, "GEOS-W128 for the C128 and the WiC64");
    if (inBook)
        draw_book();
    else
        draw_main();
    ReDoMenu();
}

static void book_menu(void)
{
    open_book();
}

static void main_menu(void)
{
    GotoFirstMenu();
    bbMessage = NULL;
    draw_main();
    ReDoMenu();
}

static struct menu geosMenu = {
    { 15, 15 + 14, 0, 100 }, 1 | VERTICAL,
    { { "about", MENU_ACTION, about } }
};

static struct menu fileMenu = {
    { 15, 15 + 2 * 14, 50, 170 }, 2 | VERTICAL,
    {
        { "main screen", MENU_ACTION, main_menu },
        { "quit", MENU_ACTION, quit_app },
    }
};

static struct menu bbsMenu = {
    { 15, 15 + 3 * 14, 90, 240 }, 3 | VERTICAL,
    {
        { "connect", MENU_ACTION, connect },
        { "address book", MENU_ACTION, book_menu },
        { "local echo", MENU_ACTION, toggle_echo },
    }
};

static struct menu mainMenu = {
    { 0, 14, 0, 140 }, 3 | HORIZONTAL,
    {
        { "geos", SUB_MENU, &geosMenu },
        { "file", SUB_MENU, &fileMenu },
        { "bbs", SUB_MENU, &bbsMenu },
    }
};

unsigned char __fastcall__ wic_call(unsigned char command, const char *tx,
                                    unsigned txlen, char *rx, unsigned rxsize)
{
    unsigned char st;

    R0 = (unsigned)tx;
    R1 = txlen;
    R2 = (unsigned)rx;
    R3 = rxsize - 1;
    st = wic_exec(command);
    rx[R4] = 0;
    return st;
}

#ifdef BBS_TEST
/* VICE test: the keys come from this table instead of the keyboard. An entry
   is a key; 0xfe waits a while first. */
static const unsigned char script[] = { BBS_TEST };
static unsigned char scriptPos, scriptWait;
unsigned char __fastcall__ test_script(void)
{
    unsigned char c;

    if (scriptWait) {
        if (--scriptWait)
            return 0;
    }
    if (scriptPos >= sizeof script)
        return 0;
    c = script[scriptPos++];
    if (c == 0xfe) {
        scriptWait = 200;
        return 0;
    }
    return c;
}
#endif

void main(void)
{
    load_all();
#ifdef BBS_TEST_HOST
    strcpy(book.r[0].name, "Test BBS");
    strcpy(book.r[0].host, "127.0.0.1");
    book.r[0].port = BBS_TEST_PORT;
    book.r[0].mode = BBS_TEST_MODE;
    book.count = 1;
    cfgDefault = BBS_COUNT;
#endif
    dispBufferOn = ST_WR_FORE;
    draw_main();
    DoMenu(&mainMenu);
    otherPressVec = pressed;
    keyVector = key_pressed;
#ifdef BBS_TEST_HOST
    connect();
#endif
    MainLoop();
}
