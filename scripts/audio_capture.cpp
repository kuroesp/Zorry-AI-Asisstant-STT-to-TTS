#include "audio_capture.h"

// ================= VAD STATE =================
bool hasValidSpeech = false; //apakah ada suara valid yang terekam
bool speechEnded    = false;// apakah user sudah selesai berbicara

// ================= SEGMEN STATE =================
bool isSpeaking           = false;  //apakah user sedang berbicara
unsigned long speechStart = 0;  // kapan mulai bicara
unsigned long lastSpeechMs = 0; // kapan terakhir terdeteksi suara

// ================= TUNING =================
const int   ENERGY_THRESHOLD   = 30; //minimum energi biar dianggap suara
const float VARIANCE_THRESHOLD = 800.0f; //minimum variasi agar bukan noise statis
const int   SILENCE_MS         = 1000; // diam 1 detik dianggap berhenti berbicara
const int   MIN_SPEECH_MS      = 150; //minimal 150ms agar dianggap valid 

// ================= UI =================
bool waitingPrinted = false;

// ================= FEATURE =================
// 1. getenergy = rata-rata nilai absolut sebuah sample, kalo tinggi suara keras, kalo rendah noise/diam
// 2. getvariance = seberapa variasi sample rata-ratanya, kalo tinggi itu speech, kalo rendah itu buzz/hum
int getEnergy(int16_t* data, int len) {
  long sum = 0;
  for (int i = 0; i < len; i++) sum += abs(data[i]);
  return sum / len;
}

float getVariance(int16_t* data, int len) {
  long sum = 0;
  for (int i = 0; i < len; i++) sum += abs(data[i]);
  float mean = (float)sum / len;

  float varSum = 0;
  for (int i = 0; i < len; i++) {
    float diff = abs(data[i]) - mean;
    varSum += diff * diff;
  }
  return varSum / len;
}

// ================= DECISION =================
// isspeechframe = adalah kondisi untuk melihat apakah speechvalid atau tidak. 
// cons1 = kalo energi terlalu rendah itu bukan suara 
// cons2 = kalo ada suara dan energi cukup tinggi lanjut merekam
// cons3 = kalo belum bicara butuh energi dan variasi yang tinggi fungsinya untuk menghindari miss VAD
bool isSpeechFrame(int16_t* data, int len) {
  int   energy   = getEnergy(data, len);
  float variance = getVariance(data, len);

  if (energy < ENERGY_THRESHOLD) return false;
  if (isSpeaking && energy >= ENERGY_THRESHOLD) return true;
  if (energy >= ENERGY_THRESHOLD && variance >= VARIANCE_THRESHOLD) return true;

  return false;
}

bool i2sInitialized = false; 

// ================= INIT I2S (ONLY ONCE) =================
//menginstall RX/TX sekaligus mencegah driver terinstall 2 kali yang bisa buat crash
void initI2S() {

  if (i2sInitialized) return; // 🔥 jangan install ulang

  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX), // 🔥 FIX
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = 6,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = BCLK,
    .ws_io_num = WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = DIN
  };

  i2s_driver_install(I2S_PORT, &config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);

  i2sInitialized = true;

  Serial.println("✅ I2S READY (TX + RX)");
}

// ================= SWITCH TO MIC =================

void useMic() {
  i2s_pin_config_t pins = {
    .bck_io_num = BCLK,
    .ws_io_num = WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = DIN
  };

  i2s_set_pin(I2S_PORT, &pins);
  Serial.println("🎙️ MIC mode");
}

// ================= RECORD + VAD =================

void recordAudioStream(AudioCallback callback, int duration_seconds) {
  
  // cara kerja dari record audio stream: pastikan mic aktif -> setup buffer frame per frame -> mulai reset state supaya buffer jadi 0 lagi -> while selama sample read masi kurang banyak dari total sample maka audio stream terus berjalan -> ambil data dari mic -> deteksi apakah speech/noise/buzz/quiet -> setelah total sample terpenuhi kirim buffer ke STT 

  useMic(); // 🔥 pastikan mode mic


  int totalSamples = SAMPLE_RATE * duration_seconds;
  int samplesRead  = 0;

  int16_t temp[BUFFER_SIZE];
  size_t bytesRead;

  // reset state
  isSpeaking      = false;
  hasValidSpeech  = false;
  speechEnded     = false;
  speechStart     = 0;
  lastSpeechMs    = millis();
  waitingPrinted  = false;

  while (samplesRead < totalSamples) {

    i2s_read(I2S_PORT, temp, sizeof(temp), &bytesRead, portMAX_DELAY);
    int count = bytesRead / 2;

    bool speechDetected = isSpeechFrame(temp, count);

    if (!isSpeaking && !speechDetected && !waitingPrinted) {
      Serial.println("🎙️ Menunggu suara...");
      waitingPrinted = true;
    }

    if (!isSpeaking && speechDetected) {
      isSpeaking     = true;
      speechStart    = millis();
      lastSpeechMs   = millis();
      waitingPrinted = false;
      Serial.println("🔴 Recording...");
    }

    if (isSpeaking) {
      lastSpeechMs = millis();

      if (millis() - speechStart >= MIN_SPEECH_MS) {
        hasValidSpeech = true;
      }

      if (callback) callback(temp, count);
    }

    if (isSpeaking && !speechDetected) {
      if (millis() - lastSpeechMs >= SILENCE_MS) {
        isSpeaking  = false;
        speechEnded = true;

        if (hasValidSpeech) break;
      }
    }

    samplesRead += count;
  }
}
