#include "memory.h"
bool sdReady = false;

// you can change memory script in another language (indonesia in default) 
// ================= HELPER =================
String waitForInput(String prompt) {
  Serial.println(prompt);
  Serial.print("> ");

  String input = "";
  long timeout = millis() + 30000;

  while (millis() < timeout) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (input.length() > 0) break;
      } else {
        input += c;
        Serial.print(c);
      }
    }
  }

  Serial.println();
  input.trim();
  return input;
}

// ================= INIT =================
bool createFileIfNotExist(const char* path, const char* defaultContent) {
  if (!SD.exists(path)) {
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
      Serial.println("❌ Gagal buat: " + String(path));
      return false;
    }
    f.print(defaultContent);
    f.close();
    Serial.println("✅ Dibuat: " + String(path));
  }
  return true;
}

bool initMemory() {
  Serial.println("🔄 Init SD Card...");
  Serial.println("─────────────────────────────");

  // ===== STEP 1: PAKSA CS HIGH =====
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(200);
  Serial.print("STEP 1 | CS (GPIO ");
  Serial.print(SD_CS);
  Serial.print(") HIGH → ");
  Serial.println(digitalRead(SD_CS) == HIGH ? "✅ OK" : "❌ GAGAL — CS tidak bisa HIGH");

  // ===== STEP 2: CEK PIN SEBELUM SPI =====
  Serial.println("─────────────────────────────");
  Serial.println("STEP 2 | Cek pin sebelum SPI.begin:");

  pinMode(18, INPUT);  // SCK
  pinMode(19, INPUT);  // MISO
  pinMode(23, INPUT);  // MOSI

  int sck  = digitalRead(18);
  int miso = digitalRead(19);
  int mosi = digitalRead(23);
  int cs   = digitalRead(SD_CS);

  Serial.print("  SCK  (GPIO 18) : "); Serial.print(sck);
  Serial.println(sck == 0 ? " ← ⚠️ LOW (mungkin short ke GND)" : " ← OK");

  Serial.print("  MISO (GPIO 19) : "); Serial.print(miso);
  if (miso == 0) Serial.println(" ← ⚠️ LOW sebelum SPI — kemungkinan short ke GND atau pin DO modul SD terhubung ke sesuatu");
  else           Serial.println(" ← OK (floating HIGH)");

  Serial.print("  MOSI (GPIO 23) : "); Serial.print(mosi);
  Serial.println(" ← info saja");

  Serial.print("  CS   (GPIO ");
  Serial.print(SD_CS);
  Serial.print(") : "); Serial.print(cs);
  Serial.println(cs == HIGH ? " ← ✅ HIGH (benar)" : " ← ❌ LOW — CS harus HIGH saat idle");

  // ===== STEP 3: INIT SPI =====
  Serial.println("─────────────────────────────");
  Serial.println("STEP 3 | SPI.begin(18, 19, 23, CS)...");
  SPI.begin(18, 19, 23, SD_CS);
  delay(100);
  Serial.println("  ✅ SPI.begin selesai");

  // ===== STEP 4: CEK SPI RESPONSE =====
  Serial.println("─────────────────────────────");
  Serial.println("STEP 4 | Kirim 0xFF ke card, baca respon MISO:");

  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  digitalWrite(SD_CS, LOW);
  delay(10);
  byte resp = SPI.transfer(0xFF);
  digitalWrite(SD_CS, HIGH);
  SPI.endTransaction();

  Serial.print("  SPI response : 0x");
  Serial.println(resp, HEX);

  if (resp == 0xFF) {
    Serial.println("  ❌ 0xFF — MISO tidak terhubung sama sekali");
    Serial.println("     → Cek jumper DO modul SD → GPIO 19 ESP32");
    Serial.println("     → Pastikan tidak ada baris breadboard yang kosong di antaranya");
  } else if (resp == 0x00) {
    Serial.println("  ❌ 0x00 — MISO short ke GND");
    Serial.println("     → Ada pin lain di baris horizontal yang sama dengan MISO");
    Serial.println("     → Geser modul SD ke baris breadboard yang benar-benar kosong");
  } else {
    Serial.print("  ✅ Card merespon (0x");
    Serial.print(resp, HEX);
    Serial.println(") — MISO terhubung dengan benar");
  }

  // ===== STEP 5: CEK CARD TYPE =====
  Serial.println("─────────────────────────────");
  Serial.println("STEP 5 | Cek card type & size:");
  SD.begin(SD_CS, SPI, 400000);

  uint8_t cardType = SD.cardType();
  Serial.print("  Card type : ");
  switch (cardType) {
    case CARD_NONE:
      Serial.println("❌ CARD_NONE — card tidak terdeteksi");
      Serial.println("     → Penyebab: MISO/MOSI/SCK tidak terhubung, atau VCC kurang");
      break;
    case CARD_MMC:
      Serial.println("MMC ✅");
      break;
    case CARD_SD:
      Serial.println("SD ✅");
      break;
    case CARD_SDHC:
      Serial.println("SDHC ✅");
      break;
    default:
      Serial.println("Unknown ⚠️ — card ada tapi tidak dikenali");
      break;
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.print("  Card size : ");
  Serial.print(cardSize);
  Serial.println(" MB");

  if (cardSize == 0 && cardType != CARD_NONE) {
    Serial.println("  ⚠️ Size 0 tapi card terdeteksi — kemungkinan filesystem corrupt");
  }

  SD.end();
  delay(100);

  // ===== STEP 6: MOUNT DENGAN BERBAGAI SPEED =====
  Serial.println("─────────────────────────────");
  Serial.println("STEP 6 | Mount filesystem:");

  uint32_t speeds[] = { 400000, 1000000, 4000000, 20000000 };
  String   labels[] = { "400kHz", "1MHz", "4MHz", "20MHz" };
  bool     mounted  = false;

  for (int i = 0; i < 4; i++) {
    Serial.print("  Coba " + labels[i] + " ... ");
    if (SD.begin(SD_CS, SPI, speeds[i])) {
      Serial.println("✅ BERHASIL!");
      Serial.println("  >>> Speed optimal: " + labels[i]);
      mounted = true;
      break;
    } else {
      Serial.println("❌");
      SD.end();
      delay(100);
    }
  }

  // ===== STEP 7: DIAGNOSIS AKHIR =====
  Serial.println("─────────────────────────────");
  if (!mounted) {
    Serial.println("STEP 7 | ❌ DIAGNOSIS AKHIR:");
    Serial.print("  MISO (GPIO 19) state : "); Serial.println(digitalRead(19));
    Serial.print("  CS   (GPIO ");
    Serial.print(SD_CS);
    Serial.print(") state : "); Serial.println(digitalRead(SD_CS));

    if (resp == 0xFF) {
      Serial.println("  → MASALAH: MISO tidak terhubung (DO modul SD → GPIO 19)");
    } else if (resp == 0x00) {
      Serial.println("  → MASALAH: MISO short ke GND (pin lain di baris yang sama)");
    } else if (cardType == CARD_NONE) {
      Serial.println("  → MASALAH: Card terdeteksi SPI tapi tidak mount");
      Serial.println("     Kemungkinan: filesystem bukan FAT32, atau card corrupt");
      Serial.println("     Solusi: format ulang ke FAT32 lewat PC");
    } else {
      Serial.println("  → MASALAH: Tidak diketahui — coba ganti SD card");
    }

    return false;
  }

  Serial.println("STEP 7 | ✅ SD Card siap digunakan");
  Serial.println("─────────────────────────────");

  // ===== BUAT FILE JIKA BELUM ADA =====
  createFileIfNotExist(CORE_MEMORY_FILE,
    "{"
      "\"user_name\":\"\","
      "\"user_age\":\"\","
      "\"user_location\":\"\","
      "\"user_job\":\"\","
      "\"learning_style\":\"\","
      "\"skill_level\":\"\","
      "\"conversation_count\":0"
    "}"
  );

  createFileIfNotExist(CONTEXT_MEMORY_FILE,
    "{"
      "\"current_topic\":\"\","
      "\"current_project\":\"\","
      "\"last_question\":\"\","
      "\"last_answer\":\"\","
      "\"progress\":\"\""
    "}"
  );

  createFileIfNotExist(ONDEMAND_MEMORY_FILE,
    "{"
      "\"hobbies\":[],"
      "\"traits\":[],"
      "\"achievements\":[],"
      "\"preferences\":[],"
      "\"history\":[],"
      "\"extra_facts\":[]"
    "}"
  );
  sdReady = true;
  return true;
}

// ================= LOAD =================
String loadCoreMemory() {
  File f = SD.open(CORE_MEMORY_FILE, FILE_READ);
  if (!f) return "{}";
  String s = "";
  while (f.available()) s += (char)f.read();
  f.close();
  return s;
}

String loadContextMemory() {
  File f = SD.open(CONTEXT_MEMORY_FILE, FILE_READ);
  if (!f) return "{}";
  String s = "";
  while (f.available()) s += (char)f.read();
  f.close();
  return s;
}

String loadOnDemandMemory() {
  File f = SD.open(ONDEMAND_MEMORY_FILE, FILE_READ);
  if (!f) return "{}";
  String s = "";
  while (f.available()) s += (char)f.read();
  f.close();
  return s;
}

// ================= SAVE =================
bool saveToFile(const char* path, const String& jsonStr) {
  SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.println("❌ Gagal simpan"); return false; }
  f.print(jsonStr);
  f.close();
 // Serial.println("✅ Tersimpan!");
  return true;
}

