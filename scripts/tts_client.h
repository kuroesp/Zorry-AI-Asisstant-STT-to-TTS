#pragma once
#include <Arduino.h>

bool initSpeaker();
void stopSpeaker();
bool streamTTS(String text);
void testSpeaker();
void initI2SSpeaker();
