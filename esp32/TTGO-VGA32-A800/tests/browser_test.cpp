/* Host tests for the file browser, using temp directory trees. */
#include "../src/ui/browser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static const char *const EXTS[] = {".atr", ".xex", NULL};
static char root[100];

static void touch(const char *dir, const char *name) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE *f = fopen(p, "w");
  CHECK(f);
  fclose(f);
}

static void mkd(const char *dir, const char *name) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  CHECK(mkdir(p, 0755) == 0);
}

static void setup() {
  snprintf(root, sizeof root, "/tmp/browser_test_XXXXXX");
  if (getenv("TMPDIR")) snprintf(root, sizeof root, "%s/browser_test_XXXXXX", getenv("TMPDIR"));
  CHECK(mkdtemp(root));
}

static void teardown() {
  char cmd[300];
  snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
  CHECK(system(cmd) == 0);
}

static void init(Browser *b) {
  memset(b, 0, sizeof *b);
  snprintf(b->root, sizeof b->root, "%s", root);
}

static void browser_filters_and_sorts() {
  setup();
  touch(root, "b.ATR"); touch(root, "a.atr"); touch(root, "z.xex");
  touch(root, "readme.txt"); touch(root, ".hidden.atr"); mkd(root, "Games");
  Browser b; init(&b);
  CHECK(Browser_Open(&b, root, EXTS) == 0);
  CHECK(b.count == 4);
  CHECK(!strcmp(b.entries[0].name, "Games") && b.entries[0].dir);
  CHECK(!strcmp(b.entries[1].name, "a.atr") && !b.entries[1].dir);
  CHECK(!strcmp(b.entries[2].name, "b.ATR"));
  CHECK(!strcmp(b.entries[3].name, "z.xex"));
  Browser_Close(&b);
  teardown();
}

static void browser_dotdot_below_root() {
  setup();
  mkd(root, "Games");
  char g[128]; snprintf(g, sizeof g, "%s/Games", root);
  touch(g, "x.atr");
  Browser b; init(&b);
  CHECK(Browser_Open(&b, g, EXTS) == 0);
  CHECK(b.count == 2);
  CHECK(!strcmp(b.entries[0].name, "..") && b.entries[0].dir);
  CHECK(!strcmp(b.entries[1].name, "x.atr"));
  char full[300];
  Browser_FullPath(&b, 1, full, sizeof full);
  CHECK(!strncmp(full, g, strlen(g)) && !strcmp(full + strlen(g), "/x.atr"));
  CHECK(Browser_Enter(&b, 0, EXTS) == 0);
  CHECK(!strcmp(b.path, root));
  CHECK(b.count == 1 && !strcmp(b.entries[0].name, "Games"));  /* no ".." at root */
  CHECK(Browser_Enter(&b, 0, EXTS) == 0);
  CHECK(!strcmp(b.path, g));
  Browser_Close(&b);
  teardown();
}

static void browser_empty_root_no_dotdot() {
  setup();
  mkd(root, "Games");
  char g[128]; snprintf(g, sizeof g, "%s/Games", root);
  Browser b; memset(&b, 0, sizeof b);
  CHECK(Browser_Open(&b, g, EXTS) == 0);
  CHECK(b.count == 0);
  Browser_Close(&b);
  teardown();
}

static void browser_caps_entries() {
  setup();
  for (int i = 0; i < 300; i++) { char n[32]; snprintf(n, sizeof n, "f%03d.atr", i); touch(root, n); }
  Browser b; init(&b);
  CHECK(Browser_Open(&b, root, EXTS) == 0);
  CHECK(b.count == 256);
  Browser_Close(&b);
  teardown();
}

static void browser_truncates_names() {
  setup();
  char n[128];
  memset(n, 'a', 96);
  strcpy(n + 96, ".atr");  /* 100 chars */
  touch(root, n);
  Browser b; init(&b);
  CHECK(Browser_Open(&b, root, EXTS) == 0);
  CHECK(b.count == 1);
  CHECK(strlen(b.entries[0].name) == 63);
  CHECK(b.entries[0].name[63] == 0);
  CHECK(b.truncated);
  Browser_Close(&b);
  teardown();
}

static void browser_missing_dir() {
  Browser b; memset(&b, 0, sizeof b);
  CHECK(Browser_Open(&b, "/nonexistent/dir/xyz", EXTS) == -1);
  CHECK(b.count == 0);
  Browser_Close(&b);
}

int main() {
  browser_filters_and_sorts();
  browser_dotdot_below_root();
  browser_empty_root_no_dotdot();
  browser_caps_entries();
  browser_truncates_names();
  browser_missing_dir();
  printf("browser_test: all passed\n");
  return 0;
}
