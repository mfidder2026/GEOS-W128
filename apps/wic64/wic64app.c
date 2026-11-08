/* WiC64 - the WiC64 network adapter under GEOS 128, in 40 or 80 columns.

   Shows firmware, WiFi connection, address, signal and time; connects to a
   WiFi network, sets the time zone and tests the internet with a real HTTP
   request and a TCP connection. Screen coordinates are for 640x200; X()
   halves them in 40 columns. Text keeps its size, so it must fit in 320. */

#include <geos.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/wic64.h"

#define ROW0    48
#define ROWH    12
#define LABELX  32
#define VALX    272
#define X(v)    (wide ? (v) : (v) >> 1)
#define VALLEN  48

enum { R_FIRM, R_CONN, R_SSID, R_IP, R_MAC, R_RSSI, R_TIME, R_ZONE, ROWS };

static const char *const labels[ROWS] = {
    "Firmware", "WiFi", "Network (SSID)", "IP address", "MAC address",
    "Signal (RSSI)", "Time (WiC64)", "Time zone"
};

static char values[ROWS][VALLEN];
static char rx[160];
static char line[80];
static char ssid[33], pass[64], zone[3];
static struct window box;
static unsigned char wide;      /* 80 columns */

unsigned char __fastcall__ wic_call(unsigned char command, const char *tx,
                                    unsigned txlen, char *out, unsigned size)
{
    unsigned char st;

    R0 = (unsigned)tx;
    R1 = txlen;
    R2 = (unsigned)out;
    R3 = size - 1;
    st = wic_exec(command);
    out[R4] = 0;
    return st;
}

static void __fastcall__ fill(char top, char bot, unsigned left,
                              unsigned right, char pattern)
{
    box.top = top;
    box.bot = bot;
    box.left = X(left);
    box.right = X(right);
    InitDrawWindow(&box);
    SetPattern(pattern);
    Rectangle();
}

static void __fastcall__ message(const char *text)
{
    fill(180, 193, 16, 623, 0);
    PutString((char *)text, 190, X(24));
}

static void __fastcall__ draw_row(unsigned char i)
{
    char y = ROW0 + i * ROWH;

    fill(y - 8, y + 2, VALX, 623, 0);
    PutString(values[i], y, X(VALX));
}

/* the reason for a failed command, as the WiC64 reports it */
static const char *__fastcall__ reason(unsigned char st)
{
    if (st == WIC_NO_DEVICE)
        return "No answer: is the WiC64 on the userport?";
    wic_call(WIC_GET_STATUS_MESSAGE, "", 0, rx, sizeof rx);
    return rx[0] ? rx : "Error";
}

static void __fastcall__ read_row(unsigned char row, unsigned char cmd)
{
    unsigned char st = wic_call(cmd, "", 0, values[row], VALLEN);

    if (st != WIC_OK)
        strcpy(values[row], st == WIC_NO_DEVICE ? "no WiC64" : "-");
    draw_row(row);
}

static void reload(void)
{
    static const char oneSecond[] = { 1 };
    unsigned char st;

    GotoFirstMenu();
    message("Reading the WiC64...");
    read_row(R_FIRM, WIC_GET_VERSION_STRING);
    st = wic_call(WIC_IS_CONNECTED, oneSecond, 1, rx, sizeof rx);
    strcpy(values[R_CONN], st == WIC_OK ? "connected" :
           st == WIC_NO_DEVICE ? "no WiC64" : "not connected");
    draw_row(R_CONN);
    read_row(R_SSID, WIC_GET_SSID);
    read_row(R_IP, WIC_GET_IP);
    read_row(R_MAC, WIC_GET_MAC);
    read_row(R_RSSI, WIC_GET_RSSI);
    read_row(R_TIME, WIC_GET_LOCAL_TIME);
    read_row(R_ZONE, WIC_GET_TIMEZONE);
    message(st == WIC_NO_DEVICE ? reason(st) : "Ready.");
}

static void connect_wifi(void)
{
    static char payload[100];
    unsigned n;
    unsigned char st;

    GotoFirstMenu();
    if (DlgBoxGetString(ssid, 32, CBOLDON "Name of the WiFi network",
                        "") == CANCEL)
        return;
    pass[0] = 0;
    if (DlgBoxGetString(pass, 63, CBOLDON "WiFi password", "") == CANCEL)
        return;
    /* <ssid> $01 <passphrase> $01 $01 */
    n = sprintf(payload, "%s\x01%s\x01\x01", ssid, pass);
    memset(pass, 0, sizeof pass);
    message("Connecting...");
    st = wic_call(WIC_CONNECT_SSID, payload, n, rx, sizeof rx);
    memset(payload, 0, sizeof payload);
    if (st != WIC_OK) {
        message(reason(st));
        return;
    }
    reload();
    message("Connect request sent; reload shows the result.");
}

