#include "Atari.h"
#include "Pokey.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

static uint8_t keycode(SDL_Keycode k) {
  if (k >= 'a' && k <= 'z')
    return (uint8_t)(k - 'a' + 0x3f);
  if (k >= '0' && k <= '9')
    return (uint8_t)(k - '0' + 0x32);
  switch (k) {
  case SDLK_SPACE:
    return 0x21;
  case SDLK_RETURN:
    return 0x0c;
  case SDLK_BACKSPACE:
    return 0x34;
  case SDLK_ESCAPE:
    return 0x1c;
  default:
    return 0xff;
  }
}
static void input(Atari *a, const SDL_Event *e, int down) {
  SDL_Keycode k = e->key.keysym.sym;
  uint8_t bit = 0;
  if (k == SDLK_UP)
    bit = 1;
  if (k == SDLK_DOWN)
    bit = 2;
  if (k == SDLK_LEFT)
    bit = 4;
  if (k == SDLK_RIGHT)
    bit = 8;
  if (bit) {
    if (down)
      a->joystick &= (uint8_t)~bit;
    else
      a->joystick |= bit;
    return;
  }
  if (k == SDLK_LALT || k == SDLK_RALT) {
    a->trigger = (uint8_t)!down;
    return;
  }
  if (k == SDLK_F2) {
    if (down)
      a->console &= ~4;
    else
      a->console |= 4;
    return;
  }
  if (k == SDLK_F3) {
    if (down)
      a->console &= ~2;
    else
      a->console |= 2;
    return;
  }
  if (k == SDLK_F4) {
    if (down)
      a->console &= ~1;
    else
      a->console |= 1;
    return;
  }
  if (k == SDLK_F5 && down) {
    Atari_Reset(a);
    return;
  }
  if (k == SDLK_F8 && down) {
    a->pokey[14] &= 0x7f;
    a->irq = 1;
    return;
  }
  uint8_t c = keycode(k);
  if (c != 0xff) {
    if (e->key.keysym.mod & KMOD_SHIFT)
      c |= 0x40;
    if (e->key.keysym.mod & KMOD_CTRL)
      c |= 0x80;
    a->keycode = c;
    if (down) {
      a->pokey[15] &= ~4;
      a->pokey[14] &= ~0x40;
      a->irq = 1;
    } else
      a->pokey[15] |= 4;
  }
}
int main(int argc, char **argv) {
  const char *os = "ATARIXL.ROM", *basic = "ATARIBAS.ROM", *disk = NULL;
  int use_basic = 0;
  AtariVideoStandard standard = A800_NTSC;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-b"))
      use_basic = 1;
    else if (!strcmp(argv[i], "--pal"))
      standard = A800_PAL;
    else if (!strcmp(argv[i], "--ntsc"))
      standard = A800_NTSC;
    else if (!strcmp(argv[i], "--os") && i + 1 < argc)
      os = argv[++i];
    else if (!strcmp(argv[i], "--basic") && i + 1 < argc)
      basic = argv[++i];
    else
      disk = argv[i];
  }
  Atari a;
  int r = Atari_Init(&a, os, basic);
  if (r) {
    fprintf(stderr, "a800: cannot load %s (%s)\n", r == -1 ? os : basic,
            r == -1 ? "expected 16384 bytes" : "expected 8192 bytes");
    return 1;
  }
  Atari_SetVideoStandard(&a, standard);
  if (!use_basic)
    a.pia[1] |= 2;
  if (disk && Atari_LoadDisk(&a, disk))
    fprintf(stderr, "a800: warning: cannot mount %s\n", disk);
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window *w = SDL_CreateWindow(
      a.video_standard == A800_PAL ? "a800 - PAL" : "a800 - NTSC",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 768, a.frame_lines * 2,
      SDL_WINDOW_RESIZABLE);
  SDL_Renderer *ren = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
  SDL_Texture *tex =
      SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                        SDL_TEXTUREACCESS_STREAMING, A800_WIDTH, a.frame_lines);
  SDL_AudioSpec want = {0}, have = {0};
  want.freq = 44100;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 1024;
  SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (dev)
    SDL_PauseAudioDevice(dev, 0);
  int run = 1;
  while (run) {
    uint64_t start = SDL_GetPerformanceCounter();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        run = 0;
      else if (e.type == SDL_KEYDOWN && !e.key.repeat)
        input(&a, &e, 1);
      else if (e.type == SDL_KEYUP)
        input(&a, &e, 0);
    }
    Atari_Frame(&a);
    SDL_UpdateTexture(tex, NULL, a.pixels, A800_WIDTH * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    if (dev && SDL_GetQueuedAudioSize(dev) < 8192) {
      int count = (int)(have.freq / a.frame_rate + .5);
      int16_t samples[1024];
      if (count > 1024)
        count = 1024;
      Pokey_Audio(&a, samples, count, have.freq);
      SDL_QueueAudio(dev, samples, (uint32_t)count * sizeof(*samples));
    }
    double elapsed = (double)(SDL_GetPerformanceCounter() - start) /
                     SDL_GetPerformanceFrequency(),
           target = 1.0 / a.frame_rate;
    if (elapsed < target)
      SDL_Delay((uint32_t)((target - elapsed) * 1000.0));
  }
  if (dev)
    SDL_CloseAudioDevice(dev);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(w);
  SDL_Quit();
  Atari_Destroy(&a);
  return 0;
}
