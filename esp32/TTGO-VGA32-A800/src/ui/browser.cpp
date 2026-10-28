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
  return strcasecmp(a->name, b->name);
}

/* True when path is strictly below a non-empty root. */
static bool below_root(const Browser *b) {
  size_t r = strlen(b->root), p = strlen(b->path);
  if (r == 0 || p <= r || strncmp(b->path, b->root, r)) return false;
  return b->root[r - 1] == '/' || b->path[r] == '/';
}

int Browser_Open(Browser *b, const char *path, const char *const *exts) {
  char root[sizeof b->root];
  memcpy(root, b->root, sizeof root);  /* caller-owned, survives re-open */
  Browser_Close(b);
  memcpy(b->root, root, sizeof root);
  DIR *d = opendir(path);
  if (!d) return -1;
  b->entries = (BrowserEntry *)malloc(BROWSER_MAX * sizeof(BrowserEntry));
  if (!b->entries) { closedir(d); return -1; }
  snprintf(b->path, sizeof b->path, "%s", path);
  size_t pl = strlen(b->path);
  while (pl > 1 && b->path[pl - 1] == '/') b->path[--pl] = 0;

  if (below_root(b)) {
    strcpy(b->entries[0].name, "..");
    b->entries[0].dir = true;
    b->count = 1;
  }
  while (b->count < BROWSER_MAX) {
    struct dirent *de = readdir(d);
    if (!de) break;
    if (de->d_name[0] == '.') continue;
    char full[sizeof b->path + sizeof de->d_name + 2];
    snprintf(full, sizeof full, "%s/%s", b->path, de->d_name);
    struct stat st;
    if (stat(full, &st)) continue;
    bool dir = S_ISDIR(st.st_mode);
    if (!dir && (!S_ISREG(st.st_mode) || !ext_ok(de->d_name, exts))) continue;
    BrowserEntry *e = &b->entries[b->count++];
    size_t n = strlen(de->d_name);
    if (n > sizeof e->name - 1) { n = sizeof e->name - 1; b->truncated = true; }
    memcpy(e->name, de->d_name, n);
    e->name[n] = 0;
    e->dir = dir;
  }
  closedir(d);
  qsort(b->entries, b->count, sizeof(BrowserEntry), cmp);
  return 0;
}

void Browser_Close(Browser *b) {
  free(b->entries);
  b->entries = NULL;
  b->count = 0;
  b->truncated = false;
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
