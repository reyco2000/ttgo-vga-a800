/* 8-bit DAC1 output from a single-producer ring. The ISR clock is the master
 * clock: the sketch emulates frames only while the ring is below target. */
#include <Arduino.h>
#include "driver/dac.h"
#include "hal.h"
#include "../../config.h"

enum { RING_SIZE = 2048, RING_MASK = RING_SIZE - 1 };

static uint8_t ring[RING_SIZE];
static volatile uint32_t ring_write, ring_read, underruns;
static hw_timer_t *timer;

static void IRAM_ATTR audio_isr() {
  uint32_t r = ring_read;
  if (r == ring_write) {
    underruns++;
    return;
  }
  dac_output_voltage(DAC_CHANNEL_1, ring[r & RING_MASK]);
  ring_read = r + 1;
}

void Audio_Init(void) {
  memset(ring, 128, sizeof(ring));
  dac_output_enable(DAC_CHANNEL_1);
  dac_output_voltage(DAC_CHANNEL_1, 128);
  timer = timerBegin(0, 80, true); /* 1 MHz ticks */
  timerAttachInterrupt(timer, audio_isr, true);
  timerAlarmWrite(timer, AUDIO_PERIOD_US, true);
  timerAlarmEnable(timer);
}

int Audio_Fill(void) { return (int)(ring_write - ring_read); }

/* POKEY output peaks at about +/-20000. */
void Audio_Push(const int16_t *samples, int count) {
  uint32_t w = ring_write;
  for (int i = 0; i < count && (int)(w - ring_read) < RING_SIZE; i++) {
    int v = 128 + samples[i] / 160;
    ring[w++ & RING_MASK] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  ring_write = w;
}

uint32_t Audio_Underruns(void) { return underruns; }