static void set_zone(void)
{
    static char payload[2];
    unsigned char n, st;

    GotoFirstMenu();
    zone[0] = 0;
    if (DlgBoxGetString(zone, 2, CBOLDON "Time zone index (0-31)",
                        "2 = GMT+1, 3 = GMT+2") == CANCEL)
        return;
    n = atoi(zone);
    /* low digit first. Plain values, not ASCII digits: the firmware computes
       high * 10 + low from the raw bytes, whatever its documentation says */
    payload[0] = n % 10;
    payload[1] = n / 10;
    st = wic_call(WIC_SET_TIMEZONE, payload, 2, rx, sizeof rx);
    if (st != WIC_OK) {
        message(reason(st));
        return;
    }
    reload();
    message("Time zone set; GEOS takes the time at the next start.");
}

static void test_http(void)
{
    static const char url[] = "http://example.com/";
    unsigned char st;

    GotoFirstMenu();
    message("HTTP GET http://example.com/ ...");
    st = wic_call(WIC_HTTP_GET, url, sizeof url - 1, rx, sizeof rx);
    if (st != WIC_OK) {
        message(reason(st));
        return;
    }
    rx[wide ? 60 : 36] = 0;
    sprintf(line, "HTTP OK: %s", rx);
    if (strchr(line, '\n'))
        *strchr(line, '\n') = 0;
    message(line);
}

static void test_tcp(void)
{
    static const char host[] = "example.com:80";
    static const char req[] =
        "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    unsigned tries;
    unsigned char st;

    GotoFirstMenu();
    message("TCP connection to example.com:80 ...");
    st = wic_call(WIC_TCP_OPEN, host, sizeof host - 1, rx, sizeof rx);
    if (st != WIC_OK) {
        message(reason(st));
        return;
    }
    st = wic_call(WIC_TCP_WRITE, req, sizeof req - 1, rx, sizeof rx);
    rx[0] = 0;
    for (tries = 0; st == WIC_OK && !rx[0] && tries < 200; ++tries)
        st = wic_call(WIC_TCP_READ, "", 0, rx, sizeof rx);
    if (st != WIC_OK) {
        message(reason(st));
    } else if (rx[0]) {
        rx[40] = 0;
        if (strchr(rx, '\r'))
            *strchr(rx, '\r') = 0;
        sprintf(line, "TCP OK: %s", rx);
        message(line);
    } else {
        message("TCP: connected, but no answer");
    }
    wic_call(WIC_TCP_CLOSE, "", 0, rx, sizeof rx);
}

static void about(void)
{
    GotoFirstMenu();
    DlgBoxOk(CBOLDON "WiC64 V1.0" CPLAINTEXT, "GEOS-W128 for the C128");
}

static void quit(void)
{
    EnterDeskTop();
}

static struct menu geosMenu = {
    { 15, 15 + 14, 0, 100 }, 1 | VERTICAL,
    { { "about", MENU_ACTION, about } }
};

static struct menu fileMenu = {
    { 15, 15 + 2 * 14, 50, 170 }, 2 | VERTICAL,
    {
        { "reload", MENU_ACTION, reload },
        { "quit",   MENU_ACTION, quit },
    }
};

static struct menu netMenu = {
    { 15, 15 + 4 * 14, 94, 290 }, 4 | VERTICAL,
    {
        { "connect to WiFi",    MENU_ACTION, connect_wifi },
        { "set time zone",      MENU_ACTION, set_zone },
        { "test HTTP request",  MENU_ACTION, test_http },
        { "test TCP connection", MENU_ACTION, test_tcp },
    }
};

static struct menu mainMenu = {
    { 0, 14, 0, 200 }, 3 | HORIZONTAL,
    {
        { "geos",    SUB_MENU, &geosMenu },
        { "file",    SUB_MENU, &fileMenu },
        { "network", SUB_MENU, &netMenu },
    }
};

void main(void)
{
    unsigned char i;

    wide = graphMode & 0x80;
    dispBufferOn = ST_WR_FORE | ST_WR_BACK;
    fill(0, 199, 0, 639, 0);        /* from the top: no deskTop menu left */
    fill(16, 198, 8, 631, 9);
    fill(20, 176, 16, 623, 0);
    fill(180, 193, 16, 623, 0);
    PutString(CBOLDON "WiC64" CPLAINTEXT "  network adapter on the C128", 32,
              X(24));
    for (i = 0; i < ROWS; ++i)
        PutString((char *)labels[i], ROW0 + i * ROWH, X(LABELX));
    DoMenu(&mainMenu);
    reload();
#ifdef WIC_SELFTEST             /* VICE test without clicking: 1 HTTP, 2 TCP */
    if (WIC_SELFTEST == 1)
        test_http();
    else
        test_tcp();
#endif
    MainLoop();
}