bool saveCoreMemory(const String& jsonStr) {
  if(!sdReady) return false;
  return saveToFile(CORE_MEMORY_FILE, jsonStr); 
}
bool saveContextMemory(const String& jsonStr){
  if(!sdReady) return false;
  return saveToFile(CONTEXT_MEMORY_FILE, jsonStr); 
}   
bool saveOnDemandMemory(const String& jsonStr){
  if(!sdReady) return false;
  return saveToFile(ONDEMAND_MEMORY_FILE, jsonStr); 
}  

// ================= UPDATE CONTEXT =================
void updateContext(String userText, String aiReply) {
  if(!sdReady) return;
  String mem = loadContextMemory();
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, mem);

  // simpan pertanyaan & jawaban terakhir
  doc["last_question"] = userText.substring(0, 100);
  doc["last_answer"]   = aiReply.substring(0, 100);

  // deteksi topik dari keyword sederhana
  String lower = userText;
  lower.toLowerCase();

  if (lower.indexOf("proyek") >= 0 || lower.indexOf("project") >= 0 ||
      lower.indexOf("kerjain") >= 0 || lower.indexOf("buat") >= 0) {
    doc["current_topic"] = "project";
  } else if (lower.indexOf("belajar") >= 0 || lower.indexOf("coding") >= 0 ||
             lower.indexOf("code") >= 0 || lower.indexOf("program") >= 0) {
    doc["current_topic"] = "belajar";
  } else if (lower.indexOf("masalah") >= 0 || lower.indexOf("error") >= 0 ||
             lower.indexOf("bug") >= 0 || lower.indexOf("tidak bisa") >= 0) {
    doc["current_topic"] = "troubleshoot";
  } else if (lower.indexOf("cerita") >= 0 || lower.indexOf("curhat") >= 0 ||
             lower.indexOf("perasaan") >= 0) {
    doc["current_topic"] = "personal";
  }

  String updated;
  serializeJson(doc, updated);
  saveContextMemory(updated);
}

