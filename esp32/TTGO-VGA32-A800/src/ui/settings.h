/* Saved machine settings (NVS) and the boot choices derived from them. */
#pragma once
#include <stddef.h>
#include <stdint.h>

enum OsChoice : uint8_t { OS_XL = 0, OS_800B = 1, OS_800A = 2 };

struct Settings {
  OsChoice os;
  bool basic;
  bool pal;
  char disk[128]; /* full /sd/... path or "" */
  char cart[128]; /* full /sd/... path or "" */
};

/* NVS namespace "a800"; missing or invalid values take the config.h defaults.
 * Only built for the board (Preferences). */
void Settings_Load(Settings *s);
void Settings_Save(const Settings *s);

const char *Settings_OsPath(OsChoice os);

/* media = s->disk if it exists, else autorun if non-empty and it exists,
 * else "". */
void Settings_ChooseMedia(const Settings *s, bool (*exists)(const char *),
                          const char *autorun, char *media, size_t n);
