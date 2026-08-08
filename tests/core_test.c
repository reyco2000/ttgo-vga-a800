#include "6502.h"
#include "Atari.h"
#include "Gtia.h"
#include <assert.h>
#include <string.h>

int main(void) {
  Atari a;
  memset(&a, 0, sizeof(a));
  a.pia[1] = 0;
  a.s = 0xfd;
  a.p = 0x24;
  const uint8_t program[] = {0xa9, 0x10, 0x69, 0x22, 0x85, 0x80,
                             0xa2, 3,    0xca, 0xd0, 0xfd};
  memcpy(a.ram + 0x200, program, sizeof(program));
  a.pc = 0x200;
  for (int i = 0; i < 11; i++)
    Cpu_Step(&a);
  assert(a.ram[0x80] == 0x32);
  assert(a.x == 0);
  a.pia[1] = 1;
  a.os[0x3ffc] = 0x34;
  a.os[0x3ffd] = 0x12;
  Cpu_Reset(&a);
  assert(a.pc == 0x1234);
  assert(Gtia_Color(0) == 0xff0c0c0cu);
  return 0;
}
