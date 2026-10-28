/* SD file browser: POSIX dirent/stat only, so it builds on host and on the
 * ESP32 VFS (/sd). The entries array is malloc'd (PSRAM on the ESP32). */
#define A800_PSRAM_STORES
#include "browser.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static bool ext_ok(const char *name, const char *const *exts) {
  size_t n = strlen(name);
  for (; exts && *exts; exts++) {
    size_t e = strlen(*exts);
    if (n >= e && !strcasecmp(name + n - e, *exts)) return true;
  }
  return false;
}

static int cmp(const void *pa, const void *pb) {
  const BrowserEntry *a = (const BrowserEntry *)pa, *b = (const BrowserEntry *)pb;
  bool da = !strcmp(a->name, ".."), db = !strcmp(b->name, "..");
  if (da != db) return da ? -1 : 1;
  if (a->dir != b->dir) return a->dir ? -1 : 1;
  int r = strcasecmp(a->name, b->name);
  return r ? r : strcmp(a->name, b->name);
}

/* True when path is strictly below a non-empty root. */
static bool below_root(const Browser *b) {
  size_t r = strlen(b->root), p = strlen(b->path);
  if (r == 0 || p <= r || strncmp(b->path, b->root, r)) return false;
  return b->root[r - 1] == '/' || b->path[r] == '/';
}

/* Builds the new listing first and swaps it in only on success, so a failure
 * leaves the current listing intact. path may alias b->path (refresh). */
int Browser_Open(Browser *b, const char *path, const char *const *exts) {
  char np[sizeof b->path];
  if (snprintf(np, sizeof np, "%s", path) >= (int)sizeof np) return -1;
  size_t pl = strlen(np);
  while (pl > 1 && np[pl - 1] == '/') np[--pl] = 0;
  DIR *d = opendir(np);
  if (!d) return -1;
  BrowserEntry *ent = (BrowserEntry *)malloc(BROWSER_MAX * sizeof(BrowserEntry));
  if (!ent) { closedir(d); return -1; }

  Browser nb = *b;
  memcpy(nb.path, np, sizeof np);
  nb.entries = ent;
  nb.count = 0;
  nb.capped = false;
  if (below_root(&nb)) {
    strcpy(ent[0].name, "..");
    ent[0].dir = true;
    nb.count = 1;
  }
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.') continue;
    bool dir;
    if (de->d_type == DT_DIR) dir = true;
    else if (de->d_type == DT_REG) dir = false;
    else {  /* DT_UNKNOWN, symlinks: ask the filesystem */
      char full[sizeof nb.path + sizeof de->d_name + 2];
      snprintf(full, sizeof full, "%s/%s", nb.path, de->d_name);
      struct stat st;
      if (stat(full, &st)) continue;
      if (S_ISDIR(st.st_mode)) dir = true;
      else if (S_ISREG(st.st_mode)) dir = false;
      else continue;
    }
    if (!dir && !ext_ok(de->d_name, exts)) continue;
    size_t n = strlen(de->d_name);
    if (n >= sizeof ent[0].name) continue;
    if (nb.count >= BROWSER_MAX) { nb.capped = true; break; }
    BrowserEntry *e = &ent[nb.count++];
    memcpy(e->name, de->d_name, n + 1);
    e->dir = dir;
  }
  closedir(d);
  qsort(ent, nb.count, sizeof(BrowserEntry), cmp);
  free(b->entries);
  *b = nb;
  return 0;
}

void Browser_Close(Browser *b) {
  free(b->entries);
  b->entries = NULL;
  b->count = 0;
  b->capped = false;
  b->path[0] = 0;
}

int Browser_Enter(Browser *b, int index, const char *const *exts) {
  if (index < 0 || index >= b->count || !b->entries[index].dir) return -1;
  char np[sizeof b->path];
  if (!strcmp(b->entries[index].name, "..")) {
    if (!below_root(b)) return -1;
    snprintf(np, sizeof np, "%s", b->path);
    char *s = strrchr(np, '/');
    if (!s) return -1;
    *s = 0;
    if (strlen(np) < strlen(b->root) && b->root[0]) snprintf(np, sizeof np, "%s", b->root);
  } else if (snprintf(np, sizeof np, "%s/%s", b->path, b->entries[index].name) >= (int)sizeof np) {
    return -1;
  }
  return Browser_Open(b, np, exts);
}

void Browser_FullPath(const Browser *b, int index, char *out, size_t n) {
  if (n == 0) return;
  if (index < 0 || index >= b->count) { out[0] = 0; return; }
  snprintf(out, n, "%s/%s", b->path, b->entries[index].name);
}