void updateConversationCount() {
  if (!sdReady) return;
  String mem = loadCoreMemory();
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, mem);
  doc["conversation_count"] = doc["conversation_count"].as<int>() + 1;
  String updated;
  serializeJson(doc, updated);
  saveCoreMemory(updated);
}

// ================= BUILD SYSTEM PROMPT =================
// ini fungsi utama — dipanggil dari ai_client.cpp
String buildSystemPrompt(String userText) {

  if(!SD.begin(SD_CS)){

    return;
  }

  // ===== 1. CORE MEMORY — selalu ada =====
  String coreMem = loadCoreMemory();
  DynamicJsonDocument coreDoc(2048);
  deserializeJson(coreDoc, coreMem);

  String userName     = coreDoc["user_name"].as<String>();
  String userAge      = coreDoc["user_age"].as<String>();
  String userLocation = coreDoc["user_location"].as<String>();
  String userJob      = coreDoc["user_job"].as<String>();
  String learnStyle   = coreDoc["learning_style"].as<String>();
  String skillLevel   = coreDoc["skill_level"].as<String>();
  int    convCount    = coreDoc["conversation_count"].as<int>();

  String coreContext = "";
  if (userName.length() > 0)     coreContext += "Nama: " + userName + ". ";
  if (userAge.length() > 0)      coreContext += "Umur: " + userAge + ". ";
  if (userLocation.length() > 0) coreContext += "Lokasi: " + userLocation + ". ";
  if (userJob.length() > 0)      coreContext += "Pekerjaan: " + userJob + ". ";
  if (skillLevel.length() > 0)   coreContext += "Level skill: " + skillLevel + ". ";
  if (learnStyle.length() > 0)   coreContext += "Gaya belajar: " + learnStyle + ". ";
  if (convCount > 0)             coreContext += "Sudah ngobrol " + String(convCount) + "x. ";

  // ===== 2. CONTEXT MEMORY — selalu ada tapi ringkas =====
  String ctxMem = loadContextMemory();
  DynamicJsonDocument ctxDoc(2048);
  deserializeJson(ctxDoc, ctxMem);

  String currentTopic   = ctxDoc["current_topic"].as<String>();
  String currentProject = ctxDoc["current_project"].as<String>();
  String lastQuestion   = ctxDoc["last_question"].as<String>();
  String progress       = ctxDoc["progress"].as<String>();

  String contextStr = "";
  if (currentTopic.length() > 0)   contextStr += "Topik saat ini: " + currentTopic + ". ";
  if (currentProject.length() > 0) contextStr += "Project: " + currentProject + ". ";
  if (progress.length() > 0)       contextStr += "Progress: " + progress + ". ";
  if (lastQuestion.length() > 0)   contextStr += "Pertanyaan sebelumnya: " + lastQuestion + ". ";

  // ===== 3. ON DEMAND MEMORY — hanya kalau relevan =====
  String lower = userText;
  lower.toLowerCase();

  bool needOnDemand =
    lower.indexOf("hobi") >= 0       ||
    lower.indexOf("prestasi") >= 0   ||
    lower.indexOf("sifat") >= 0      ||
    lower.indexOf("kesukaan") >= 0   ||
    lower.indexOf("prefer") >= 0     ||
    lower.indexOf("cerita") >= 0     ||
    lower.indexOf("dulu") >= 0       ||
    lower.indexOf("pernah") >= 0     ||
    lower.indexOf("ingat") >= 0      ||
    lower.indexOf("siapa aku") >= 0  ||
    lower.indexOf("tentang aku") >= 0;

  String onDemandStr = "";
  if (needOnDemand) {
    Serial.println("📂 Loading on-demand memory...");

    String odMem = loadOnDemandMemory();
    DynamicJsonDocument odDoc(4096);
    deserializeJson(odDoc, odMem);

    JsonArray hobbies      = odDoc["hobbies"].as<JsonArray>();
    JsonArray traits       = odDoc["traits"].as<JsonArray>();
    JsonArray achievements = odDoc["achievements"].as<JsonArray>();
    JsonArray preferences  = odDoc["preferences"].as<JsonArray>();
    JsonArray extraFacts   = odDoc["extra_facts"].as<JsonArray>();

    if (hobbies.size() > 0) {
      onDemandStr += "Hobi: ";
      for (String h : hobbies) onDemandStr += h + ", ";
      onDemandStr += ". ";
    }
    if (traits.size() > 0) {
      onDemandStr += "Sifat: ";
      for (String t : traits) onDemandStr += t + ", ";
      onDemandStr += ". ";
    }
    if (achievements.size() > 0) {
      onDemandStr += "Prestasi: ";
      for (String a : achievements) onDemandStr += a + ", ";
      onDemandStr += ". ";
    }
    if (preferences.size() > 0) {
      onDemandStr += "Preferensi: ";
      for (String p : preferences) onDemandStr += p + ", ";
      onDemandStr += ". ";
    }
    if (extraFacts.size() > 0) {
      onDemandStr += "Fakta tambahan: ";
      for (String e : extraFacts) onDemandStr += e + ", ";
      onDemandStr += ". ";
    }
  }

  // ===== GABUNGKAN JADI SYSTEM PROMPT =====
  String prompt =
    "You are a AI named Warren. "; // you can change his/her personality
    

  if (coreContext.length() > 0) {
    prompt += "\n[Identitas pengguna]: " + coreContext;
  }

  if (contextStr.length() > 0) {
    prompt += "\n[Konteks saat ini]: " + contextStr;
  }

  if (onDemandStr.length() > 0) {
    prompt += "\n[Info tambahan pengguna]: " + onDemandStr;
  }

  // debug — uncomment kalau mau lihat prompt
  // Serial.println("PROMPT: " + prompt);

  return prompt;
}

