/* WiCTest - checks the WiC64 driver: every command the applications use,
   large transfers byte for byte, a response larger than the buffer, an error
   status, and the time zone. Prints PASS/FAIL per test, for 640x200. */

#include <geos.h>
#include <string.h>
#include <stdio.h>
#include "../common/wic64.h"

#define WIC_FORCE_ERROR 0xfd
#define WIC_ECHO        0xfe

static char rx[1100];
static char tx[1024];
static char line[80];
static unsigned char row, col, failed, passed;
static struct window box = { 0, 199, 0, 639 };

unsigned char __fastcall__ wic_call(unsigned char command, const char *out,
                                    unsigned len, char *in, unsigned size)
{
    unsigned char st;

    R0 = (unsigned)out;
    R1 = len;
    R2 = (unsigned)in;
    R3 = size - 1;
    st = wic_exec(command);
    in[R4] = 0;
    return st;
}

static void __fastcall__ report(const char *name, unsigned char ok,
                                const char *info)
{
    sprintf(line, "%s %s: %.26s", ok ? "PASS" : CBOLDON "FAIL" CPLAINTEXT,
            name, info);
    PutString(line, 26 + row * 11, col ? 330 : 8);
    if (ok)
        ++passed;
    else
        ++failed;
    if (++row == 15) {
        row = 0;
        col = 1;
    }
}

static void __fastcall__ text_cmd(const char *name, unsigned char cmd)
{
    unsigned char st = wic_call(cmd, "", 0, rx, 80);

    report(name, st == WIC_OK && rx[0], rx);
}

/* send and receive n bytes through ECHO, compare them */
static void __fastcall__ echo(unsigned n)
{
    unsigned i, bad = 0;
    unsigned char st;
    static char name[12];

    for (i = 0; i < n; ++i)
        tx[i] = (char)(i * 7 + (i >> 8));
    memset(rx, 0, n);
    st = wic_call(WIC_ECHO, tx, n, rx, sizeof rx);
    for (i = 0; i < n; ++i)
        if (rx[i] != tx[i])
            ++bad;
    sprintf(name, "echo %u", n);
    sprintf(line + 40, "got %u, %u wrong", R4, bad);
    report(name, st == WIC_OK && R4 == n && !bad, line + 40);
}

void main(void)
{
    static const char url[] = "http://example.com/";
    static const char host[] = "example.com:80";
    static const char req[] = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    static const char second[] = { 1 };
    static const char zone3[] = { 3, 0 };   /* raw digits, low one first */
    static const char zone2[] = { 2, 0 };
    unsigned tries;
    unsigned char st;

    dispBufferOn = ST_WR_FORE | ST_WR_BACK;
    InitDrawWindow(&box);
    SetPattern(0);
    Rectangle();
    PutString(CBOLDON "WiCTest" CPLAINTEXT " - WiC64 driver test", 14, 8);

    text_cmd("firmware", WIC_GET_VERSION_STRING);
    st = wic_call(WIC_IS_CONNECTED, second, 1, rx, 80);
    report("connected", st == WIC_OK, st == WIC_NO_DEVICE ? "no answer" : "");
    text_cmd("ssid", WIC_GET_SSID);
    text_cmd("ip", WIC_GET_IP);
    text_cmd("mac", WIC_GET_MAC);
    text_cmd("rssi", WIC_GET_RSSI);
    text_cmd("time", WIC_GET_LOCAL_TIME);
    report("time format", rx[2] == ':' && rx[11] == '-' && strlen(rx) == 19, rx);

    echo(1);
    echo(255);
    echo(256);
    echo(1000);

    /* a response larger than the buffer: the rest must be dropped, and the
       next command must still work */
    st = wic_call(WIC_HTTP_GET, url, sizeof url - 1, rx, 33);
    report("http, small buffer", st == WIC_OK && R4 == 32, rx);
    echo(100);

    /* an error status */
    st = wic_call(WIC_FORCE_ERROR, "", 0, rx, 80);
    sprintf(line + 40, "status %u", st);
    report("error status", st != WIC_OK && st != WIC_NO_DEVICE, line + 40);
    st = wic_call(WIC_GET_STATUS_MESSAGE, "", 0, rx, 80);
    report("error message", st == WIC_OK && rx[0], rx);

    /* time zone: set index 3 (GMT+2), read back, set index 2 (GMT+1) */
    st = wic_call(WIC_SET_TIMEZONE, zone3, 2, rx, 80);
    report("set time zone", st == WIC_OK, "");
    st = wic_call(WIC_GET_TIMEZONE, "", 0, rx, 80);
    report("read time zone", st == WIC_OK && strcmp(rx, "7200") == 0, rx);
    wic_call(WIC_SET_TIMEZONE, zone2, 2, rx, 80);
    wic_call(WIC_GET_TIMEZONE, "", 0, rx, 80);
    report("time zone back", strcmp(rx, "3600") == 0, rx);

    /* TCP */
    st = wic_call(WIC_TCP_OPEN, host, sizeof host - 1, rx, 80);
    report("tcp open", st == WIC_OK, "");
    st = wic_call(WIC_TCP_WRITE, req, sizeof req - 1, rx, 80);
    report("tcp write", st == WIC_OK, "");
    rx[0] = 0;
    for (tries = 0; st == WIC_OK && !rx[0] && tries < 200; ++tries)
        st = wic_call(WIC_TCP_READ, "", 0, rx, 200);
    if (strchr(rx, '\r'))
        *strchr(rx, '\r') = 0;
    report("tcp read", st == WIC_OK && memcmp(rx, "HTTP/", 5) == 0, rx);
    st = wic_call(WIC_TCP_CLOSE, "", 0, rx, 80);
    report("tcp close", st == WIC_OK, "");

    sprintf(line, CBOLDON "%u passed, %u failed", passed, failed);
    PutString(line, 196, 8);
    MainLoop();
}
