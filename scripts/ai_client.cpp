#include "ai_client.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <memory.h>

#define OPENROUTER_API_KEY "" //your api key 

#define AI_HOST  "openrouter.ai" 
#define AI_PATH  "/api/v1/chat/completions"
#define AI_MODEL "qwen/qwen-2.5-72b-instruct" // if u wanna change model of ai u can as you want


String sendToAI(String userText) {

  String systemPrompt = buildSystemPrompt(userText);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  Serial.println("🌐 Connecting AI...");

  if (!client.connect(AI_HOST, 443)) {
    Serial.println("❌ AI connect failed");
    return "ERROR: connect failed";
  }

  // ================= BUILD JSON =================
  DynamicJsonDocument doc(4096);
  doc["model"]      = AI_MODEL;
  doc["max_tokens"] = 200;

  JsonArray messages = doc.createNestedArray("messages");

  JsonObject sys = messages.createNestedObject();
  sys["role"]    = "system";
  sys["content"] = systemPrompt;

  JsonObject usr = messages.createNestedObject();
  usr["role"]    = "user";
  usr["content"] = userText;

  String reqBody;
  serializeJson(doc, reqBody);

  // ================= HTTP REQUEST (1.0 = no chunked) =================
  client.println("POST " + String(AI_PATH) + " HTTP/1.0");
  client.println("Host: " + String(AI_HOST));
  client.println("Authorization: Bearer " + String(OPENROUTER_API_KEY));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(reqBody.length()));
  client.println("Connection: close");
  client.println();
  client.print(reqBody);

  // ================= WAIT RESPONSE =================
  long timeout = millis() + 15000;
  while (!client.available() && millis() < timeout) {
    delay(10);
  }

  if (!client.available()) {
    Serial.println("❌ No response");
    client.stop();
    return "ERROR: no response";
  }

  // ================= SKIP HEADER =================
  bool isChunked = false;
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line.indexOf("Transfer-Encoding: chunked") >= 0) {
      isChunked = true;  // catat kalau chunked
    }
    if (line == "\r" || line.length() == 0) break;
  }

  Serial.printf("📦 Chunked: %s\n", isChunked ? "YES" : "NO");

  // ================= READ BODY =================
  String body = "";
  timeout = millis() + 15000;

  if (isChunked) {
    // ===== CHUNKED DECODE =====
    while (millis() < timeout) {
      if (!client.available()) {
        if (!client.connected()) break;
        delay(5);
        continue;
      }

      String chunkSizeLine = client.readStringUntil('\n');
      chunkSizeLine.trim();
      if (chunkSizeLine.length() == 0) continue;

      char* endPtr;
      long chunkSize = strtol(chunkSizeLine.c_str(), &endPtr, 16);

      if (chunkSize == 0) break; // last chunk

      for (long i = 0; i < chunkSize && millis() < timeout; i++) {
        while (!client.available() && millis() < timeout) delay(1);
        if (client.available()) body += (char)client.read();
      }
      client.readStringUntil('\n'); // trailing \r\n
    }

  } else {
    // ===== PLAIN READ =====
    while (millis() < timeout) {
      while (client.available()) {
        body += (char)client.read();
      }
      if (!client.connected()) break;
      delay(5);
    }
  }

  client.stop();

  if (body.length() == 0) {
    Serial.println("❌ Empty body");
    return "ERROR: empty";
  }

  //Serial.println("===== RAW BODY =====");
  //Serial.println(body);

  // ================= EXTRACT JSON =================
  int start = body.indexOf('{');
  int end   = body.lastIndexOf('}');

  if (start == -1 || end == -1 || end <= start) {
    Serial.println("❌ Invalid JSON block");
    return "ERROR: invalid json";
  }

  String jsonStr = body.substring(start, end + 1);

  // ================= PARSE =================
  DynamicJsonDocument res(8192);
  DeserializationError err = deserializeJson(res, jsonStr);

  if (err) {
    Serial.print("❌ JSON parse error: ");
    Serial.println(err.c_str());
    Serial.println("JSON string: " + jsonStr.substring(0, 200));
    return "ERROR: parse";
  }

  // ================= EXTRACT REPLY =================
  if (!res["choices"] || res["choices"].size() == 0) {
    Serial.println("❌ No choices in response");
    return "ERROR: no choices";
  }

  String reply = res["choices"][0]["message"]["content"] | "";
  reply.trim();

  if (reply.length() == 0) {
    return "ERROR: empty reply";
  }

  Serial.println("🤖 AI OK: " + reply.substring(0, 50));

  // ================= UPDATE MEMORY =================
  static int unsavedCount = 0;
  unsavedCount++;
  if (unsavedCount >= 5) {
    updateContext(userText, reply);
    updateConversationCount();
    unsavedCount = 0;
  }
 
  
  return reply;
}
