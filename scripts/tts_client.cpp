#include "tts_client.h"
#include <WiFiClient.h>
#include "driver/i2s.h"

#define TTS_SERVER_IP   "192.168.100.12"
#define TTS_SERVER_PORT 5050

#define I2S_SPK_PORT I2S_NUM_1
#define SPK_BCLK 26
#define SPK_WS   25
#define SPK_DOUT 22

// untuk tes speaker ketika setup
void testSpeaker() {
  Serial.println("🔊 Test tone...");

  const int SAMPLES = 48000; // 1 detik
  int16_t* buf = (int16_t*)malloc(SAMPLES * sizeof(int16_t));
  if (!buf) { Serial.println("❌ malloc gagal"); return; }

  // generate sine 440Hz
  for (int i = 0; i < SAMPLES; i++) {
    buf[i] = (int16_t)(10000.0f * sin(2.0f * PI * 440.0f * i / 16000.0f));
  }

  size_t bytesWritten = 0;
  esp_err_t err = i2s_write(I2S_SPK_PORT, buf, SAMPLES * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  Serial.printf("i2s_write: %s | written: %d\n", esp_err_to_name(err), (int)bytesWritten);

  free(buf);
  Serial.println("✅ Test selesai");
}

// setup hardware dari speaker agar menyamakan extension pada esp32 dan server.py
void initI2SSpeaker() {
  Serial.println("🔧 Init I2S Speaker...");

  i2s_driver_uninstall(I2S_SPK_PORT);

  i2s_config_t config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = 48000,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 16,
    .dma_buf_len          = 512,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num   = SPK_BCLK,
    .ws_io_num    = SPK_WS,
    .data_out_num = SPK_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };

  esp_err_t err;
  err = i2s_driver_install(I2S_SPK_PORT, &config, 0, NULL);
  Serial.printf("i2s_driver_install: %s\n", esp_err_to_name(err));

  err = i2s_set_pin(I2S_SPK_PORT, &pin_config);
  Serial.printf("i2s_set_pin: %s\n", esp_err_to_name(err));

  i2s_zero_dma_buffer(I2S_SPK_PORT);

  Serial.println("✅ I2S Speaker Ready");
}

