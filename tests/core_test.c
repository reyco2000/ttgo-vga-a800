#include "6502.h"
#include "Antic.h"
#include "Atari.h"
#include "AtariIo.h"
#include "Gtia.h"
#include "Pia.h"
#include "Pokey.h"
#ifndef NO_SAVE_STATE
#include "State.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_rom_xex(void) {
  static const uint8_t header[] = {0xff, 0xff, 0x00, 0x00, 0x0f, 0x00, 'A', '8',
                                   'O',  'S',  'X',  'E',  'X',  0,    1,   0,
                                   0,    0,    0,    0,    0,    0};
  const struct {
    uint8_t machine;
    uint16_t address;
  } cases[] = {{A800_MACHINE_800, 0xd800},
               {A800_MACHINE_XL, 0x5000},
               {A800_MACHINE_5200, 0xf800}};
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    FILE *file = fopen("core_test_os.xex", "wb");
    assert(file);
    uint8_t image[sizeof(header)];
    memcpy(image, header, sizeof(image));
    image[15] = cases[i].machine;
    assert(fwrite(image, 1, sizeof(image), file) == sizeof(image));
    uint8_t segment[5] = {
        (uint8_t)cases[i].address, (uint8_t)(cases[i].address >> 8),
        (uint8_t)cases[i].address, (uint8_t)(cases[i].address >> 8), 0x5a};
    assert(fwrite(segment, 1, sizeof(segment), file) == sizeof(segment));
    assert(!fclose(file));
    Atari a = {0};
    assert(!AtariIo_LoadOs(&a, "core_test_os.xex"));
    assert(a.machine_type == cases[i].machine);
    if (a.machine_type == A800_MACHINE_XL) {
      a.pia_ddr[1] = 0xff, a.pia[1] = 0x7f;
      Atari_InvalidateMemoryMap(&a);
    }
    assert(Atari_Read(&a, cases[i].address) == 0x5a);
  }
  assert(!remove("core_test_os.xex"));
}

