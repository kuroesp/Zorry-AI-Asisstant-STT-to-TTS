
#include "stt_client.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#define GROQ_API_KEY "" // your api key grog

#define STT_HOST  "api.groq.com"
#define STT_PATH  "/openai/v1/audio/transcriptions"
#define STT_MODEL "whisper-large-v3-turbo" //model ai


// ================= HALLUCINATION FILTER =================
// fungsi ishallucination adalah mencegah ai menebak-nebak kata yang sebenarnya noise malah menjadi kata asal seperti "terimakasih", ini dianggap reply yang tidak valid.
// fungisnya juga memfilter kata yang jika terlalu pendek akan nonvalid reply
bool isHallucination(String text) {
  String lower = text;
  lower.toLowerCase();
  lower.trim();

  if (lower.length() == 0) return true;

  bool hasLetter = false;
  for (int i = 0; i < (int)lower.length(); i++) {
    if (isAlpha(lower[i])) { hasLetter = true; break; }
  }
  if (!hasLetter) return true;

  const char* soloBlacklist[] = {
    "you", "the", "thank you", "thanks", "bye",
    "okay", "ok", ".", "...", "subscribe",
    "terima kasih.", "terimakasih."
  };

  int count = sizeof(soloBlacklist) / sizeof(soloBlacklist[0]);
  for (int i = 0; i < count; i++) {
    if (lower == String(soloBlacklist[i])) return true;
  }

  if (lower.length() < 2) return true;
  return false;
}

// ← harus ada di sini, SEBELUM sendToSTT()
// fungsinya supaya mengubah pcm RAW menjadi data format WAV (sample rate 16 khz, mono, 16bit)
void writeWavHeader(uint8_t* header, int dataSize) {
  int sampleRate    = SAMPLE_RATE;
  int numChannels   = 1;
  int bitsPerSample = 16;
  int byteRate      = sampleRate * numChannels * bitsPerSample / 8;
  int blockAlign    = numChannels * bitsPerSample / 8;

  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';

  int fileSize = dataSize + 36;
  header[4] = fileSize & 0xFF;
  header[5] = (fileSize >> 8) & 0xFF;
  header[6] = (fileSize >> 16) & 0xFF;
  header[7] = (fileSize >> 24) & 0xFF;

  header[8]  = 'W'; header[9]  = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';

  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1;  header[21] = 0;

  header[22] = numChannels; header[23] = 0;

  header[24] = sampleRate & 0xFF;
  header[25] = (sampleRate >> 8) & 0xFF;
  header[26] = (sampleRate >> 16) & 0xFF;
  header[27] = (sampleRate >> 24) & 0xFF;

  header[28] = byteRate & 0xFF;
  header[29] = (byteRate >> 8) & 0xFF;
  header[30] = (byteRate >> 16) & 0xFF;
  header[31] = (byteRate >> 24) & 0xFF;

  header[32] = blockAlign; header[33] = 0;
  header[34] = bitsPerSample; header[35] = 0;

  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';

  header[40] = dataSize & 0xFF;
  header[41] = (dataSize >> 8) & 0xFF;
  header[42] = (dataSize >> 16) & 0xFF;
  header[43] = (dataSize >> 24) & 0xFF;
}

// baru di sini sendToSTT()
// mengirim  ke ai untuk diubah menjadi kata. connect ke ai api.grog.com  -> kalo gagal koneksi coba lagi -> confersi file ke file upload dan bukan json biasa -> kirim request ke api -> menerima respon lalu mengolah data respon menjadi rapi (json parse) -> data yang rapi, sistem mengambil bagian response string saja (reply ai) -> masuk ke filtering noises text/kata random -> maka text reply ai berhasil
String sendToSTT(uint8_t* data, size_t length) {

  for (int attempt = 1; attempt <= 2; attempt++) {

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    Serial.print("Connecting to Groq (attempt ");
    Serial.print(attempt);
    Serial.println(")...");

    if (!client.connect(STT_HOST, 443)) {
      Serial.println("❌ Gagal konek, coba lagi...");
      delay(1000);
      continue;  // ← retry
    }

    Serial.println("Connected!");

    uint8_t wavHeader[44];
    writeWavHeader(wavHeader, length);

    String boundary = "ESP32Boundary12345";

    String filePart =
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
      "Content-Type: audio/wav\r\n\r\n";

    String modelPart =
      "\r\n--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
      + String(STT_MODEL) + "\r\n";

    String langPart =
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
      "id\r\n";

    String formatPart =
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
      "verbose_json\r\n";

    String closingPart = "--" + boundary + "--\r\n";

    int totalBody = filePart.length() + 44 + length
                  + modelPart.length()
                  + langPart.length()
                  + formatPart.length()
                  + closingPart.length();

    // ================= HTTP HEADER =================
    client.println("POST " + String(STT_PATH) + " HTTP/1.1");
    client.println("Host: " + String(STT_HOST));
    client.println("Authorization: Bearer " + String(GROQ_API_KEY));
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(totalBody));
    client.println("Connection: close");
    client.println();

    // ================= BODY =================
    client.print(filePart);
    client.write(wavHeader, 44);

    size_t chunkSize = 1024;
    for (size_t i = 0; i < length; i += chunkSize) {
      size_t sendSize = min(chunkSize, length - i);
      client.write(data + i, sendSize);
      delay(1);
    }

    client.print(modelPart);
    client.print(langPart);
    client.print(formatPart);
    client.print(closingPart);

    Serial.println("Audio terkirim...");
    delay(500);

    // ================= RESPONSE =================
    String body  = "";
    bool isBody  = false;
    long timeout = millis() + 15000;

    while (millis() < timeout) {
      while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") { isBody = true; continue; }
        if (isBody) body += line;
      }
      if (!client.connected()) break;
    }

    client.stop();

    // Serial.println("FULL RESPONSE: " + body);  // uncomment untuk debug

    // ================= PARSE JSON =================
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
      Serial.println("JSON parse error!");
      // kalau parse error → coba lagi
      delay(500);
      continue;
    }

    // ================= CEK NO SPEECH =================
    float noSpeechProb = 0.0f;
    if (doc["segments"].is<JsonArray>() && doc["segments"].size() > 0) {
      noSpeechProb = doc["segments"][0]["no_speech_prob"].as<float>();
    }

    // Serial.print("no_speech_prob: "); Serial.println(noSpeechProb);  // debug

    if (noSpeechProb > 0.6f) {
      Serial.println("❌ Whisper: tidak yakin speech");
      return "";
    }

    // ================= AMBIL TEKS =================
    String text = doc["text"].as<String>();
    text.trim();

    // Serial.print("Raw text: "); Serial.println(text);  // debug

    if (text.isEmpty()) return "";

    if (isHallucination(text)) return "";

    return text;  // ← berhasil, keluar dari loop
  }

  // semua attempt gagal
  return "ERROR: Gagal konek setelah 2x percobaan";
}