// ================= PRINT ALL MEMORY =================
void printCurrentMemory() {

  Serial.println("\n===== CORE MEMORY =====");
  String core = loadCoreMemory();
  DynamicJsonDocument cDoc(2048);
  deserializeJson(cDoc, core);
  Serial.println("Nama      : " + cDoc["user_name"].as<String>());
  Serial.println("Umur      : " + cDoc["user_age"].as<String>());
  Serial.println("Lokasi    : " + cDoc["user_location"].as<String>());
  Serial.println("Pekerjaan : " + cDoc["user_job"].as<String>());
  Serial.println("Skill     : " + cDoc["skill_level"].as<String>());
  Serial.println("Belajar   : " + cDoc["learning_style"].as<String>());
  Serial.println("Ngobrol   : " + cDoc["conversation_count"].as<String>() + "x");

  Serial.println("\n===== CONTEXT MEMORY =====");
  String ctx = loadContextMemory();
  DynamicJsonDocument xDoc(2048);
  deserializeJson(xDoc, ctx);
  Serial.println("Topik     : " + xDoc["current_topic"].as<String>());
  Serial.println("Project   : " + xDoc["current_project"].as<String>());
  Serial.println("Progress  : " + xDoc["progress"].as<String>());
  Serial.println("Last Q    : " + xDoc["last_question"].as<String>());

  Serial.println("\n===== ON DEMAND MEMORY =====");
  String od = loadOnDemandMemory();
  DynamicJsonDocument oDoc(4096);
  deserializeJson(oDoc, od);

  Serial.print("Hobi      : ");
  for (String h : oDoc["hobbies"].as<JsonArray>()) Serial.print(h + ", ");
  Serial.println();

  Serial.print("Sifat     : ");
  for (String t : oDoc["traits"].as<JsonArray>()) Serial.print(t + ", ");
  Serial.println();

  Serial.print("Prestasi  : ");
  for (String a : oDoc["achievements"].as<JsonArray>()) Serial.print(a + ", ");
  Serial.println();

  Serial.print("Preferensi: ");
  for (String p : oDoc["preferences"].as<JsonArray>()) Serial.print(p + ", ");
  Serial.println();

  Serial.print("Fakta     : ");
  for (String e : oDoc["extra_facts"].as<JsonArray>()) Serial.print(e + ", ");
  Serial.println();

  Serial.println("==========================\n");
}

