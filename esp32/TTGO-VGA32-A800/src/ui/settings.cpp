/* Saved settings in NVS, plus the pure boot-choice helpers (host-tested). */
#include "settings.h"
#include "../../config.h"
#include <stdio.h>
#include <string.h>
#ifdef ARDUINO
#include <Preferences.h>
#endif

const char *Settings_OsPath(OsChoice os) {
  switch (os) {
  case OS_800B: return ROM_DIR "/ATARIOSB.ROM";
  case OS_800A: return ROM_DIR "/ATARIOSA.ROM";
  default: return OS_ROM;
  }
}

const char *Settings_KbdName(KbdChoice kbd) {
  static const char *const names[KBD_COUNT] = {"US English", "Latam Spanish",
                                               "Italian", "Portuguese BR"};
  return kbd < KBD_COUNT ? names[kbd] : names[KBD_US];
}

void Settings_ChooseMedia(const Settings *s, bool (*exists)(const char *),
                          const char *autorun, char *media, size_t n) {
  const char *pick = "";
  if (s->disk[0] && exists(s->disk))
    pick = s->disk;
  else if (autorun && autorun[0] && exists(autorun))
    pick = autorun;
  snprintf(media, n, "%s", pick);
}

#ifdef ARDUINO
static void load_path(Preferences &p, const char *key, char *out, size_t n) {
  String v = p.getString(key, "");
  /* anything but a full SD path (or one that does not fit) is dropped */
  if (v.length() >= n || (v.length() && !v.startsWith("/sd/")))
    v = "";
  snprintf(out, n, "%s", v.c_str());
}

void Settings_Load(Settings *s) {
  Preferences p;
  memset(s, 0, sizeof *s);
  s->os = OS_XL;
  s->basic = USE_BASIC;
  s->pal = USE_PAL;
  s->kbd = (KbdChoice)KBD_LAYOUT;
  s->mouse_swap = MOUSE_SWAP;
  if (!p.begin("a800", true))
    return; /* namespace never written: first boot */
  uint8_t os = p.getUChar("os", OS_XL);
  if (os <= OS_800A)
    s->os = (OsChoice)os;
  s->basic = p.getBool("basic", USE_BASIC);
  s->pal = p.getBool("pal", USE_PAL);
  uint8_t kbd = p.getUChar("kbd", KBD_LAYOUT);
  if (kbd < KBD_COUNT)
    s->kbd = (KbdChoice)kbd;
  s->mouse_swap = p.getBool("mswap", MOUSE_SWAP);
  load_path(p, "disk", s->disk, sizeof s->disk);
  load_path(p, "cart", s->cart, sizeof s->cart);
  p.end();
}

void Settings_Save(const Settings *s) {
  Preferences p;
  if (!p.begin("a800", false))
    return;
  p.putUChar("os", s->os);
  p.putBool("basic", s->basic);
  p.putBool("pal", s->pal);
  p.putUChar("kbd", s->kbd);
  p.putBool("mswap", s->mouse_swap);
  p.putString("disk", s->disk);
  p.putString("cart", s->cart);
  p.end();
}
#endif