bool streamTTS(String text) {

  WiFiClient client;    //connect ke server.py
  client.setTimeout(15000);

  Serial.println("🌐 Connecting TTS...");
  if (!client.connect(TTS_SERVER_IP, TTS_SERVER_PORT)) {
    Serial.println("❌ Gagal konek");
    return false;
  }

  String safeText = text;
  safeText.replace("\"", "'");
  String body = "{\"text\":\"" + safeText + "\"}";

  client.println("POST /tts HTTP/1.0");
  client.println("Host: " + String(TTS_SERVER_IP));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(body.length()));
  client.println("Connection: close");
  client.println();
  client.print(body);

  // ================= SKIP HEADER =================
  // mulai skip header dari http karena hanya butuh binary pcm 
  Serial.println("⏭ Skipping HTTP header...");
  int headerLines = 0;
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    Serial.printf("  HDR[%d]: '%s'\n", headerLines, line.c_str());
    headerLines++;
    if (line == "\r" || line.length() == 0) break;
    if (headerLines > 30) {
      Serial.println("❌ Header abort");
      client.stop();
      return false;
    }
  }
  Serial.printf("✅ Header selesai (%d lines)\n", headerLines);

  // ================= MALLOC =================
  const int CHUNK = 1024;
  uint8_t* buf = (uint8_t*)malloc(CHUNK);
  int16_t* out = (int16_t*)malloc(CHUNK * sizeof(int16_t)); // 2048 bytes

  if (!buf || !out) {
    Serial.println("❌ malloc gagal");
    free(buf);
    free(out);
    client.stop();
    return false;
  }

  Serial.printf("✅ malloc OK: buf=%p out=%p\n", buf, out);
  Serial.println("🔊 Streaming PCM...");

  int leftover      = 0;
  size_t bytesWritten = 0;
  int totalBytes    = 0;
  int totalSamples  = 0;
  int chunkCount    = 0;
  int32_t pcmSum    = 0;
  int16_t pcmMin    = 0;
  int16_t pcmMax    = 0;

  // ================= STREAM LOOP =================
  while (client.connected()) {
    int avail = client.available();
    if (avail <= 0) { delay(1); continue; }

    int toRead = min(avail, CHUNK - leftover);
    int got    = client.read(buf + leftover, toRead);
    int total  = got + leftover;

    int aligned = total & ~1;
    leftover    = total - aligned;

    if (aligned > 0) {
      int samples = aligned / 2;
      totalBytes  += aligned;
      totalSamples += samples;
      chunkCount++;

      // ================= DEBUG RAW BYTES chunk#1 =================
      if (chunkCount == 1) {
        Serial.println("=== RAW BYTES chunk#1 (first 16) ===");
        for (int i = 0; i < min(16, aligned); i++) {
          Serial.printf("%02X ", buf[i]);
        }
        Serial.println();
      }

      // ================= CONVERT uint8 -> int16 =================
      // fungsinya agar lebih ringan, lebih tajam suaranya, agar sesuai dengan extension esp32 dan max98357a juga 
      for (int i = 0; i < samples; i++) {
        int idx = i * 2;
        out[i]  = (int16_t)((uint16_t)buf[idx] | ((uint16_t)buf[idx + 1] << 8));

        if (out[i] > pcmMax) pcmMax = out[i];
        if (out[i] < pcmMin) pcmMin = out[i];
        pcmSum += abs(out[i]);
      }

      // ================= DEBUG PCM VALUES =================
      if (chunkCount <= 5) {
        // print 16 sample pertama dari tiap chunk awal
        Serial.printf("\n--- Chunk#%d PCM samples (first 16) ---\n", chunkCount);
        for (int i = 0; i < min(16, samples); i++) {
          Serial.printf("[%3d] %6d  ", i, out[i]);
          if ((i + 1) % 4 == 0) Serial.println();
        }
        Serial.println();
      }

      // setiap 10 chunk print 4 sample + stats sementara
    //  if (chunkCount % 10 == 0) {
       // Serial.printf(
        //  "Chunk#%d | bytes=%d | samples=%d | "
        //  "pcm[0..3]: %d %d %d %d | "
        //  "min=%d max=%d avg=%d\n",
        //  chunkCount, aligned, samples,
        //  out[0], out[1], out[2], out[3],
        //  (int)pcmMin, (int)pcmMax,
        //  (int)(pcmSum / totalSamples)
      //  );
    //  }

      // ================= I2S WRITE 16BIT =================
      // data max98357a dibaca oleh pin dan diteruskan ke speaker secara realtime streaming
      esp_err_t err = i2s_write(
        I2S_SPK_PORT,
        out,
        samples * sizeof(int16_t),
        &bytesWritten,
        portMAX_DELAY
      );

      if (chunkCount <= 3) {
        Serial.printf("  → i2s write: %s | bytesWritten: %d\n",
          esp_err_to_name(err), (int)bytesWritten);
      }
    }

    if (leftover > 0) {
      buf[0] = buf[aligned];
    }
  }

  free(buf);
  free(out);
  client.stop();
  i2s_zero_dma_buffer(I2S_SPK_PORT);

  // ================= SUMMARY =================
  Serial.println("\n=== PCM SUMMARY ===");
  Serial.printf("Total bytes    : %d\n", totalBytes);
  Serial.printf("Total samples  : %d\n", totalSamples);
  Serial.printf("Total chunks   : %d\n", chunkCount);
  Serial.printf("PCM min        : %d\n", (int)pcmMin);
  Serial.printf("PCM max        : %d\n", (int)pcmMax);
  Serial.printf("PCM avg abs    : %d\n",
    totalSamples > 0 ? (int)(pcmSum / totalSamples) : 0);

  if (chunkCount == 0) {
    Serial.println("❌ TIDAK ADA DATA");
  } else if (pcmSum == 0) {
    Serial.println("⚠️  PCM ALL ZERO → ffmpeg/edge-tts gagal");
  } else if (pcmMax < 100 && pcmMin > -100) {
    Serial.println("⚠️  PCM sangat kecil");
  } else {
    Serial.println("✅ PCM ada sinyal → cek wiring jika tidak ada suara");
  }

  Serial.println("✅ TTS selesai");
  return true;
}