// ======================================================
// ================= MENU UTAMA =========================
// ======================================================
void menuNama();
void menuKonteks();
void menuOnDemand();
void menuLihatData();
void menuReset();

void runMemoryMenu() {
  bool exitMenu = false;

  while (!exitMenu) {
    Serial.println("\n");
    Serial.println("╔══════════════════════════╗");
    Serial.println("║       MENU MEMORI AI      ║");
    Serial.println("╠══════════════════════════╣");
    Serial.println("║  1. Core Memory           ║");
    Serial.println("║  2. Context Memory        ║");
    Serial.println("║  3. On Demand Memory      ║");
    Serial.println("║  4. Lihat Semua Data      ║");
    Serial.println("║  5. Reset Data            ║");
    Serial.println("║  0. Keluar                ║");
    Serial.println("╚══════════════════════════╝");

    String pilihan = waitForInput("Pilih menu (0-5):");

    switch (pilihan.toInt()) {
      case 1: menuNama();      break;
      case 2: menuKonteks();   break;
      case 3: menuOnDemand();  break;
      case 4: menuLihatData(); break;
      case 5: menuReset();     break;
      case 0:
        Serial.println("✅ Keluar dari menu");
        exitMenu = true;
        break;
      default:
        Serial.println("❌ Pilihan tidak valid");
        break;
    }
  }
}

