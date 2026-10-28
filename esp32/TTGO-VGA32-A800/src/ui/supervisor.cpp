/* Supervisor state machine and screens. Keys only navigate and queue an
 * action; Supervisor_Update applies it (SD, machine, NVS) and redraws the
 * whole screen when dirty, a popup on top of the screen beneath it. Runs
 * from flash: nothing here is IRAM_ATTR. */
#include <Arduino.h>
#include "fabgl.h"
#include "supervisor.h"
#include "browser.h"
#include "osd.h"
#include "../hal/hal.h"
#include "../../config.h"
extern "C" {
#include "../core/AtariIo.h"
}

enum Screen { SCR_MAIN, SCR_DISKS, SCR_CART, SCR_SETUP, SCR_ABOUT };
enum Popup { POP_NONE, POP_RESET, POP_CONFIRM, POP_MESSAGE };
enum Action {
  ACT_NONE, ACT_CLOSE, ACT_OPEN_DISKS, ACT_OPEN_CART, ACT_ENTER_DIR,
  ACT_MOUNT, ACT_EJECT, ACT_CART, ACT_CART_REMOVE, ACT_WARM, ACT_COLD,
  ACT_APPLY
};
enum Nav {
  K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BACK, K_PARENT,
  K_PGUP, K_PGDN, K_HOME, K_END
};

/* Screen layout: a header bar with a colour stripe under it, the body, and
 * a footer bar of key hints. Lists use 8x14 text in 16-pixel rows, a
 * marker icon before each name and a scroll bar at the right. */
enum { HEADER_H = 24, STRIPE_H = 2, FOOTER_Y = 222 };
enum { LIST_X = 8, LIST_Y = 46, ROW_H = 16, LIST_ROWS = 10, LIST_COLS = 36 };
enum { NAME_X = LIST_X + 18, BAR_X = VIDEO_WIDTH - 12, BAR_W = 4 };
enum { TILE_W = 100, TILE_H = 84, TILE_GAP = 10, TILE_Y = 34 };
enum { SETUP_ROWS = 5, TILES = 6 };

static const char *const disk_exts[] = {".atr", ".xex", NULL};
static const char *const cart_exts[] = {".rom", ".car", NULL};
static const char *const os_names[] = {"XL", "800 OS-B", "800 OS-A"};

static struct {
  Atari *a;
  Settings *s;
  bool active, dirty, sd_ok;
  Screen screen;
  Popup popup;
  int tile, sel, top, pop_sel;
  Action pending, confirm;
  const char *message;
  Settings edit;            /* Setup's working copy */
  char path[sizeof(BrowserEntry::name) + sizeof(Browser::path) + 2];
  char last_dir[2][sizeof(Browser::path)]; /* Disks, Cart */
} sv;
static Browser browser;

/* --- helpers ------------------------------------------------------------ */

static const char *base_name(const char *path) {
  const char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}

/* Copies s into out (cols + 1 bytes), shortening long names to
 * head..ext so the extension stays visible. */
static void fit(char *out, const char *s, int cols) {
  int n = (int)strlen(s);
  if (n <= cols) {
    memcpy(out, s, (size_t)n + 1);
    return;
  }
  const char *dot = strrchr(s, '.');
  int tail = dot && s + n - dot <= 5 ? (int)(s + n - dot) : 0;
  int head = cols - 2 - tail;
  memcpy(out, s, (size_t)head);
  memcpy(out + head, "..", 2);
  memcpy(out + head + 2, s + n - tail, (size_t)tail + 1);
}

static void message(const char *text) {
  sv.popup = POP_MESSAGE;
  sv.message = text;
  sv.pop_sel = 0;
  sv.dirty = true;
}

static void ask(Action yes) {
  sv.popup = POP_CONFIRM;
  sv.confirm = yes;
  sv.pop_sel = 0; /* NO */
}

/* Every save records both running slots: disk = the mounted media ("" when
 * none), cart = the inserted game cartridge ("" when none). */
static void save_slots(const char *cart) {
  Settings *s = sv.s;
  snprintf(s->disk, sizeof s->disk, "%s", Host_Media());
  if (cart != s->cart)
    snprintf(s->cart, sizeof s->cart, "%s", cart);
  Settings_Save(s);
}

