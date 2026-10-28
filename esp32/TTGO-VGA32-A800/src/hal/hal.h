/* Board services used by the TTGO VGA32 sketch. */
#ifndef A800_ESP32_HAL_H
#define A800_ESP32_HAL_H
#include <stdint.h>
extern "C" {
#include "../core/Atari.h"
}

/* FabGL VGA output; installs the core's line sink. */
bool Video_Init(Atari *a);
uint32_t Video_DrawerBusyUs(void); /* core 1 drawing time so far */
/* Pause returns once every queued line is in the framebuffer; call it from
 * the emulation task so nothing new is queued. Resume is the symmetric hook
 * (the next frame repaints every row). */
void Video_Pause(void);
void Video_Resume(void);
uint8_t *Video_Row(int y);             /* framebuffer row, 0..VIDEO_HEIGHT-1 */
uint8_t Video_Colour(uint8_t atari_colour); /* raw FabGL byte for a palette index */
uint8_t Video_Rgb(uint8_t rgb222);          /* raw FabGL byte for R | G<<2 | B<<4 */

/* DAC1 timer ISR fed from a ring buffer. */
void Audio_Init(void);
int Audio_Fill(void);
void Audio_Push(const int16_t *samples, int count);
uint32_t Audio_Underruns(void);

/* PS/2 keyboard. Returns KEY_ACTION_* for keys the frontend handles; while
 * the supervisor is open every event goes to Supervisor_Key instead. */
enum { KEY_ACTION_NONE = 0, KEY_ACTION_RESET = 1, KEY_ACTION_SUPERVISOR = 2 };
void Keyboard_Init(void);
/* KbdChoice (settings.h); off US, right Alt is AltGr, not the trigger. */
void Keyboard_SetLayout(uint8_t layout);
int Keyboard_Poll(Atari *a);
/* Releases whatever the Atari sees held: key, joystick, trigger, console. */
void Keyboard_ReleaseAll(Atari *a);

/* microSD mounted at /sd for the core's stdio file loading. */
bool Storage_Init(void);
bool Storage_ReadAutorun(char *path, int size);
void Storage_End(void);

#endif
