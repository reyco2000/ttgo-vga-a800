/* F12 supervisor menu: tile main menu, disk and cartridge browsers, reset,
 * setup (with its key mapper) and about screens, drawn over the paused
 * emulator with Osd_*. All calls come from the emulation task (core 0). */
#ifndef A800_ESP32_SUPERVISOR_H
#define A800_ESP32_SUPERVISOR_H
#include <stdint.h>
#include "settings.h"
extern "C" {
#include "../core/Atari.h"
}

/* Pauses video, mounts the SD card and shows the main menu. s is the
 * running configuration; the menu edits and saves it. */
void Supervisor_Open(Atari *a, Settings *s);
bool Supervisor_Active(void);
/* vk is a fabgl::VirtualKey and sc its Keymap_Scancode; Keyboard_Poll
 * forwards every event here while the menu is open. */
void Supervisor_Key(int vk, uint8_t ascii, uint16_t sc, bool down);
/* Applies pending actions and redraws when needed. */
void Supervisor_Update(void);

/* Machine operations provided by the sketch (TTGO-VGA32-A800.ino). */
void Host_ColdReset(void);           /* the F5 reset, re-arming an XEX */
int Host_Mount(const char *path);    /* ATR or XEX, then cold reset; 0 or -1 */
void Host_Eject(void);               /* empties D1 and forgets the XEX */
const char *Host_Media(void);        /* mounted ATR or XEX path, or "" */
/* path "" removes (BASIC returns on an 800 OS); AtariIo_LoadCartridge's
 * result otherwise. Does not reset. */
int Host_Cart(const char *path);
OsChoice Host_BootedOs(void);        /* may differ from the saved choice */
bool Host_Basic(void);               /* BASIC actually enabled */
float Host_Fps(void);                /* last measured frame rate */

#endif
