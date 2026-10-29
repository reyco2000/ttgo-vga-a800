/* Host tests for the pure settings functions. */
#include "../src/ui/settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static bool exists(const char *p) { return !strcmp(p, "/sd/a.atr") || !strcmp(p, "/sd/b.xex"); }

static Settings with_disk(const char *d) {
  Settings s = {};
  snprintf(s.disk, sizeof s.disk, "%s", d);
  return s;
}

static void choose_media_prefers_nvs() {
  char m[128];
  Settings s = with_disk("/sd/a.atr");
  Settings_ChooseMedia(&s, exists, "/sd/b.xex", m, sizeof m);
  CHECK(!strcmp(m, "/sd/a.atr"));
}

static void choose_media_skips_missing_nvs() {
  char m[128];
  Settings s = with_disk("/sd/gone.atr");
  Settings_ChooseMedia(&s, exists, "/sd/b.xex", m, sizeof m);
  CHECK(!strcmp(m, "/sd/b.xex"));
}

static void choose_media_none() {
  char m[128] = "junk";
  Settings s = with_disk("");
  Settings_ChooseMedia(&s, exists, "", m, sizeof m);
  CHECK(!m[0]);
  s = with_disk("/sd/gone.atr");
  Settings_ChooseMedia(&s, exists, "/sd/gone2.xex", m, sizeof m);
  CHECK(!m[0]);
}

static void os_path_values() {
  CHECK(!strcmp(Settings_OsPath(OS_XL), "/sd/atari/roms/ATARIXL.ROM"));
  CHECK(!strcmp(Settings_OsPath(OS_800B), "/sd/atari/roms/ATARIOSB.ROM"));
  CHECK(!strcmp(Settings_OsPath(OS_800A), "/sd/atari/roms/ATARIOSA.ROM"));
}

static void kbd_names() {
  CHECK(!strcmp(Settings_KbdName(KBD_US), "US English"));
  CHECK(!strcmp(Settings_KbdName(KBD_BRAZIL), "Portuguese BR"));
  CHECK(!strcmp(Settings_KbdName(KBD_COUNT), "US English")); /* bad NVS */
}

int main() {
  choose_media_prefers_nvs();
  choose_media_skips_missing_nvs();
  choose_media_none();
  os_path_values();
  kbd_names();
  printf("settings_test OK\n");
  return 0;
}