static int nav(int vk) {
  switch (vk) {
  case fabgl::VK_UP: case fabgl::VK_KP_UP: return K_UP;
  case fabgl::VK_DOWN: case fabgl::VK_KP_DOWN: return K_DOWN;
  case fabgl::VK_LEFT: case fabgl::VK_KP_LEFT: return K_LEFT;
  case fabgl::VK_RIGHT: case fabgl::VK_KP_RIGHT: return K_RIGHT;
  case fabgl::VK_RETURN: case fabgl::VK_KP_ENTER: case fabgl::VK_SPACE:
    return K_ENTER;
  case fabgl::VK_ESCAPE: return K_BACK;
  case fabgl::VK_BACKSPACE: return K_PARENT;
  case fabgl::VK_PAGEUP: case fabgl::VK_KP_PAGEUP: return K_PGUP;
  case fabgl::VK_PAGEDOWN: case fabgl::VK_KP_PAGEDOWN: return K_PGDN;
  case fabgl::VK_HOME: case fabgl::VK_KP_HOME: return K_HOME;
  case fabgl::VK_END: case fabgl::VK_KP_END: return K_END;
  default: return K_NONE;
  }
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* --- drawing ------------------------------------------------------------- */

struct Hint {
  const char *key, *label;
};

static void center(int y, const char *s, uint8_t fg, OsdFont font) {
  Osd_Text((VIDEO_WIDTH - Osd_TextWidth(s, font)) / 2, y, s, fg, OSD_CLEAR, font);
}

/* Header bar with the title and, at the right, a small note; an Atari
 * rainbow stripe under it. */
static void draw_frame(const char *title, const char *note) {
  static const uint8_t stripe[] = {
      OSD_RGB(3, 0, 0), OSD_RGB(3, 2, 0), OSD_RGB(3, 3, 0),
      OSD_RGB(0, 3, 0), OSD_RGB(0, 3, 3), OSD_RGB(1, 1, 3)};
  enum { N = sizeof stripe };
  Osd_Rect(0, 0, VIDEO_WIDTH, VIDEO_HEIGHT, OSD_BG);
  Osd_Rect(0, 0, VIDEO_WIDTH, HEADER_H, OSD_HEADER);
  for (int i = 0; i < N; i++)
    Osd_Rect(i * VIDEO_WIDTH / N, HEADER_H, VIDEO_WIDTH / N + 1, STRIPE_H, stripe[i]);
  Osd_Text(10, 4, title, OSD_TITLE, OSD_CLEAR, OSD_FONT_TITLE);
  if (note)
    Osd_Text(VIDEO_WIDTH - 10 - Osd_TextWidth(note, OSD_FONT_SMALL), 9, note,
             OSD_LABEL, OSD_CLEAR, OSD_FONT_SMALL);
}

/* Footer bar: each key on a light cap, followed by what it does. */
static void draw_hints(const Hint *h, int n) {
  int cw = Osd_FontWidth(OSD_FONT_SMALL), w = 0;
  for (int i = 0; i < n; i++)
    w += ((int)strlen(h[i].key) + (int)strlen(h[i].label)) * cw + 6 + 4 + 12;
  int x = (VIDEO_WIDTH - w + 12) / 2, y = FOOTER_Y + 5;
  Osd_Rect(0, FOOTER_Y, VIDEO_WIDTH, VIDEO_HEIGHT - FOOTER_Y, OSD_HEADER);
  for (int i = 0; i < n; i++) {
    int kw = Osd_TextWidth(h[i].key, OSD_FONT_SMALL) + 6;
    Osd_Rect(x, y - 2, kw, 12, OSD_KEY_BG);
    Osd_Text(x + 3, y, h[i].key, OSD_KEY_FG, OSD_CLEAR, OSD_FONT_SMALL);
    x = Osd_Text(x + kw + 4, y, h[i].label, OSD_FG, OSD_CLEAR, OSD_FONT_SMALL) + 12;
  }
}

static bool has_ext(const char *name, const char *ext) {
  const char *dot = strrchr(name, '.');
  return dot && !strcasecmp(dot, ext);
}

/* One list row: marker icon and name; the selected row on a bar. */
static void draw_row(int r, OsdIcon icon, const char *text, uint8_t fg, bool selected) {
  char line[LIST_COLS + 1];
  fit(line, text, LIST_COLS);
  int y = LIST_Y + r * ROW_H;
  if (selected)
    Osd_Rect(LIST_X - 2, y, BAR_X - 4 - (LIST_X - 2), ROW_H, OSD_SEL_BG);
  Osd_Icon(LIST_X + 2, y + 4, icon);
  Osd_Text(NAME_X, y + 1, line, selected ? OSD_SEL_FG : fg);
}

static void draw_scrollbar(int n) {
  if (n <= LIST_ROWS)
    return;
  int h = LIST_ROWS * ROW_H, thumb = h * LIST_ROWS / n;
  if (thumb < 8)
    thumb = 8;
  Osd_Rect(BAR_X, LIST_Y, BAR_W, h, OSD_PANEL);
  Osd_Rect(BAR_X, LIST_Y + (h - thumb) * sv.top / (n - LIST_ROWS), BAR_W, thumb,
           OSD_BORDER);
}

static void tile_note(int i, char *out, int cols) {
  const char *media = Host_Media(), *v = sv.s->pal ? "PAL" : "NTSC";
  char text[160];
  switch (i) {
  case 0: snprintf(text, sizeof text, "%s", media[0] ? base_name(media) : "No disk"); break;
  case 1:
    snprintf(text, sizeof text, "%s",
             sv.a->cart && sv.s->cart[0] ? base_name(sv.s->cart) : "No cartridge");
    break;
  case 2: snprintf(text, sizeof text, "Warm or cold"); break;
  case 3: snprintf(text, sizeof text, "%s  %s", os_names[sv.s->os], v); break;
  case 4: snprintf(text, sizeof text, "Version %s", FIRMWARE_VERSION); break;
  default: snprintf(text, sizeof text, "Return to Atari"); break;
  }
  fit(out, text, cols);
}

static void machine_note(char *out, int n) {
  snprintf(out, (size_t)n, "ATARI %s %s", Atari_MachineName(sv.a),
           sv.a->video_standard == A800_PAL ? "PAL" : "NTSC");
}

static void draw_main(void) {
  static const char *const labels[TILES] = {"Disks", "Cartridge", "Reset",
                                            "Setup", "About", "Resume"};
  static const Hint hints[] = {
      {"\x1b\x18\x19\x1a", "Move"}, {"Enter", "Select"}, {"Esc", "Close"}};
  char note[40];
  machine_note(note, sizeof note);
  draw_frame("A800 Supervisor", note);
  int x0 = (VIDEO_WIDTH - 3 * TILE_W - 2 * TILE_GAP) / 2;
  for (int i = 0; i < TILES; i++) {
    int x = x0 + (i % 3) * (TILE_W + TILE_GAP);
    int y = TILE_Y + (i / 3) * (TILE_H + TILE_GAP);
    bool on = i == sv.tile;
    Osd_Rect(x + 3, y + 3, TILE_W, TILE_H, OSD_SHADOW);
    Osd_Rect(x, y, TILE_W, TILE_H, on ? OSD_PANEL_HI : OSD_PANEL);
    if (on) {
      Osd_Frame(x, y, TILE_W, TILE_H, OSD_TITLE);
      Osd_Frame(x + 1, y + 1, TILE_W - 2, TILE_H - 2, OSD_TITLE);
    }
    Osd_Icon(x + (TILE_W - 32) / 2, y + 8, (OsdIcon)i, 2);
    Osd_Text(x + (TILE_W - Osd_TextWidth(labels[i])) / 2, y + 44, labels[i],
             on ? OSD_TITLE : OSD_FG);
    char text[24];
    tile_note(i, text, (TILE_W - 8) / Osd_FontWidth(OSD_FONT_SMALL));
    Osd_Text(x + (TILE_W - Osd_TextWidth(text, OSD_FONT_SMALL)) / 2, y + 64, text,
             on ? OSD_FG : OSD_DIM, OSD_CLEAR, OSD_FONT_SMALL);
  }
  draw_hints(hints, 3);
}

static void draw_list(void) {
  static const Hint hints[] = {
      {"Enter", "Open"}, {"Bksp", "Up"}, {"A-Z", "Jump"}, {"Esc", "Back"}};
  bool disks = sv.screen == SCR_DISKS;
  int n = browser.count + 1;
  char line[LIST_COLS + 8], text[sizeof(BrowserEntry::name) + 16];
  snprintf(line, sizeof line, "%d / %d", sv.sel + 1, n);
  draw_frame(disks ? "Disks" : "Cartridge", line);
  fit(line, browser.path + 3, LIST_COLS + 4); /* drop the "/sd" mount point */
  Osd_Icon(LIST_X + 2, 32, ICON_FOLDER);
  Osd_Text(NAME_X, 32, line, OSD_LABEL, OSD_CLEAR, OSD_FONT_SMALL);
  for (int r = 0; r < LIST_ROWS && sv.top + r < n; r++) {
    int i = sv.top + r;
    bool on = i == sv.sel;
    if (i == 0 && disks) {
      const char *m = Host_Media();
      if (m[0])
        snprintf(text, sizeof text, "Eject D1: %s", base_name(m));
      else
        snprintf(text, sizeof text, "D1: empty");
      draw_row(r, ICON_EJECT, text, OSD_ALERT, on);
    } else if (i == 0) {
      draw_row(r, ICON_EJECT, "Remove cartridge", OSD_ALERT, on);
    } else {
      const BrowserEntry *e = &browser.entries[i - 1];
      if (e->dir) {
        snprintf(text, sizeof text, "%s/", e->name);
        draw_row(r, ICON_FOLDER, text, OSD_LABEL, on);
      } else if (!disks) {
        draw_row(r, ICON_ROM, e->name, OSD_WARM, on);
      } else if (has_ext(e->name, ".xex")) {
        draw_row(r, ICON_XEX, e->name, OSD_GOOD, on);
      } else {
        draw_row(r, ICON_ATR, e->name, OSD_FG, on);
      }
    }
  }
  draw_scrollbar(n);
  if (browser.capped)
    center(LIST_Y + LIST_ROWS * ROW_H + 4, "Over 256 files: use subfolders",
           OSD_ALERT, OSD_FONT_SMALL);
  draw_hints(hints, 4);
}

/* One "label   < value >" line of the Setup screen. */
static void setup_row(int r, const char *label, const char *value) {
  bool on = sv.sel == r;
  int y = LIST_Y + r * 22;
  if (on)
    Osd_Rect(LIST_X + 8, y - 3, VIDEO_WIDTH - 2 * (LIST_X + 8), 20, OSD_SEL_BG);
  Osd_Text(LIST_X + 16, y, label, on ? OSD_SEL_FG : OSD_LABEL);
  int x = 184;
  x = Osd_Text(x, y, "\x11 ", on ? OSD_SEL_FG : OSD_DIM);
  x = Osd_Text(x, y, value, OSD_VALUE);
  Osd_Text(x, y, " \x10", on ? OSD_SEL_FG : OSD_DIM);
}

static void draw_setup(void) {
  static const Hint hints[] = {
      {"\x18\x19", "Move"}, {"\x1b\x1a", "Change"}, {"Enter", "Apply"}, {"Esc", "Cancel"}};
  draw_frame("Setup", "SAVED IN FLASH");
  setup_row(0, "Operating system", os_names[sv.edit.os]);
  setup_row(1, "BASIC", sv.edit.basic ? "On" : "Off");
  setup_row(2, "Video", sv.edit.pal ? "PAL (untested)" : "NTSC");
  setup_row(3, "Keyboard", Settings_KbdName(sv.edit.kbd));
  /* Apply is a button under the options. */
  static const char apply_text[] = "Apply and restart";
  bool on = sv.sel == 4;
  int w = Osd_TextWidth(apply_text) + 24, x = (VIDEO_WIDTH - w) / 2, y = LIST_Y + 4 * 22 + 14;
  Osd_Rect(x + 3, y + 3, w, 22, OSD_SHADOW);
  Osd_Rect(x, y, w, 22, on ? OSD_RGB(0, 2, 0) : OSD_PANEL);
  if (on)
    Osd_Frame(x, y, w, 22, OSD_GOOD);
  center(y + 4, apply_text, on ? OSD_FG : OSD_DIM, OSD_FONT_BODY);
  center(y + 40, "The machine restarts with the new settings.", OSD_DIM,
         OSD_FONT_SMALL);
  draw_hints(hints, 4);
}

/* One "LABEL     value" line of the About screen. */
static void about_row(int r, const char *label, uint8_t colour, const char *fmt, ...) {
  enum { VALUE_X = 120 };
  char value[160], line[LIST_COLS + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(value, sizeof value, fmt, ap);
  va_end(ap);
  int y = 36 + r * 19;
  Osd_Text(LIST_X + 16, y, label, OSD_LABEL);
  fit(line, value, (VIDEO_WIDTH - VALUE_X - 12) / Osd_FontWidth(OSD_FONT_BODY));
  Osd_Text(VALUE_X, y, line, colour);
}

static void draw_about(void) {
  static const Hint hints[] = {{"Esc", "Back"}};
  const Atari *a = sv.a;
  const char *media = Host_Media();
  const char *basic = !Host_Basic() ? "Off"
                      : a->machine_type == A800_MACHINE_XL ? "On (built in)"
                                                           : "On (cartridge)";
  char note[40];
  machine_note(note, sizeof note);
  draw_frame("About", note);
  int r = 0;
#define row(...) about_row(r++, __VA_ARGS__)
  row("Firmware", OSD_TITLE, "%s", FIRMWARE_VERSION);
  row("Machine", OSD_FG, "Atari %s %s", Atari_MachineName(a),
      a->video_standard == A800_PAL ? "PAL" : "NTSC");
  row("OS", OSD_FG, "%s", base_name(Settings_OsPath(Host_BootedOs())));
  row("BASIC", OSD_FG, "%s", basic);
  row("Cartridge", OSD_WARM, "%s", a->cart && sv.s->cart[0] ? base_name(sv.s->cart) : "None");
  row("Disk", OSD_GOOD, "%s", media[0] ? base_name(media) : "None");
  row("Internal", OSD_FG, "%u bytes free",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  row("PSRAM", OSD_FG, "%u bytes free",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  row("Speed", OSD_VALUE, "%.1f fps", Host_Fps());
#undef row
  draw_hints(hints, 1);
}

/* A dialog centred on the screen: a coloured title bar, up to two lines of
 * text, then a row of buttons. */
static void draw_popup(const char *title, uint8_t bar, const char *const *lines,
                       int nlines, const char *const *buttons, int n) {
  enum { BTN_H = 18, BTN_GAP = 10 };
  int bw = 0;
  for (int i = 0; i < n; i++)
    bw += Osd_TextWidth(buttons[i]) + 24 + (i ? BTN_GAP : 0);
  int w = bw;
  if (Osd_TextWidth(title) > w)
    w = Osd_TextWidth(title);
  for (int i = 0; i < nlines; i++)
    if (Osd_TextWidth(lines[i]) > w)
      w = Osd_TextWidth(lines[i]);
  w += 32;
  int h = 22 + 10 + nlines * 16 + 8 + BTN_H + 12;
  int x = (VIDEO_WIDTH - w) / 2, y = (VIDEO_HEIGHT - h) / 2;
  Osd_Rect(x + 5, y + 5, w, h, OSD_SHADOW);
  Osd_Rect(x, y, w, h, OSD_PANEL);
  Osd_Rect(x, y, w, 22, bar);
  Osd_Frame(x, y, w, h, OSD_BORDER);
  center(y + 4, title, OSD_FG, OSD_FONT_BODY);
  for (int i = 0; i < nlines; i++)
    center(y + 32 + i * 16, lines[i], OSD_FG, OSD_FONT_BODY);
  int bx = (VIDEO_WIDTH - bw) / 2, by = y + h - 12 - BTN_H;
  for (int i = 0; i < n; i++) {
    int tw = Osd_TextWidth(buttons[i]) + 24;
    bool on = i == sv.pop_sel;
    Osd_Rect(bx, by, tw, BTN_H, on ? OSD_SEL_BG : OSD_BG);
    Osd_Frame(bx, by, tw, BTN_H, on ? OSD_FG : OSD_BORDER);
    Osd_Text(bx + 12, by + 2, buttons[i], on ? OSD_SEL_FG : OSD_DIM);
    bx += tw + BTN_GAP;
  }
}

static void draw(void) {
  switch (sv.screen) {
  case SCR_MAIN: draw_main(); break;
  case SCR_DISKS: case SCR_CART: draw_list(); break;
  case SCR_SETUP: draw_setup(); break;
  case SCR_ABOUT: draw_about(); break;
  }
  static const char *const reset_buttons[] = {"No", "Warm", "Cold"};
  static const char *const confirm_buttons[] = {"No", "Yes"};
  static const char *const ok_button[] = {"OK"};
  static const char *const reset_lines[] = {"Reset the Atari?"};
  char file[LIST_COLS + 1];
  const char *lines[2];
  switch (sv.popup) {
  case POP_NONE: break;
  case POP_RESET:
    draw_popup("Reset", OSD_RGB(2, 1, 0), reset_lines, 1, reset_buttons, 3);
    break;
  case POP_CONFIRM:
    if (sv.confirm == ACT_CART_REMOVE) {
      lines[0] = "Remove the cartridge?";
    } else {
      fit(file, base_name(sv.path), 30);
      lines[0] = file;
    }
    lines[1] = "The Atari will reboot.";
    draw_popup(sv.confirm == ACT_CART_REMOVE ? "Remove cartridge"
               : sv.confirm == ACT_CART    ? "Insert cartridge"
                                           : "Load disk",
               OSD_HEADER, lines, 2, confirm_buttons, 2);
    break;
  case POP_MESSAGE:
    draw_popup("Notice", OSD_RGB(2, 0, 0), &sv.message, 1, ok_button, 1);
    break;
  }
}

/* --- actions (from Supervisor_Update) ------------------------------------ */

static void close_menu(void) {
  Browser_Close(&browser);
  Storage_End();
  Video_Resume();
  sv.active = false;
}

static void show_list(Screen screen) {
  sv.screen = screen;
  sv.sel = browser.count ? 1 : 0; /* first file rather than the action row */
  sv.top = 0;
}

static void open_list(Screen screen) {
  if (!sv.sd_ok)
    sv.sd_ok = Storage_Init(); /* the card may have been inserted since */
  if (!sv.sd_ok) {
    message("No SD card");
    return;
  }
  const char *const *exts = screen == SCR_DISKS ? disk_exts : cart_exts;
  const char *dir = sv.last_dir[screen == SCR_CART];
  if ((!dir[0] || Browser_Open(&browser, dir, exts)) &&
      Browser_Open(&browser, MEDIA_DIR, exts)) {
    message("Cannot read /atari");
    return;
  }
  show_list(screen);
}

static void apply(Action act) {
  Settings *s = sv.s;
  int r;
  if (act == ACT_NONE)
    return;
  sv.dirty = true;
  switch (act) {
  case ACT_NONE:
    break;
  case ACT_CLOSE:
    close_menu();
    break;
  case ACT_OPEN_DISKS:
    open_list(SCR_DISKS);
    break;
  case ACT_OPEN_CART:
    open_list(SCR_CART);
    break;
  case ACT_ENTER_DIR:
    if (Browser_Enter(&browser, sv.sel - 1,
                      sv.screen == SCR_DISKS ? disk_exts : cart_exts))
      message("Cannot read folder");
    else
      show_list(sv.screen);
    break;
  case ACT_MOUNT:
    if (Host_Mount(sv.path)) {
      message("Cannot load file");
      break;
    }
    save_slots(s->cart);
    close_menu();
    break;
  case ACT_EJECT:
    Host_Eject();
    save_slots(s->cart);
    break;
  case ACT_CART:
    r = Host_Cart(sv.path);
    if (r) {
      message(r == -2 ? "Cartridge type not supported" : "Cannot load file");
      break;
    }
    save_slots(sv.path);
    Host_ColdReset();
    close_menu();
    break;
  case ACT_CART_REMOVE:
    Host_Cart("");
    save_slots("");
    Host_ColdReset();
    close_menu();
    break;
  case ACT_WARM:
    Atari_WarmReset(sv.a);
    close_menu();
    break;
  case ACT_COLD:
    Host_ColdReset();
    close_menu();
    break;
  case ACT_APPLY:
    s->os = sv.edit.os;
    s->basic = sv.edit.basic;
    s->pal = sv.edit.pal;
    s->kbd = sv.edit.kbd;
    Settings_Save(s);
    Storage_End();
    esp_restart();
    break;
  }
}

/* --- keys ---------------------------------------------------------------- */

static void popup_key(int k, uint8_t ascii) {
  if (sv.popup == POP_MESSAGE) {
    sv.popup = POP_NONE;
    return;
  }
  int n = sv.popup == POP_RESET ? 3 : 2;
  if (sv.popup == POP_CONFIRM && (ascii == 'y' || ascii == 'Y'))
    sv.pop_sel = 1, k = K_ENTER;
  if (sv.popup == POP_CONFIRM && (ascii == 'n' || ascii == 'N'))
    k = K_BACK;
  switch (k) {
  case K_UP: case K_LEFT:
    sv.pop_sel = clamp(sv.pop_sel - 1, 0, n - 1);
    break;
  case K_DOWN: case K_RIGHT:
    sv.pop_sel = clamp(sv.pop_sel + 1, 0, n - 1);
    break;
  case K_BACK:
    sv.popup = POP_NONE;
    break;
  case K_ENTER:
    if (sv.popup == POP_RESET)
      sv.pending = sv.pop_sel == 1 ? ACT_WARM : sv.pop_sel == 2 ? ACT_COLD : ACT_NONE;
    else if (sv.pop_sel == 1)
      sv.pending = sv.confirm;
    sv.popup = POP_NONE;
    break;
  }
}

static void main_key(int k) {
  static const Action tile_action[TILES] = {ACT_OPEN_DISKS, ACT_OPEN_CART,
                                            ACT_NONE, ACT_NONE, ACT_NONE,
                                            ACT_CLOSE};
  int col = sv.tile % 3, row = sv.tile / 3;
  switch (k) {
  case K_LEFT: col = clamp(col - 1, 0, 2); break;
  case K_RIGHT: col = clamp(col + 1, 0, 2); break;
  case K_UP: row = 0; break;
  case K_DOWN: row = 1; break;
  case K_BACK: sv.pending = ACT_CLOSE; return;
  case K_ENTER:
    sv.pending = tile_action[sv.tile];
    if (sv.tile == 2) {
      sv.popup = POP_RESET;
      sv.pop_sel = 0;
    } else if (sv.tile == 3) {
      sv.edit = *sv.s;
      sv.screen = SCR_SETUP;
      sv.sel = 0;
    } else if (sv.tile == 4) {
      sv.screen = SCR_ABOUT;
    }
    return;
  }
  sv.tile = row * 3 + col;
}

static void list_key(int k, uint8_t ascii) {
  int n = browser.count + 1;
  switch (k) {
  case K_UP: sv.sel--; break;
  case K_DOWN: sv.sel++; break;
  case K_PGUP: sv.sel -= LIST_ROWS; break;
  case K_PGDN: sv.sel += LIST_ROWS; break;
  case K_HOME: sv.sel = 0; break;
  case K_END: sv.sel = n - 1; break;
  case K_BACK:
    memcpy(sv.last_dir[sv.screen == SCR_CART], browser.path, sizeof browser.path);
    Browser_Close(&browser);
    sv.screen = SCR_MAIN;
    return;
  case K_PARENT:
    if (browser.count && !strcmp(browser.entries[0].name, "..")) {
      sv.sel = 1;
      sv.pending = ACT_ENTER_DIR;
    }
    return;
  case K_ENTER:
    if (sv.sel == 0) {
      if (sv.screen == SCR_DISKS) {
        if (Host_Media()[0])
          sv.pending = ACT_EJECT;
      } else if (sv.s->cart[0]) {
        ask(ACT_CART_REMOVE);
      } else {
        message("No cartridge inserted");
      }
    } else if (browser.entries[sv.sel - 1].dir) {
      sv.pending = ACT_ENTER_DIR;
    } else {
      Browser_FullPath(&browser, sv.sel - 1, sv.path, sizeof sv.path);
      if (strlen(sv.path) >= sizeof(sv.s->disk))
        message("Path too long");
      else
        ask(sv.screen == SCR_DISKS ? ACT_MOUNT : ACT_CART);
    }
    return;
  default:
    /* A letter or digit jumps to the next name starting with it. */
    if (isalnum(ascii))
      for (int j = 1; j < n; j++) {
        int i = (sv.sel + j) % n;
        if (i && tolower((uint8_t)browser.entries[i - 1].name[0]) == tolower(ascii)) {
          sv.sel = i;
          break;
        }
      }
    break;
  }
  sv.sel = clamp(sv.sel, 0, n - 1);
  if (sv.sel < sv.top)
    sv.top = sv.sel;
  if (sv.sel >= sv.top + LIST_ROWS)
    sv.top = sv.sel - LIST_ROWS + 1;
}

static void setup_key(int k) {
  int step = k == K_LEFT ? -1 : k == K_RIGHT || k == K_ENTER ? 1 : 0;
  switch (k) {
  case K_UP: sv.sel = clamp(sv.sel - 1, 0, SETUP_ROWS - 1); return;
  case K_DOWN: sv.sel = clamp(sv.sel + 1, 0, SETUP_ROWS - 1); return;
  case K_BACK: sv.screen = SCR_MAIN; return;
  }
  if (!step)
    return;
  switch (sv.sel) {
  case 0: sv.edit.os = (OsChoice)((sv.edit.os + 3 + step) % 3); break;
  case 1: sv.edit.basic = !sv.edit.basic; break;
  case 2: sv.edit.pal = !sv.edit.pal; break;
  case 3: sv.edit.kbd = (KbdChoice)((sv.edit.kbd + KBD_COUNT + step) % KBD_COUNT); break;
  case 4:
    if (k == K_ENTER)
      sv.pending = ACT_APPLY;
    break;
  }
}

/* --- public -------------------------------------------------------------- */

void Supervisor_Open(Atari *a, Settings *s) {
  if (sv.active)
    return;
  sv.a = a;
  sv.s = s;
  Keyboard_ReleaseAll(a);
  Video_Pause(); /* no emulator line may land on the menu */
  Osd_Begin();
  sv.sd_ok = Storage_Init();
  snprintf(browser.root, sizeof browser.root, "%s", MEDIA_DIR);
  sv.active = sv.dirty = true;
  sv.screen = SCR_MAIN;
  sv.popup = POP_NONE;
  sv.tile = 0;
  sv.pending = ACT_NONE;
}

bool Supervisor_Active(void) { return sv.active; }

void Supervisor_Key(int vk, uint8_t ascii, bool down) {
  if (!sv.active || !down || sv.pending)
    return;
  if (vk == fabgl::VK_F12) {
    sv.pending = ACT_CLOSE;
    return;
  }
  int k = nav(vk);
  if (!k && !ascii)
    return;
  sv.dirty = true;
  if (sv.popup) {
    popup_key(k, ascii);
    return;
  }
  switch (sv.screen) {
  case SCR_MAIN: main_key(k); break;
  case SCR_DISKS: case SCR_CART: list_key(k, ascii); break;
  case SCR_SETUP: setup_key(k); break;
  case SCR_ABOUT:
    if (k == K_BACK || k == K_ENTER)
      sv.screen = SCR_MAIN;
    break;
  }
}

void Supervisor_Update(void) {
  if (!sv.active)
    return;
  Action act = sv.pending;
  sv.pending = ACT_NONE;
  apply(act);
  if (sv.active && sv.dirty) {
    sv.dirty = false;
    draw();
  }
}
