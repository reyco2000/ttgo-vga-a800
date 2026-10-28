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

/* List geometry: 8x8 text, 10-pixel rows, 40 columns inside the frame. */
enum { LIST_X = 8, LIST_Y = 40, ROW_H = 10, LIST_ROWS = 16, LIST_COLS = 40 };
enum { SETUP_ROWS = 4, TILES = 6 };

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

static void center(int y, const char *s, uint8_t fg, uint8_t bg) {
  Osd_Text((VIDEO_WIDTH - (int)strlen(s) * 8) / 2, y, s, fg, bg);
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

static void save_path(char *field, const char *path) {
  snprintf(field, sizeof(sv.s->disk), "%s", path);
  Settings_Save(sv.s);
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

static void draw_frame(const char *title) {
  Osd_Rect(0, 0, VIDEO_WIDTH, VIDEO_HEIGHT, OSD_BG);
  Osd_Frame(4, 4, VIDEO_WIDTH - 8, VIDEO_HEIGHT - 8, OSD_FG);
  center(12, title, OSD_FG, OSD_BG);
}

static void draw_row(int r, const char *text, bool selected) {
  char line[LIST_COLS + 1];
  fit(line, text, LIST_COLS);
  int y = LIST_Y + r * ROW_H;
  if (selected) {
    Osd_Rect(LIST_X - 2, y - 1, VIDEO_WIDTH - 2 * (LIST_X - 2), ROW_H, OSD_SEL_BG);
    Osd_Text(LIST_X, y, line, OSD_SEL_FG, OSD_SEL_BG);
  } else {
    Osd_Text(LIST_X, y, line, OSD_FG, OSD_BG);
  }
}

static void draw_main(void) {
  static const char *const labels[TILES] = {"DISKS", "CART", "RESET",
                                            "SETUP", "ABOUT", "RESUME"};
  draw_frame("A800 SUPERVISOR");
  for (int i = 0; i < TILES; i++) {
    int x = 12 + (i % 3) * 108, y = 36 + (i / 3) * 84;
    bool on = i == sv.tile;
    uint8_t bg = on ? OSD_SEL_BG : OSD_TILE, fg = on ? OSD_SEL_FG : OSD_FG;
    Osd_Rect(x, y, 96, 72, bg);
    Osd_Icon(x + 32, y + 14, (OsdIcon)i, fg, bg);
    Osd_Text(x + (96 - (int)strlen(labels[i]) * 8) / 2, y + 52, labels[i], fg, bg);
  }
  center(222, "ARROWS, ENTER   ESC/F12 CLOSE", OSD_FG, OSD_BG);
}

static void draw_list(void) {
  bool disks = sv.screen == SCR_DISKS;
  draw_frame(disks ? "DISKS" : "CARTRIDGE");
  char line[LIST_COLS + 1], text[sizeof(BrowserEntry::name) + 16];
  fit(line, browser.path + 3, LIST_COLS); /* drop the "/sd" mount point */
  Osd_Text(LIST_X, 24, line, OSD_FG, OSD_BG);
  int n = browser.count + 1;
  for (int r = 0; r < LIST_ROWS && sv.top + r < n; r++) {
    int i = sv.top + r;
    if (i == 0 && disks) {
      const char *m = Host_Media();
      snprintf(text, sizeof text, "EJECT D1: %s", m[0] ? base_name(m) : "EMPTY");
    } else if (i == 0) {
      snprintf(text, sizeof text, "REMOVE CARTRIDGE");
    } else {
      const BrowserEntry *e = &browser.entries[i - 1];
      snprintf(text, sizeof text, "%s%s", e->name, e->dir ? "/" : "");
    }
    draw_row(r, text, i == sv.sel);
  }
  if (browser.capped)
    center(206, "OVER 256 FILES: USE SUBFOLDERS", OSD_FG, OSD_BG);
  snprintf(text, sizeof text, "%d/%d  ENTER SELECT  ESC BACK", sv.sel + 1, n);
  center(222, text, OSD_FG, OSD_BG);
}

static void draw_setup(void) {
  char text[LIST_COLS + 1];
  draw_frame("SETUP");
  snprintf(text, sizeof text, "OS: %s", os_names[sv.edit.os]);
  draw_row(0, text, sv.sel == 0);
  draw_row(1, sv.edit.basic ? "BASIC: ON" : "BASIC: OFF", sv.sel == 1);
  draw_row(2, sv.edit.pal ? "VIDEO: PAL (UNTESTED)" : "VIDEO: NTSC", sv.sel == 2);
  draw_row(4, "APPLY AND RESTART", sv.sel == 3);
  center(206, "LEFT/RIGHT CHANGE", OSD_FG, OSD_BG);
  center(222, "ENTER APPLY   ESC CANCEL", OSD_FG, OSD_BG);
}

/* One "LABEL     value" line of the About screen. */
static void about_row(int r, const char *label, const char *fmt, ...) {
  char value[160], line[LIST_COLS + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(value, sizeof value, fmt, ap);
  va_end(ap);
  int n = snprintf(line, sizeof line, "%-10s", label);
  fit(line + n, value, LIST_COLS - 1 - n);
  Osd_Text(LIST_X + 8, LIST_Y + r * 12, line, OSD_FG, OSD_BG);
}

static void draw_about(void) {
  const Atari *a = sv.a;
  const char *media = Host_Media();
  const char *basic = !Host_Basic() ? "OFF"
                      : a->machine_type == A800_MACHINE_XL ? "ON (BUILT-IN)"
                                                           : "ON (CARTRIDGE)";
  draw_frame("ABOUT");
  int r = 0;
#define row(...) about_row(r++, __VA_ARGS__)
  row("FIRMWARE", "%s", FIRMWARE_VERSION);
  row("MACHINE", "ATARI %s %s", Atari_MachineName(a),
      a->video_standard == A800_PAL ? "PAL" : "NTSC");
  row("OS", "%s", base_name(Settings_OsPath(Host_BootedOs())));
  row("BASIC", "%s", basic);
  row("CART", "%s", a->cart && sv.s->cart[0] ? base_name(sv.s->cart) : "NONE");
  row("DISK", "%s", media[0] ? base_name(media) : "NONE");
  row("INTERNAL", "%u BYTES FREE",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  row("PSRAM", "%u BYTES FREE",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  row("SPEED", "%.1f FPS", Host_Fps());
#undef row
  center(222, "ESC BACK", OSD_FG, OSD_BG);
}

/* A box centred on the screen: a title line, then n choice rows. */
static void draw_popup(const char *title, const char *const *rows, int n) {
  int cols = (int)strlen(title);
  for (int i = 0; i < n; i++)
    if ((int)strlen(rows[i]) + 2 > cols)
      cols = (int)strlen(rows[i]) + 2;
  int w = cols * 8 + 32, h = 32 + n * 12;
  int x = (VIDEO_WIDTH - w) / 2, y = (VIDEO_HEIGHT - h) / 2;
  Osd_Rect(x, y, w, h, OSD_TILE);
  Osd_Frame(x, y, w, h, OSD_FG);
  center(y + 8, title, OSD_FG, OSD_TILE);
  for (int i = 0; i < n; i++) {
    int ry = y + 24 + i * 12, rw = ((int)strlen(rows[i]) + 2) * 8;
    bool on = i == sv.pop_sel;
    Osd_Rect((VIDEO_WIDTH - rw) / 2, ry - 2, rw, 11, on ? OSD_SEL_BG : OSD_TILE);
    center(ry, rows[i], on ? OSD_SEL_FG : OSD_FG, on ? OSD_SEL_BG : OSD_TILE);
  }
}

static void draw(void) {
  switch (sv.screen) {
  case SCR_MAIN: draw_main(); break;
  case SCR_DISKS: case SCR_CART: draw_list(); break;
  case SCR_SETUP: draw_setup(); break;
  case SCR_ABOUT: draw_about(); break;
  }
  static const char *const reset_rows[] = {"NO", "WARM", "COLD"};
  static const char *const confirm_rows[] = {"NO", "YES"};
  static const char *const ok_row[] = {"OK"};
  switch (sv.popup) {
  case POP_NONE: break;
  case POP_RESET: draw_popup("RESET THE ATARI?", reset_rows, 3); break;
  case POP_CONFIRM: draw_popup("REBOOT NOW?", confirm_rows, 2); break;
  case POP_MESSAGE: draw_popup(sv.message, ok_row, 1); break;
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
    message("NO SD CARD");
    return;
  }
  const char *const *exts = screen == SCR_DISKS ? disk_exts : cart_exts;
  const char *dir = sv.last_dir[screen == SCR_CART];
  if ((!dir[0] || Browser_Open(&browser, dir, exts)) &&
      Browser_Open(&browser, MEDIA_DIR, exts)) {
    message("CANNOT READ /ATARI");
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
      message("CANNOT READ FOLDER");
    else
      show_list(sv.screen);
    break;
  case ACT_MOUNT:
    if (Host_Mount(sv.path)) {
      message("CANNOT LOAD FILE");
      break;
    }
    save_path(s->disk, sv.path);
    close_menu();
    break;
  case ACT_EJECT:
    Host_Eject();
    save_path(s->disk, "");
    break;
  case ACT_CART:
    r = Host_Cart(sv.path);
    if (r) {
      message(r == -2 ? "CARTRIDGE TYPE NOT SUPPORTED" : "CANNOT LOAD FILE");
      break;
    }
    save_path(s->cart, sv.path);
    Host_ColdReset();
    close_menu();
    break;
  case ACT_CART_REMOVE:
    Host_Cart("");
    save_path(s->cart, "");
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
        message("NO CARTRIDGE INSERTED");
      }
    } else if (browser.entries[sv.sel - 1].dir) {
      sv.pending = ACT_ENTER_DIR;
    } else {
      Browser_FullPath(&browser, sv.sel - 1, sv.path, sizeof sv.path);
      if (strlen(sv.path) >= sizeof(sv.s->disk))
        message("PATH TOO LONG");
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
  case 3:
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
  Osd_Begin(a);
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
