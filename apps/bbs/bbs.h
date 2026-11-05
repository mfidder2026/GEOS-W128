/* GEOS-W128 BBS client: what the parts share */

#ifndef BBS_H
#define BBS_H

#define BBS_COUNT   10          /* built in */
#define BBS_CUSTOM  6           /* your own, in BBS.BOOK */
#define BBS_MAX     (BBS_COUNT + BBS_CUSTOM)
#define BK_NAMEMAX  20
#define BK_HOSTMAX  32

#define BM_DIRECTORY 0
#define BM_PETSCII   1
#define BM_ASCII     2
#define BM_AUTO      3          /* AUTO = PETSCII */

/* an entry of the address book */
const char *bb_name(unsigned char i);
const char *bb_host(unsigned char i);
unsigned bb_port(unsigned char i);
unsigned char bb_mode(unsigned char i);

extern unsigned char cfgDefault, cfgEcho;
extern const char *bbMessage;

/* the session with the default BBS; returns 1 for Q: leave to deskTop */
unsigned char term_run(void);

unsigned char __fastcall__ wic_call(unsigned char command, const char *tx,
                                    unsigned txlen, char *rx, unsigned rxsize);

/* term.c, for xmodem.c */
extern unsigned char tmCols;
extern unsigned char tnSeen;
unsigned char term_key(void);   /* next PETSCII key, 0 = none */
void tm_txput(unsigned char c);
void tm_status(const char *text);

/* xmodem.c */
extern unsigned char xmOn;
void xm_start(void);
void xm_byte(unsigned char c);
void xm_tick(void);
void xm_stopkey(void);
void xm_close(void);

/* vdc.s */
extern unsigned char frames;
void __fastcall__ vdc_enter(unsigned char columns);
void vdc_leave(void);
void vdc_font(void);
void __fastcall__ vdc_putc(unsigned addr, char c, char attr);
void __fastcall__ vdc_fill(unsigned addr, char value, unsigned n);
void __fastcall__ vdc_copy(unsigned dst, unsigned src, unsigned n);
void __fastcall__ vdc_cursor(unsigned addr);
void __fastcall__ vdc_case(unsigned char lower, unsigned cells);
unsigned char kbd_scan(void);
extern unsigned char kbdMods;
void frame_tick(void);
unsigned __fastcall__ crc16(const char *p, unsigned n);

#endif
