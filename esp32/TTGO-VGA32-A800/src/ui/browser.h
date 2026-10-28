/* SD file browser: lists directories and files matching an extension set. */
#pragma once
#include <stddef.h>

#define BROWSER_MAX 256

struct BrowserEntry {
  char name[64];
  bool dir;
};

struct Browser {
  BrowserEntry *entries;
  int count;
  bool truncated;  /* a name was cut to 63 bytes */
  char path[128];
  char root[128];  /* set by caller; ".." is offered only below it ("" = never) */
};

/* exts: NULL-terminated, lower case, e.g. {".atr", ".xex", NULL}. 0 or -1. */
int Browser_Open(Browser *b, const char *path, const char *const *exts);
void Browser_Close(Browser *b);
/* Descends into a directory or "..". Returns 0 or -1. */
int Browser_Enter(Browser *b, int index, const char *const *exts);
void Browser_FullPath(const Browser *b, int index, char *out, size_t n);
