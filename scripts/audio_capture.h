#pragma once
#include <Arduino.h>
#include "driver/i2s.h"

#define I2S_PORT    I2S_NUM_0

// ── Pin share BCLK + WS untuk mic dan speaker ──
#define BCLK        14   // share: mic BCLK  = speaker BCLK
#define WS          15  // share: mic WS    = speaker WS
#define DIN         32   // mic saja  (data in)
#define SPK_DOUT    22   // speaker saja (data out)

#define SAMPLE_RATE  16000
#define BUFFER_SIZE  512

extern bool hasValidSpeech;

void initI2S();

int   getEnergy(int16_t* data, int len);
float getVariance(int16_t* data, int len);

typedef void (*AudioCallback)(int16_t* data, size_t length);
void recordAudioStream(AudioCallback callback, int duration_seconds);
