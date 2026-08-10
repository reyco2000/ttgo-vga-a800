#ifndef A800_ATARI_IO_H
#define A800_ATARI_IO_H
#include "Atari.h"
int AtariIo_LoadFile(const char *path, uint8_t *destination, size_t expected);
int AtariIo_LoadDisk(Atari *a, const char *path);
int AtariIo_Sector(Atari *a, unsigned sector, uint8_t *data, int write);
uint8_t AtariIo_Sio(Atari *a);
void AtariIo_XexContinue(Atari *a);
int Atari_LoadExecutableData(Atari *a, const uint8_t *data, size_t size);
#endif
