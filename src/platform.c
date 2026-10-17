#include "platform.h"
#include <gint/hardware.h>
#include <stdint.h>
#include <string.h>
int platform;
platform_keys_t platform_keys;
const platform_hints_t *platform_hints;
/* fx-CG100: BACK, HOME, OK, LEFT, RIGHT, TOOLS sit in the rows 7-9 that
   replaced the fx-CG50 function keys (gint keydev.c CG100_keymap). */
static const platform_keys_t keys_cg100={48,31,58,38,68,34,69,27};
/* fx-CG50: no OK key (EXE twice), EXIT, AC/ON, MENU, OPTN; gint keycode
   (row<<4)+(7-column), so LEFT 0x85 is 38 and RIGHT 0x76 is 27. */
static const platform_keys_t keys_cg50={31,31,38,27,47,10,48,58};
static const platform_hints_t hints_cg100={"OK / EXE","BACK / TOOLS","TOOLS",
    "OK: open   BACK: up / exit   TOOLS: rescan   PAGE: scroll"};
static const platform_hints_t hints_cg50={"EXE","EXIT / OPTN","OPTN",
    "EXE: open   EXIT: up / exit   OPTN: rescan   LEFT/RIGHT: page"};
int cg50_serial_open(unsigned char *mode);
int cg50_serial_close(int mode);
int cg50_serial_state(void);
/* OS 2.00 has no syscall table for these; the entries are called at fixed
   addresses, checked against 16 bytes of their code at startup. */
#define CG100_OPEN 0x8030a358u
#define CG100_CLOSE 0x8030a3f6u
#define CG100_STATE 0x8030a89eu
static int cg100_os_2_00(void)
{
    static const uint8_t open_sig[] = {
        0x2f,0xe6,0x4f,0x22,0x7f,0xf4,0xd1,0x77,
        0x1f,0x41,0x62,0x10,0x22,0x28,0x8f,0x42 };
    static const uint8_t close_sig[] = {
        0x2f,0xa6,0x2f,0xb6,0x2f,0xc6,0x2f,0xd6,
        0x2f,0xe6,0x4f,0x22,0xd1,0x4e,0x62,0x10 };
    static const uint8_t state_sig[] = {
        0xd6,0x5a,0x62,0x60,0x22,0x28,0x8d,0x01,
        0xe0,0x03,0xe0,0x01,0x00,0x0b,0x00,0x09 };
    return !memcmp((void *)CG100_OPEN,open_sig,sizeof open_sig) &&
        !memcmp((void *)CG100_CLOSE,close_sig,sizeof close_sig) &&
        !memcmp((void *)CG100_STATE,state_sig,sizeof state_sig);
}
const char *platform_init(void)
{
    if(gint[HWCALC]==HWCALC_FXCG100) {
        if(!cg100_os_2_00()) return "fx-CG100: needs OS 2.00 serial entries";
        platform=PLATFORM_CG100; platform_keys=keys_cg100; platform_hints=&hints_cg100;
        return NULL;
    }
    /* Also the Graph 90+E and fx-CG50 AU; the Prizm (fx-CG10/20) has a
       different RAM map, the fx-CG Manager no serial port. */
    platform_keys=keys_cg50; platform_hints=&hints_cg50;
    if(gint[HWCALC]!=HWCALC_FXCG50) return "Unsupported model";
    platform=PLATFORM_CG50;
    return NULL;
}
int platform_serial_state(void)
{
    if(platform==PLATFORM_CG50) return cg50_serial_state();
    return ((int (*)(void))(uintptr_t)CG100_STATE)();
}
int platform_serial_open(unsigned char mode[6])
{
    if(platform==PLATFORM_CG50) return cg50_serial_open(mode);
    return ((int (*)(unsigned char *))(uintptr_t)CG100_OPEN)(mode);
}
int platform_serial_close(int abort)
{
    if(platform==PLATFORM_CG50) return cg50_serial_close(abort);
    return ((int (*)(int))(uintptr_t)CG100_CLOSE)(abort);
}
