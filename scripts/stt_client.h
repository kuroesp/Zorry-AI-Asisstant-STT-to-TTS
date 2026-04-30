#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "audio_capture.h"

void writeWavHeader(uint8_t* header, int dataSize);

String sendToSTT(uint8_t* data, size_t length);
