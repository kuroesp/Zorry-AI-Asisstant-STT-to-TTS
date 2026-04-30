#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>

#define SD_CS 5

extern bool sdReady;
// ===== FILE PATHS =====
#define CORE_MEMORY_FILE    "/core.json"      // identitas dasar
#define CONTEXT_MEMORY_FILE "/context.json"   // konteks percakapan aktif
#define ONDEMAND_MEMORY_FILE "/ondemand.json" // fakta detail

bool initMemory();

// ===== LOAD PER TIPE =====
String loadCoreMemory();
String loadContextMemory();
String loadOnDemandMemory();

// ===== SAVE PER TIPE =====
bool saveCoreMemory(const String& jsonStr);
bool saveContextMemory(const String& jsonStr);
bool saveOnDemandMemory(const String& jsonStr);

// ===== BUILD PROMPT =====
// dipanggil dari ai_client.cpp
String buildSystemPrompt(String userText);  // otomatis pilih memory yang relevan

// ===== UPDATE CONTEXT =====
void updateContext(String userText, String aiReply);
void updateConversationCount();

// ===== MENU =====
String waitForInput(String prompt);
void runMemoryMenu();
void printCurrentMemory();