// ======================================================
// ================= SUBMENU CORE =======================
// ======================================================
void menuNama() {
  bool back = false;
  while (!back) {
    String mem = loadCoreMemory();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, mem);

    Serial.println("\n╔══════════════════════════╗");
    Serial.println("║       CORE MEMORY         ║");
    Serial.println("╠══════════════════════════╣");
    Serial.println("║  1. Nama                  ║");
    Serial.println("║  2. Umur                  ║");
    Serial.println("║  3. Lokasi                ║");
    Serial.println("║  4. Pekerjaan             ║");
    Serial.println("║  5. Level Skill           ║");
    Serial.println("║  6. Gaya Belajar          ║");
    Serial.println("║  0. Kembali               ║");
    Serial.println("╚══════════════════════════╝");
    Serial.println("Nama    : " + doc["user_name"].as<String>());
    Serial.println("Umur    : " + doc["user_age"].as<String>());
    Serial.println("Lokasi  : " + doc["user_location"].as<String>());
    Serial.println("Job     : " + doc["user_job"].as<String>());
    Serial.println("Skill   : " + doc["skill_level"].as<String>());
    Serial.println("Belajar : " + doc["learning_style"].as<String>());

    String p = waitForInput("Pilih (0-6):");
    switch (p.toInt()) {
      case 1: { String v = waitForInput("Nama:"); if (v.length()>0){ doc["user_name"]=v; String u; serializeJson(doc,u); saveCoreMemory(u); } break; }
      case 2: { String v = waitForInput("Umur:"); if (v.length()>0){ doc["user_age"]=v; String u; serializeJson(doc,u); saveCoreMemory(u); } break; }
      case 3: { String v = waitForInput("Lokasi:"); if (v.length()>0){ doc["user_location"]=v; String u; serializeJson(doc,u); saveCoreMemory(u); } break; }
      case 4: { String v = waitForInput("Pekerjaan:"); if (v.length()>0){ doc["user_job"]=v; String u; serializeJson(doc,u); saveCoreMemory(u); } break; }
      case 5: { String v = waitForInput("Level skill (pemula/menengah/mahir):"); if (v.length()>0){ doc["skill_level"]=v; String u; serializeJson(doc,u); saveCoreMemory(u); } break; }
      case 6: { String v = waitForInput("Gaya belajar (visual/praktek/teori):"); if (v.length()>0){ doc["learning_style"]=v; String u; serializeJson(doc,u); saveCoreMemory(u); } break; }
      case 0: back = true; break;
      default: Serial.println("❌ Tidak valid"); break;
    }
  }
}

// ======================================================
// ================= SUBMENU CONTEXT ====================
// ======================================================
void menuKonteks() {
  bool back = false;
  while (!back) {
    String mem = loadContextMemory();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, mem);

    Serial.println("\n╔══════════════════════════╗");
    Serial.println("║      CONTEXT MEMORY       ║");
    Serial.println("╠══════════════════════════╣");
    Serial.println("║  1. Topik Saat Ini        ║");
    Serial.println("║  2. Project Aktif         ║");
    Serial.println("║  3. Progress Project      ║");
    Serial.println("║  4. Reset Context         ║");
    Serial.println("║  0. Kembali               ║");
    Serial.println("╚══════════════════════════╝");
    Serial.println("Topik   : " + doc["current_topic"].as<String>());
    Serial.println("Project : " + doc["current_project"].as<String>());
    Serial.println("Progress: " + doc["progress"].as<String>());

    String p = waitForInput("Pilih (0-4):");
    switch (p.toInt()) {
      case 1: { String v = waitForInput("Topik saat ini:"); if(v.length()>0){ doc["current_topic"]=v; String u; serializeJson(doc,u); saveContextMemory(u); } break; }
      case 2: { String v = waitForInput("Nama project:"); if(v.length()>0){ doc["current_project"]=v; String u; serializeJson(doc,u); saveContextMemory(u); } break; }
      case 3: { String v = waitForInput("Progress project:"); if(v.length()>0){ doc["progress"]=v; String u; serializeJson(doc,u); saveContextMemory(u); } break; }
      case 4: {
        String reset = "{\"current_topic\":\"\",\"current_project\":\"\",\"last_question\":\"\",\"last_answer\":\"\",\"progress\":\"\"}";
        saveContextMemory(reset);
        Serial.println("✅ Context direset");
        break;
      }
      case 0: back = true; break;
      default: Serial.println("❌ Tidak valid"); break;
    }
  }
}

