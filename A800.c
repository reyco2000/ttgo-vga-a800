/* SDL2 frontend for the compact a800 core. */
#include "Atari.h"
#include "Pokey.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

static uint8_t keycode(SDL_Keycode k, SDL_Keymod mod) {
  static const uint8_t letters[26] = {0x3f, 0x15, 0x12, 0x3a, 0x2a, 0x38, 0x3d,
                                      0x39, 0x0d, 0x01, 0x05, 0x00, 0x25, 0x23,
                                      0x08, 0x0a, 0x2f, 0x28, 0x3e, 0x2d, 0x0b,
                                      0x10, 0x2e, 0x16, 0x2b, 0x17};
  static const uint8_t digits[10] = {0x32, 0x1f, 0x1e, 0x1a, 0x18,
                                     0x1d, 0x1b, 0x33, 0x35, 0x30};
  int shift = (mod & KMOD_SHIFT) != 0;
  if (k >= 'a' && k <= 'z')
    return (uint8_t)(letters[k - 'a'] | (shift ? 0x40 : 0));
  if (k >= '0' && k <= '9') {
    static const uint8_t shifted[10] = {0x72, 0x5f, 0x75, 0x5a, 0x58,
                                        0x5d, 0x47, 0x5b, 0x07, 0x70};
    return shift ? shifted[k - '0'] : digits[k - '0'];
  }
  switch (k) {
  case SDLK_SPACE:
    return 0x21;
  case SDLK_RETURN:
    return 0x0c;
  case SDLK_BACKSPACE:
    return 0x34;
  case SDLK_ESCAPE:
    return 0x1c;
  case SDLK_TAB:
    return shift ? 0x6c : 0x2c;
  case SDLK_MINUS:
    return shift ? 0x4e : 0x0e;
  case SDLK_EQUALS:
    return shift ? 0x06 : 0x0f;
  case SDLK_LEFTBRACKET:
    return 0x60;
  case SDLK_RIGHTBRACKET:
    return 0x62;
  case SDLK_SEMICOLON:
    return shift ? 0x42 : 0x02;
  case SDLK_QUOTE:
    return shift ? 0x5e : 0x73;
  case SDLK_COMMA:
    return shift ? 0x36 : 0x20;
  case SDLK_PERIOD:
    return shift ? 0x37 : 0x22;
  case SDLK_SLASH:
    return shift ? 0x66 : 0x26;
  case SDLK_BACKSLASH:
    return shift ? 0x4f : 0x46;
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
    a->pokey_irqst &= 0x7f;
    if (a->pokey_irqen & 0x80)
      a->irq = 1;
    return;
  }
  uint8_t c = keycode(k, e->key.keysym.mod);
  if (c != 0xff) {
    if (e->key.keysym.mod & KMOD_CTRL)
      c |= 0x80;
    a->keycode = c;
    if (down) {
      a->pokey_skstat &= ~4;
      a->pokey_irqst &= ~0x40;
      if (a->pokey_irqen & 0x40)
        a->irq = 1;
    } else
      a->pokey_skstat |= 4;
  }
}
int main(int argc, char **argv) {
  const char *os = "ATARIXL.ROM", *basic = "ATARIBAS.ROM", *media = NULL,
             *xex = NULL;
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
      media = argv[i];
  }
  if (media) {
    size_t n = strlen(media);
    if (n >= 4 && !SDL_strcasecmp(media + n - 4, ".xex"))
      xex = media;
  }
  Atari a;
  int r = Atari_Init(&a, os, basic);
  if (r) {
    fprintf(stderr, "a800: cannot load %s (%s)\n", r == -1 ? os : basic,
            r == -1 ? "expected 16384 bytes" : "expected 8192 bytes");
    return 1;
  }
  Atari_SetVideoStandard(&a, standard);
  if (!use_basic && !xex)
    a.console &= (uint8_t)~4;
  if (media && !xex && Atari_LoadDisk(&a, media))
    fprintf(stderr, "a800: warning: cannot mount %s\n", media);
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
  int run = 1, boot_frames = 0;
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
    if (xex && ++boot_frames == 76) {
      if (Atari_LoadExecutable(&a, xex))
        fprintf(stderr, "a800: cannot load executable %s\n", xex);
      else
        Atari_Frame(&a);
      xex = NULL;
    }
    if (!xex) {
      SDL_UpdateTexture(tex, NULL, a.pixels, A800_WIDTH * 4);
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, tex, NULL, NULL);
      SDL_RenderPresent(ren);
    }
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
