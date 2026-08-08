#include "6502.h"
#include "Atari.h"
#include "AtariIo.h"
#include "Gtia.h"
#include "Pia.h"
#include "Pokey.h"
#include <assert.h>
#include <string.h>

int main(void) {
  Atari a;
  memset(&a, 0, sizeof(a));
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
  a.pia_ddr[1] = 1;
  a.os[0x3ffc] = 0x34;
  a.os[0x3ffd] = 0x12;
  Cpu_Reset(&a);
  assert(a.pc == 0x1234);
  assert(Gtia_Color(0) == 0xff0c0c0cu);
  Atari_SetVideoStandard(&a, A800_NTSC);
  assert(a.frame_lines == 262);
  assert(a.cpu_hz == 1789772u);
  assert(Gtia_Read(&a, 0xd014) == 0x0f);
  Atari_SetVideoStandard(&a, A800_PAL);
  assert(a.frame_lines == 312);
  assert(a.cpu_hz == 1773447u);
  assert(Gtia_Read(&a, 0xd014) == 0x01);
  Pia_Reset(&a);
  Pia_Write(&a, 0xd301, 0xff);
  assert(a.pia_ddr[1] == 0xff);
  assert(Atari_PortB(&a) == 0xff);
  Pia_Write(&a, 0xd303, 4);
  Pia_Write(&a, 0xd301, 0xfd);
  assert(Atari_PortB(&a) == 0xfd);
  a.pokey_irqst = 0xff;
  a.pokey_irqen = 0;
  Pokey_Write(&a, 0xd20e, 0x40);
  assert(a.pokey_irqen == 0x40);
  assert(Pokey_Read(&a, 0xd20e) == 0xff);
  a.pokey_skstat = 0xff;
  Pokey_Write(&a, 0xd20f, 3);
  assert(a.pokey_skctl == 3);
  assert(Pokey_Read(&a, 0xd20f) == 0xff);
  memset(a.ram + 0x300, 0, 12);
  a.ram[0x300] = 0x31;
  a.ram[0x301] = 1;
  a.ram[0x302] = 0x52;
  assert(AtariIo_Sio(&a) == 0x8a);
  return 0;
}
