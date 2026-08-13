#ifndef A800_STATE_H
#define A800_STATE_H
#include "Atari.h"

/* Diagnostic snapshots are intentionally same-build-only. */
int State_Save(const Atari *atari, const char *path);
int State_Load(Atari *atari, const char *path);

#endif