int main(void) {
  test_rom_xex();
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
  a.pc = 0x400;
  a.x = 1;
  a.ram[0x400] = 0xbd;
  a.ram[0x401] = 0xff;
  a.ram[0x402] = 0;
  a.ram[0x100] = 0x55;
  assert(Cpu_Step(&a) == 5);
  assert(a.a == 0x55);
  a.pc = 0x4fd;
  a.p = 0x20;
  a.ram[0x4fd] = 0xd0;
  a.ram[0x4fe] = 2;
  assert(Cpu_Step(&a) == 4);
  assert(a.pc == 0x501);
#ifdef A800_UNDOCUMENTED_OPCODES
  a.pc = 0x500;
  a.ram[0x500] = 0xa7;
  a.ram[0x501] = 0x80;
  a.ram[0x80] = 0x5a;
  assert(Cpu_Step(&a) == 3);
  assert(a.a == 0x5a && a.x == 0x5a);
  a.pc = 0x502;
  a.ram[0x502] = 0x87;
  a.ram[0x503] = 0x81;
  a.a = 0xf3;
  a.x = 0x3f;
  assert(Cpu_Step(&a) == 3);
  assert(a.ram[0x81] == 0x33);
  a.pc = 0x504;
  a.ram[0x504] = 0x07;
  a.ram[0x505] = 0x82;
  a.ram[0x82] = 0x81;
  a.a = 1;
  assert(Cpu_Step(&a) == 5);
  assert(a.ram[0x82] == 2 && a.a == 3 && (a.p & 1));
  a.pc = 0x506;
  a.ram[0x506] = 0xc7;
  a.ram[0x507] = 0x83;
  a.ram[0x83] = 0x11;
  a.a = 0x10;
  assert(Cpu_Step(&a) == 5);
  assert(a.ram[0x83] == 0x10 && (a.p & 3) == 3);
  a.pc = 0x508;
  a.ram[0x508] = 0xe7;
  a.ram[0x509] = 0x84;
  a.ram[0x84] = 1;
  a.a = 5;
  a.p = 0x21;
  assert(Cpu_Step(&a) == 5);
  assert(a.ram[0x84] == 2 && a.a == 3);
  a.pc = 0x50a;
  a.x = 1;
  a.ram[0x50a] = 0x1c;
  a.ram[0x50b] = 0xff;
  a.ram[0x50c] = 0;
  assert(Cpu_Step(&a) == 5);
  assert(a.pc == 0x50d);
#endif
  a.pia[1] = 1;
  a.pia_ddr[1] = 1;
  a.machine_type = A800_MACHINE_XL;
  a.os_pages[31] = 0x80;
  Atari_InvalidateMemoryMap(&a);
  a.os[0x3ffc] = 0x34;
  a.os[0x3ffd] = 0x12;
  Cpu_Reset(&a);
  assert(a.pc == 0x1234);
#ifndef NO_NTSC
  Atari_SetVideoStandard(&a, A800_NTSC);
  assert(a.frame_lines == 262);
  assert(a.cpu_hz == 1789772u);
  assert(a.palette[0] == 0xff040404u);
  assert(a.palette[0x34] == 0xff5e2800u);
  assert(Gtia_Read(&a, 0xd014) == 0x0f);
#endif
#ifndef NO_PAL
  Atari_SetVideoStandard(&a, A800_PAL);
  assert(a.frame_lines == 312);
  assert(a.cpu_hz == 1773447u);
  assert(a.palette[0x34] == 0xff681b23u);
  assert(Gtia_Read(&a, 0xd014) == 0x01);
#endif
  memset(a.trigger, 1, sizeof(a.trigger));
  memset(a.trigger_latch, 1, sizeof(a.trigger_latch));
  Gtia_Write(&a, 0xd01d, 4);
  a.trigger[0] = 0;
  Gtia_Frame(&a);
  a.trigger[0] = 1;
  assert(Gtia_Read(&a, 0xd010) == 0);
  Gtia_Write(&a, 0xd01d, 0);
  assert(Gtia_Read(&a, 0xd010) == 1);
  Pia_Reset(&a);
  Pia_Write(&a, 0xd301, 0xff);
  assert(a.pia_ddr[1] == 0xff);
  assert(Atari_PortB(&a) == 0xff);
  Pia_Write(&a, 0xd303, 4);
  Pia_Write(&a, 0xd301, 0xfd);
  assert(Atari_PortB(&a) == 0xfd);
  a.ram[0xa000] = 0x5a;
  Atari_Write(&a, 0xa000, 0xa5);
#ifdef NO_BASIC
  assert(Atari_Read(&a, 0xa000) == 0);
#else
  assert(Atari_Read(&a, 0xa000) == a.basic[0]);
#endif
  assert(a.ram[0xa000] == 0x5a);
  Pia_Write(&a, 0xd301, 0xff);
  assert(Atari_Read(&a, 0xa000) == 0x5a);
  a.pokey_irqst = 0xff;
  a.pokey_irqen = 0;
  Pokey_Write(&a, 0xd20e, 0x40);
  assert(a.pokey_irqen == 0x40);
  assert(Pokey_Read(&a, 0xd20e) == 0xff);
  a.pokey_skstat = 0xff;
  Pokey_Write(&a, 0xd20f, 3);
  assert(a.pokey_skctl == 3);
  assert(Pokey_Read(&a, 0xd20f) == 0xff);
  Pokey_Write(&a, 0xd20f, 0);
  a.cycles += 100;
  (void)Pokey_Read(&a, 0xd20a);
  assert(a.rng == 1 && a.pokey_random_poly9 == 1);
  Pokey_Write(&a, 0xd20f, 3);
  a.cycles++;
  (void)Pokey_Read(&a, 0xd20a);
  assert(a.rng != 1 && a.pokey_random_poly9 != 1);
  a.cpu_hz = 1789772;
  Pokey_Write(&a, 0xd200, 10);
  Pokey_Write(&a, 0xd201, 0xaf);
  int16_t audio[512];
  Pokey_Audio(&a, NULL, 0, 44100);
  Pokey_Advance(&a, 21000);
  Pokey_Audio(&a, audio, 512, 44100);
  int changes = 0;
  for (int i = 1; i < 512; i++)
    changes += audio[i] != audio[i - 1];
  assert(changes > 10);
  Atari loud;
  memset(&loud, 0, sizeof(loud));
  loud.cpu_hz = 1789772;
  loud.pokey_skctl = 3;
  for (int ch = 0; ch < 4; ch++)
    Pokey_Write(&loud, (uint16_t)(0xd201 + ch * 2), 0x1f);
  Pokey_Audio(&loud, NULL, 0, 44100);
  Pokey_Advance(&loud, 21000);
  Pokey_Audio(&loud, audio, 512, 44100);
  int peak = 0;
  for (int i = 0; i < 512; i++) {
    int sample = audio[i] < 0 ? -audio[i] : audio[i];
    if (sample > peak)
      peak = sample;
  }
  assert(peak > 18000 && peak <= 20000);
  Pokey_Write(&a, 0xd200, 0);
  Pokey_Write(&a, 0xd201, 0xa8);
  int16_t ultrasonic[4096];
  Pokey_Advance(&a, 166000);
  Pokey_Audio(&a, ultrasonic, 4096, 44100);
  for (int i = 0; i < 4096; i++)
    assert(ultrasonic[i] <= 20000 && ultrasonic[i] >= -20000);
  a.audio_phase[0] = 0;
  a.audio_output[0] = 1;
  Pokey_Write(&a, 0xd209, 0);
  assert(a.audio_phase[0] > 0);
  assert(!a.audio_output[0] && !a.audio_output[1] && a.audio_output[2] &&
         a.audio_output[3]);
  Atari sweep;
  memset(&sweep, 0, sizeof(sweep));
  sweep.cpu_hz = 100;
  sweep.pokey_skctl = 3;
  Pokey_Write(&sweep, 0xd200, 9);
  Pokey_Write(&sweep, 0xd201, 0xaf);
  Pokey_Write(&sweep, 0xd208, 0x40);
  Pokey_Audio(&sweep, NULL, 0, 100);
  Pokey_Advance(&sweep, 5);
  assert(!sweep.audio_output[0]);
  sweep.cycles = 5;
  Pokey_Write(&sweep, 0xd200, 0);
  Pokey_Advance(&sweep, 4);
  assert(!sweep.audio_output[0]);
  Pokey_Advance(&sweep, 4);
  assert(sweep.audio_output[0]);
  Atari timed;
  a.pokey_irqst = 0xff;
  Pokey_Write(&a, 0xd200, 0);
  Pokey_Write(&a, 0xd208, 0x40);
  Pokey_Write(&a, 0xd20e, 1);
  Pokey_Write(&a, 0xd209, 0);
  Pokey_Advance(&a, 4);
  assert(!(a.pokey_irqst & 1));
  a.pokey_irqst = 0xff;
  Pokey_Write(&a, 0xd209, 0);
  Pokey_Advance(&a, 3);
  Pokey_Write(&a, 0xd200, 20);
  Pokey_Advance(&a, 1);
  assert(!(a.pokey_irqst & 1));
  a.pokey_irqst = 0xff;
  Pokey_Write(&a, 0xd200, 0);
  Pokey_Write(&a, 0xd202, 0);
  Pokey_Write(&a, 0xd208, 0x50);
  Pokey_Write(&a, 0xd20e, 2);
  Pokey_Write(&a, 0xd209, 0);
  Pokey_Advance(&a, 7);
  assert(!(a.pokey_irqst & 2));
  a.pc = 0xe474;
  a.option_override = 1;
  a.ram[0x0c] = 0x34;
  a.ram[0x0d] = 0x12;
  assert(Cpu_Step(&a) == 3);
  assert(a.pc == 0x1234);
  const uint8_t xex[] = {0xff, 0xff, 0x00, 0x20, 0x00, 0x20, 0x60};
  a.antic[9] = a.ram[0x2f4] = 0;
  assert(Atari_LoadExecutableData(&a, xex, sizeof(xex)) == 0);
  assert(a.antic[9] == 0xe0 && a.ram[0x2f4] == 0xe0);
  assert(a.ram[0x2000] == 0x60 && a.pc == 0x2000);
  memset(a.ram + 0x300, 0, 12);
  a.ram[0x300] = 0x31;
  a.ram[0x301] = 1;
  a.ram[0x302] = 0x52;
  assert(AtariIo_Sio(&a) == 0x8a);
  memset(&a, 0, sizeof(a));
  Atari_SetVideoStandard(&a, A800_NTSC);
  a.pia[1] = 0;
  a.pia_ddr[1] = 0xff;
  Atari_InvalidateMemoryMap(&a);
  a.antic[0] = 0x22;
  a.antic[2] = 0x00;
  a.antic[3] = 0x02;
  a.antic[9] = 0xe0;
  a.line_antic[8][9] = 0xe0;
  a.gtia[23] = 0x4a;
  a.line_colors[8][1] = 0x4a;
  a.ram[0x200] = 0x47;
  a.ram[0x201] = 0x00;
  a.ram[0x202] = 0x03;
  a.ram[0x203] = 0x41;
  a.ram[0x204] = 0x00;
  a.ram[0x205] = 0x02;
  a.ram[0x300] = 0x40;
  a.ram[0xe000] = 0x80;
  a.antic[9] = 0;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x4a);
  a.ram[0x200] = 0x57;
  a.ram[0x300] = 0;
  a.ram[0x302] = 0x40;
  a.line_antic[8][4] = 2;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0);
  assert(a.pixels[8 * A800_WIDTH + 36] == 0x4a);
  a.ram[0x200] = 0x66;
  a.line_antic[8][4] = 0;
  a.line_antic[8][5] = 1;
  a.ram[0x300] = 0;
  a.ram[0xe001] = 0;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0);
  a.ram[0x200] = 0x42;
  a.line_antic[8][1] = 1;
  a.line_antic[8][5] = 0;
  a.ram[0x300] = 0x80;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0);
  a.line_antic[8][1] = 3;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x0a);
  memset(&a, 0, sizeof(a));
  Atari_SetVideoStandard(&a, A800_NTSC);
  a.antic[0] = 0x22;
  a.antic[2] = 0;
  a.antic[3] = 2;
  a.antic[5] = 3;
  a.ram[0x200] = 0x70;
  Antic_BeginFrame(&a);
  Antic_BeginLine(&a, 8);
  assert(a.display_rows == 8 && !a.display_vscroll_active);
  memset(&a, 0, sizeof(a));
  Atari_SetVideoStandard(&a, A800_NTSC);
  a.antic[0] = 0x22;
  a.antic[2] = 0;
  a.antic[3] = 2;
  a.antic[5] = 3;
  a.ram[0x200] = 0x62;
  a.ram[0x201] = 0;
  a.ram[0x202] = 3;
  a.ram[0x203] = 0x42;
  a.ram[0x204] = 0;
  a.ram[0x205] = 4;
  Antic_BeginFrame(&a);
  Antic_BeginLine(&a, 8);
  assert(a.display_row == 3 && a.display_rows == 5);
  assert(Antic_DmaCycles(&a) == 83);
  for (int line = 8; line < 13; line++) {
    if (line != 8)
      Antic_BeginLine(&a, line);
    Antic_RenderLine(&a, line);
  }
  Antic_BeginLine(&a, 13);
  assert(a.display_row == 0 && a.display_rows == 4);
  memset(a.ram, 0, sizeof(a.ram));
  a.antic[0] = 0x22;
  a.antic[2] = 0;
  a.antic[3] = 2;
  a.antic[14] = 0x80;
  a.ram[0x200] = 0xf0;
  a.ram[0x201] = 0x41;
  a.ram[0x202] = 0;
  a.ram[0x203] = 2;
  Antic_BeginFrame(&a);
  for (int line = 8; line < 16; line++) {
    Antic_BeginLine(&a, line);
    Antic_RenderLine(&a, line);
  }
  assert(a.nmi);
  assert(a.antic[15] & 0x80);
  memset(&a, 0, sizeof(a));
  Atari_SetVideoStandard(&a, A800_NTSC);
  a.antic[0] = 2;
  a.antic[2] = 0;
  a.antic[3] = 2;
  a.ram[0x200] = 0x4f;
  a.ram[0x201] = 0;
  a.ram[0x202] = 3;
  a.ram[0x300] = 0xff;
  a.line_colors[8][4] = 0x20;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x20);
  assert(a.display_list == 0x200);
  a.antic[0] = 0x22;
  a.pia_ddr[1] = 0xff;
  a.pia[1] = 0;
  Atari_InvalidateMemoryMap(&a);
  a.ram[0x200] = 0x4f;
  a.ram[0x201] = 0xf0;
  a.ram[0x202] = 0xff;
  a.ram[0x203] = 0x41;
  a.ram[0x204] = 0;
  a.ram[0x205] = 2;
  a.ram[0xf000] = 0x80;
  a.line_colors[8][1] = 0x0a;
  a.line_colors[8][2] = 0x40;
  Antic_Frame(&a);
  assert(a.screen_memory == 0xf018);
  assert(a.pixels[8 * A800_WIDTH + 160] == 0x4a);
  memset(&a, 0, sizeof(a));
  Atari_SetVideoStandard(&a, A800_NTSC);
  a.pia[1] = 0;
  a.pia_ddr[1] = 0xff;
  Atari_InvalidateMemoryMap(&a);
  a.antic[0] = 0x1a;
  a.antic[7] = 0x30;
  a.line_gtia[40][0] = 0x80;
  a.line_gtia[40][18] = 0x4a;
  a.line_gtia[40][29] = 2;
  a.scanline = 40;
  a.ram[0x3428] = 0x80;
  Antic_CaptureLine(&a);
  a.ram[0x3428] = 0;
  Antic_Frame(&a);
  assert(a.pixels[40 * A800_WIDTH + 192] == 0x4a);
  a.gtia[28] = 1;
  a.pm_latch[0] = 0x40;
  a.ram[0x3428] = 0x80;
  a.scanline = 40;
  Antic_CaptureLine(&a);
  assert(a.line_pmg[40][0] == 0x40);
  a.ram[0x3429] = 0x80;
  a.scanline = 41;
  Antic_CaptureLine(&a);
  assert(a.line_pmg[41][0] == 0x80);
  memset(&a, 0, sizeof(a));
  Atari_SetVideoStandard(&a, A800_NTSC);
  a.pia[1] = 0;
  a.pia_ddr[1] = 0xff;
  Atari_InvalidateMemoryMap(&a);
  a.antic[0] = 0x3e;
  a.antic[2] = 0;
  a.antic[3] = 2;
  a.antic[7] = 0x30;
  a.ram[0x200] = 0x49;
  a.ram[0x201] = 0;
  a.ram[0x202] = 3;
  a.ram[0x203] = 0x41;
  a.ram[0x204] = 0;
  a.ram[0x205] = 2;
  a.ram[0x300] = 0x80;
  for (int line = 8; line < 12; line++)
    a.line_colors[line][0] = 0x4a;
  a.line_gtia[8][0] = a.line_gtia[8][1] = a.line_gtia[8][4] = 48;
  a.line_gtia[8][18] = 0x2a;
  a.line_gtia[8][19] = 0x4a;
  a.line_gtia[8][29] = 3;
  a.line_pmg[8][4] = 3;
  a.line_pmg[8][0] = a.line_pmg[8][1] = 0x80;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x6a);
  assert(a.pixels[8 * A800_WIDTH + 35] == 0x6a);
  assert(a.pixels[8 * A800_WIDTH + 36] == 0);
  a.line_gtia[8][27] = 1;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x2a);
  a.line_gtia[8][27] = 4;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x4a);
  assert((Gtia_Read(&a, 0xd000) & 1) != 0);
  assert((Gtia_Read(&a, 0xd004) & 1) != 0);
  assert((Gtia_Read(&a, 0xd008) & 3) == 3);
  assert((Gtia_Read(&a, 0xd00c) & 2) != 0);
  assert((Gtia_Read(&a, 0xd00d) & 1) != 0);
  Gtia_Write(&a, 0xd01e, 0);
  for (int i = 0; i < 16; i++)
    assert(a.gtia_collisions[i] == 0);
  memset(&a, 0, sizeof(a));
  Atari_SetVideoStandard(&a, A800_NTSC);
  a.pia[1] = 0;
  a.pia_ddr[1] = 0xff;
  Atari_InvalidateMemoryMap(&a);
  a.antic[0] = 0x22;
  a.antic[2] = 0;
  a.antic[3] = 2;
  a.ram[0x200] = 0x4f;
  a.ram[0x201] = 0;
  a.ram[0x202] = 3;
  a.ram[0x203] = 0x41;
  a.ram[0x204] = 0;
  a.ram[0x205] = 2;
  a.ram[0x300] = 0x1f;
  a.line_colors[8][4] = 0x40;
  a.line_gtia[8][27] = 0x40;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x41);
  assert(a.pixels[8 * A800_WIDTH + 36] == 0x4f);
  a.ram[0x300] = 0x07;
  a.line_gtia[8][27] = 0x80;
  a.line_gtia[8][18] = 0x2a;
  a.line_colors[8][3] = 0x8a;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x2a);
  assert(a.pixels[8 * A800_WIDTH + 36] == 0x8a);
  a.ram[0x300] = 0x1f;
  a.line_gtia[8][27] = 0xc0;
  a.line_colors[8][4] = 0x06;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x16);
  assert(a.pixels[8 * A800_WIDTH + 36] == 0xf6);
  a.ram[0x200] = 0x42;
  a.ram[0x300] = 0;
  a.ram[0xe000] = 0x1f;
  a.line_antic[8][9] = 0xe0;
  Antic_Frame(&a);
  assert(a.pixels[8 * A800_WIDTH + 32] == 0x16);
  assert(a.pixels[8 * A800_WIDTH + 36] == 0xf6);
  a.console = 7;
  a.option_override = 2;
  assert(Gtia_Read(&a, 0xd01f) == 3);
  assert(Gtia_Read(&a, 0xd01f) == 3);
  assert(Gtia_Read(&a, 0xd01f) == 7);
  memset(&a, 0, sizeof(a));
  Atari_SetVideoStandard(&a, A800_NTSC);
  a.line_gtia[40][0] = 0x80;
  a.line_gtia[40][13] = 0x80;
  a.line_gtia[40][18] = 0x2a;
  a.line_gtia[40][29] = 2;
  a.line_pmg[40][0] = 0x80;
  Antic_Frame(&a);
  assert(a.pixels[40 * A800_WIDTH + 192] == 0x2a);
  a.line_gtia[40][29] = 0;
  Antic_Frame(&a);
  assert(a.pixels[40 * A800_WIDTH + 192] == 0x2a);
  memset(&timed, 0, sizeof(timed));
  Atari_SetVideoStandard(&timed, A800_NTSC);
  timed.pia_ddr[1] = 0xff;
  timed.pia[1] = 0;
  Atari_InvalidateMemoryMap(&timed);
  timed.pc = 0x200;
  timed.ram[0x200] = 0x4c;
  timed.ram[0x201] = 0;
  timed.ram[0x202] = 2;
  Atari_Frame(&timed);
  assert(timed.cycles == (uint64_t)timed.frame_lines * 114u);
  assert(timed.cpu_cycle_debt <= 2);
#ifndef NO_SAVE_STATE
  timed.ram[0x1234] = 0x56;
  timed.audio_len = 7;
  timed.disk_size = 3;
  timed.disk = (uint8_t *)malloc(timed.disk_size);
  timed.xex_size = 2;
  timed.xex_data = (uint8_t *)malloc(timed.xex_size);
  assert(timed.disk && timed.xex_data);
  memcpy(timed.disk, "ATR", timed.disk_size);
  timed.xex_data[0] = 0xff;
  timed.xex_data[1] = 0xff;
  assert(State_Save(&timed, "core_test.state") == 0);
  timed.ram[0x1234] = 0;
  timed.disk[0] = 0;
  timed.xex_data[0] = 0;
  assert(State_Load(&timed, "core_test.state") == 0);
  assert(timed.ram[0x1234] == 0x56);
  assert(timed.palette == Gtia_Palette(timed.video_standard));
  assert(timed.audio_len == 0);
  assert(timed.disk_size == 3 && !memcmp(timed.disk, "ATR", 3));
  assert(timed.xex_size == 2 && timed.xex_data[0] == 0xff &&
         timed.xex_data[1] == 0xff);
  assert(remove("core_test.state") == 0);
#endif
  Atari_Destroy(&timed);
  return 0;
}