// ======================================================
// ================= SUBMENU ON DEMAND ==================
// ======================================================
void menuOnDemand() {
  bool back = false;
  while (!back) {
    Serial.println("\n╔══════════════════════════╗");
    Serial.println("║      ON DEMAND MEMORY     ║");
    Serial.println("╠══════════════════════════╣");
    Serial.println("║  1. Hobi                  ║");
    Serial.println("║  2. Sifat / Karakter      ║");
    Serial.println("║  3. Prestasi              ║");
    Serial.println("║  4. Preferensi            ║");
    Serial.println("║  5. Fakta Tambahan        ║");
    Serial.println("║  0. Kembali               ║");
    Serial.println("╚══════════════════════════╝");

    String p = waitForInput("Pilih (0-5):");

    // helper lambda-like untuk tambah/hapus array
    auto editArray = [&](const char* key, String label) {
      bool innerBack = false;
      while (!innerBack) {
        String mem = loadOnDemandMemory();
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, mem);
        JsonArray arr = doc[key].as<JsonArray>();

        Serial.println("\n1. Tambah " + label);
        Serial.println("2. Hapus " + label);
        Serial.println("3. Lihat semua");
        Serial.println("0. Kembali");

        String pp = waitForInput("Pilih:");
        switch (pp.toInt()) {
          case 1: {
            String v = waitForInput("Masukkan " + label + ":");
            if (v.length() > 0) {
              doc[key].add(v);
              String u; serializeJson(doc, u);
              saveOnDemandMemory(u);
              Serial.println("✅ Ditambahkan: " + v);
            }
            break;
          }
          case 2: {
            if (arr.size() == 0) { Serial.println("❌ Kosong"); break; }
            int idx = 0;
            for (String item : arr) Serial.println(String(idx++) + ". " + item);
            String idxStr = waitForInput("Nomor yang dihapus:");
            int del = idxStr.toInt();
            if (del >= 0 && del < (int)arr.size()) {
              arr.remove(del);
              String u; serializeJson(doc, u);
              saveOnDemandMemory(u);
              Serial.println("✅ Dihapus");
            }
            break;
          }
          case 3: {
            if (arr.size() == 0) { Serial.println("(kosong)"); break; }
            int idx = 0;
            for (String item : arr) Serial.println(String(idx++) + ". " + item);
            break;
          }
          case 0: innerBack = true; break;
          default: Serial.println("❌ Tidak valid"); break;
        }
      }
    };

    switch (p.toInt()) {
      case 1: editArray("hobbies",      "Hobi");            break;
      case 2: editArray("traits",       "Sifat");           break;
      case 3: editArray("achievements", "Prestasi");        break;
      case 4: editArray("preferences",  "Preferensi");      break;
      case 5: editArray("extra_facts",  "Fakta Tambahan");  break;
      case 0: back = true; break;
      default: Serial.println("❌ Tidak valid"); break;
    }
  }
}

// ======================================================
// ================= LIHAT & RESET ======================
// ======================================================
void menuLihatData() {
  printCurrentMemory();
  waitForInput("Enter untuk kembali...");
}

void menuReset() {
  Serial.println("\n╔══════════════════════════╗");
  Serial.println("║        RESET DATA         ║");
  Serial.println("╠══════════════════════════╣");
  Serial.println("║  1. Reset Core Memory     ║");
  Serial.println("║  2. Reset Context Memory  ║");
  Serial.println("║  3. Reset On Demand       ║");
  Serial.println("║  4. Reset SEMUA           ║");
  Serial.println("║  0. Kembali               ║");
  Serial.println("╚══════════════════════════╝");

  String p = waitForInput("Pilih (0-4):");
  switch (p.toInt()) {
    case 1: {
      String k = waitForInput("Yakin? (YA):");
      if (k == "YA" || k == "ya") { SD.remove(CORE_MEMORY_FILE); initMemory(); Serial.println("✅ Core direset"); }
      break;
    }
    case 2: {
      saveContextMemory("{\"current_topic\":\"\",\"current_project\":\"\",\"last_question\":\"\",\"last_answer\":\"\",\"progress\":\"\"}");
      Serial.println("✅ Context direset");
      break;
    }
    case 3: {
      saveOnDemandMemory("{\"hobbies\":[],\"traits\":[],\"achievements\":[],\"preferences\":[],\"history\":[],\"extra_facts\":[]}");
      Serial.println("✅ On Demand direset");
      break;
    }
    case 4: {
      String k = waitForInput("Yakin reset SEMUA? (YA):");
      if (k == "YA" || k == "ya") {
        SD.remove(CORE_MEMORY_FILE);
        SD.remove(CONTEXT_MEMORY_FILE);
        SD.remove(ONDEMAND_MEMORY_FILE);
        initMemory();
        Serial.println("✅ Semua direset!");
      }
      break;
    }
    case 0: break;
    default: Serial.println("❌ Tidak valid"); break;
  }
}
