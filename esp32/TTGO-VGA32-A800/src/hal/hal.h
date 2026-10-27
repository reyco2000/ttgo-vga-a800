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

/* DAC1 timer ISR fed from a ring buffer. */
void Audio_Init(void);
int Audio_Fill(void);
void Audio_Push(const int16_t *samples, int count);
uint32_t Audio_Underruns(void);

/* PS/2 keyboard. Returns KEY_ACTION_* for keys the frontend handles. */
enum { KEY_ACTION_NONE = 0, KEY_ACTION_RESET = 1 };
void Keyboard_Init(void);
int Keyboard_Poll(Atari *a);

/* microSD mounted at /sd for the core's stdio file loading. */
bool Storage_Init(void);
bool Storage_ReadAutorun(char *path, int size);
void Storage_End(void);

#endif
