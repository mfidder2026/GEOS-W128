/* GEOS-W128: WiC64 driver for applications, see wic64.s */

#ifndef WIC64_H
#define WIC64_H

#define R0 (*(unsigned *)0x02)
#define R1 (*(unsigned *)0x04)
#define R2 (*(unsigned *)0x06)
#define R3 (*(unsigned *)0x08)
#define R4 (*(unsigned *)0x0a)

/* commands, from the wic64-library */
#define WIC_GET_VERSION_STRING  0x00
#define WIC_HTTP_GET            0x01
#define WIC_CONNECT_SSID        0x02
#define WIC_GET_IP              0x06
#define WIC_GET_SSID            0x10
#define WIC_GET_RSSI            0x11
#define WIC_GET_MAC             0x14
#define WIC_GET_LOCAL_TIME      0x15
#define WIC_SET_TIMEZONE        0x16
#define WIC_GET_TIMEZONE        0x17
#define WIC_TCP_OPEN            0x21
#define WIC_TCP_READ            0x22
#define WIC_TCP_WRITE           0x23
#define WIC_GET_STATUS_MESSAGE  0x2a
#define WIC_IS_CONNECTED        0x2c
#define WIC_TCP_CLOSE           0x2e
#define WIC_TCP_AVAILABLE       0x30

#define WIC_OK          0
#define WIC_NO_DEVICE   0xff    /* no answer on the userport */

unsigned char __fastcall__ wic_exec(unsigned char command);

/* send tx (txlen bytes), answer as a string in rx (rxsize bytes room);
   returns the status, rx is always terminated */
unsigned char __fastcall__ wic_call(unsigned char command, const char *tx,
                                    unsigned txlen, char *rx, unsigned rxsize);

#endif
