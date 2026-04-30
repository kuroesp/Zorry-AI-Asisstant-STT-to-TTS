#include <Arduino.h>
#include <WiFi.h>
#include "audio_capture.h"
#include "stt_client.h"
#include "ai_client.h"
#include "memory.h"
#include <SD.h>
#include <SPI.h>
#include <tts_client.h>

// SETIAP FUNCTION DALAM MAINCPP
// 1. ensurewifi = fungsinya adalah untuk connect dengan wifi karena selesai TTS wifi dimatikan untuk mereset stack
// 2. processaudio = fungsinya untuk melakukan callback, ketika ada data mic masuk di proses ke STT secara bertahap chunk demi chunk lalu mereset buffer kembali baru menjadikan 1 sekmen dan dikirim ke STT (chunked streaming)
// 3. setup = ada 4 initi2s (menghidupkan mic), initi2s speaker ( menghidupkan speaker), initmemory(memuat sd card, dan membuat file json jika belum ada untuk memasukan data user), wifi begin(conecct ke wifi)
// 4. loop = cek serial command apakah ada code -memori/-mem -> reset buffer dan VAD aktif ketika ada suara yang terdeteksi oleh VAD -> melakukan recordaudiostream 8 detik max, VAD akan otomatis diam ketika user berhenti ngomong -> kirim sisa audio terakhir dan menjadikannya 1 segment atau 1 kalimat utuh -> lalu jalankan fucntion sendtoai dan mulai load memori khususnya core memory, kirim ke OpenRouter ai dan mendapatkan reply dari ai -> reply ai dikirim ke server.py python untuk menerima PCM audio lalu dikonversi ke mp3, putar lewat i2sSpeaker -> wifi yang awallnya disconnect tadi di connectkan kembali untuk mendeteksi suara lagi, dan cek heap memori jika aman lanjut jika tidak rebooting. 
// ===== WIFI =====
#define WIFI_SSID "" // your wifi
#define WIFI_PASS "" // your pass

// ===== BUFFER =====
#define MAX_AUDIO_SIZE 64000

uint8_t audioData[MAX_AUDIO_SIZE];
size_t  audioIndex = 0;
String  fullText   = "";

extern bool hasValidSpeech;
extern bool speechEnded;

// ================= WIFI RECONNECT =================
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("🔄 WiFi reconnecting...");
  WiFi.disconnect(false);
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi reconnected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n❌ WiFi gagal reconnect, restart...");
    ESP.restart();
  }
}

// ================= AUDIO CALLBACK =================
void processAudio(int16_t* data, size_t length) {
  size_t bytes = length * 2;

  if (audioIndex + bytes <= MAX_AUDIO_SIZE) {
    memcpy(audioData + audioIndex, data, bytes);
    audioIndex += bytes;
  }

  if (audioIndex >= MAX_AUDIO_SIZE) {
    if (hasValidSpeech) {
      Serial.println("🚀 Kirim chunk...");
      ensureWiFi();
      String result = sendToSTT(audioData, audioIndex);
      if (result.length() > 0) {
        fullText += " " + result;
        Serial.println("Chunk result: " + result);
      }
    }
    audioIndex = 0;
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== BOOTING SYSTEM ===");

  // ===== 1. INIT I2S MIC =====
  Serial.println("🎙️ Init I2S MIC...");
  initI2S();
  delay(200);

  // ===== 2. INIT I2S SPEAKER =====
  Serial.println("🔊 Init I2S Speaker...");
  initI2SSpeaker();
  delay(200);

  // ===== 3. TEST SPEAKER =====
  // testSpeaker();  // uncomment kalau mau test nada

  // ===== 4. INIT MEMORY (SD Card) =====
  bool sdOk = initMemory();
  if (!sdOk) {
    Serial.println("⚠️ SD tidak siap, AI jalan tanpa memori");
  } else {
    Serial.println("✅ Memory siap");
  }

  // ===== 5. WIFI =====
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected!");
  Serial.println(WiFi.localIP());

  Serial.println("\n✅ SYSTEM READY");
  Serial.println("=== LISTEN MODE ===\n");
}

// ================= LOOP =================
void loop() {

  // ===== SERIAL COMMAND =====
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "-memori") {
      if (sdReady) {
        runMemoryMenu();
      } else {
        Serial.println("❌ SD tidak siap, menu memori tidak bisa dibuka");
      }
      return;
    }

    if (cmd == "-mem") {
      if (sdReady) {
        printCurrentMemory();
      } else {
        Serial.println("❌ SD tidak siap");
      }
      return;
    }
  }

  // ===== RESET AUDIO STATE =====
  audioIndex     = 0;
  fullText       = "";
  hasValidSpeech = false;
  speechEnded    = false;

  // ===== RECORD =====
  Serial.println("🎧 Listening...");
  recordAudioStream(processAudio, 8);

  // ===== LAST CHUNK =====
  if (audioIndex > 0 && hasValidSpeech) {
    Serial.println("📦 Kirim chunk terakhir...");
    ensureWiFi();
    String result = sendToSTT(audioData, audioIndex);
    if (result.length() > 0) {
      fullText += " " + result;
    }
  }

  fullText.trim();

  // ===== AI PROCESS =====
  if (fullText.length() > 0) {
    Serial.println("\n=============================");
    Serial.println("USER: " + fullText);

    ensureWiFi();
    String aiReply = sendToAI(fullText);

    Serial.println("AI  : " + aiReply);
    Serial.println("=============================\n");

    if (aiReply.length() > 0 && aiReply.indexOf("ERROR") < 0) {

      // ===== SPEAK =====
      Serial.println("🔊 Speaking...");
      streamTTS(aiReply);
      delay(500);

      // Reconnect WiFi setelah TTS selesai
      WiFi.disconnect(false);
      delay(300);
      ensureWiFi();

      Serial.print("🧠 Free heap: ");
      Serial.println(ESP.getFreeHeap());

      if (ESP.getFreeHeap() < 20000) {
        Serial.println("⚠️ Heap kritis! Restart...");
        delay(500);
        ESP.restart();
      }
    }
  }
}
