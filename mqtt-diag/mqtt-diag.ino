/*
 * ============================================================
 *  Kontrol LED ESP32-S3 via MQTT over TLS
 *  Kelas 08A / Kelompok 05
 * ============================================================
 *
 *  Broker default : broker.emqx.io:8883 (TLS)
 *  Dashboard web  : index.html (WSS 8084)
 *
 *  Skema topik (HARUS sama dengan index.html):
 *    <base>/led/26/set          "0" / "1"   (dari web)
 *    <base>/led/27/set          "0" / "1"   (dari web)
 *    <base>/led/26/status       "0" / "1"   (retained)
 *    <base>/led/27/status       "0" / "1"   (retained)
 *    <base>/esp32/status        online/offline (retained + LWT)
 *    <base>/esp32/info          JSON, retained saat connect
 *    <base>/esp32/heartbeat     JSON, tiap 15 detik
 *
 *  Fitur:
 *    - State LED disimpan di NVS, tahan reboot/listrik mati.
 *    - Retained command: perintah terakhir langsung diterapkan saat boot.
 *    - LWT "offline" + heartbeat untuk indikator online di dashboard.
 *    - Reconnect WiFi/MQTT otomatis tanpa memblokir loop terus-menerus.
 *    - Diagnosis error TLS di Serial.
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <time.h>
#include <esp_system.h>

#ifndef RGB_BUILTIN
#error "RGB_BUILTIN tidak tersedia. Pilih board ESP32-S3 yang punya NeoPixel onboard."
#endif

// ===== VERSI =====
const char* FW_VERSION = "1.1.0";

// ===== KREDENSIAL WIFI =====
// Disimpan di secrets.h yang tidak ikut repository publik.
// Salin secrets.example.h menjadi secrets.h, lalu isi.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #warning "secrets.h tidak ditemukan. Salin secrets.example.h menjadi secrets.h."
  #define WIFI_SSID "GANTI_NAMA_WIFI"
  #define WIFI_PASS "GANTI_PASSWORD"
#endif

// ===== MQTT OVER TLS =====
const char* MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 8883;

// Kosongkan bila broker tidak memakai autentikasi.
const char* MQTT_USER = "";
const char* MQTT_PASS = "";

// ===== TOPIK =====
// Samakan nilai ini dengan kolom "Topik dasar" di index.html.
#define TOPIC_BASE "kelas-08A/05"

static const char* TOPIC_SET_26    = TOPIC_BASE "/led/26/set";
static const char* TOPIC_SET_27    = TOPIC_BASE "/led/27/set";
static const char* TOPIC_STATUS_26 = TOPIC_BASE "/led/26/status";
static const char* TOPIC_STATUS_27 = TOPIC_BASE "/led/27/status";
static const char* TOPIC_STATUS    = TOPIC_BASE "/esp32/status";
static const char* TOPIC_INFO      = TOPIC_BASE "/esp32/info";
static const char* TOPIC_HEARTBEAT = TOPIC_BASE "/esp32/heartbeat";

// ===== ROOT CA EMQX =====
// DigiCert Global Root G2 (diverifikasi 2026: *.emqx.io ->
// RapidSSL TLS RSA CA G1 -> DigiCert Global Root G2)
const char* ROOT_CA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

// ===== KLIEN =====
WiFiClientSecure espClient;
PubSubClient client(espClient);
Preferences prefs;

String clientId;

// ===== STATUS LED =====
int stateLed26 = 0;
int stateLed27 = 0;

const uint8_t BRIGHTNESS = 80;

// ===== TIMER =====
unsigned long lastMQTTAttempt = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastTimeLog = 0;
unsigned long lastHeartbeat = 0;

bool wifiWasConnected = false;
bool mqttWasConnected = false;


// ============================================================
//  NEO PIXEL
// ============================================================
void updateRGBLed() {
  int r = 0, g = 0, b = 0;

  if (stateLed26 == 1 && stateLed27 == 0) {
    b = BRIGHTNESS;                 // 26 saja  -> biru
  } else if (stateLed26 == 0 && stateLed27 == 1) {
    r = BRIGHTNESS;                 // 27 saja  -> merah
  } else if (stateLed26 == 1 && stateLed27 == 1) {
    g = BRIGHTNESS;                 // keduanya -> hijau
  }

  rgbLedWrite(RGB_BUILTIN, r, g, b);

  Serial.printf("[RGB] LED26=%d LED27=%d | R=%d G=%d B=%d\n",
                stateLed26, stateLed27, r, g, b);
}

void testNeoPixel() {
  Serial.println("[TEST] NeoPixel: merah - hijau - biru");

  rgbLedWrite(RGB_BUILTIN, 100, 0, 0);
  delay(350);
  rgbLedWrite(RGB_BUILTIN, 0, 100, 0);
  delay(350);
  rgbLedWrite(RGB_BUILTIN, 0, 0, 100);
  delay(350);
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);

  Serial.println("[TEST] NeoPixel selesai");
}


// ============================================================
//  PENYIMPANAN STATE (NVS)
// ============================================================
void loadState() {
  prefs.begin("ledstate", true);
  stateLed26 = prefs.getUChar("led26", 0) ? 1 : 0;
  stateLed27 = prefs.getUChar("led27", 0) ? 1 : 0;
  prefs.end();

  Serial.printf("[NVS] State tersimpan: 26=%d 27=%d\n",
                stateLed26, stateLed27);
}

void saveState() {
  prefs.begin("ledstate", false);
  prefs.putUChar("led26", (uint8_t)stateLed26);
  prefs.putUChar("led27", (uint8_t)stateLed27);
  prefs.end();
}


// ============================================================
//  PUBLISH HELPER
// ============================================================
void publishStatus(int kanal) {
  const char* topik = (kanal == 26) ? TOPIC_STATUS_26 : TOPIC_STATUS_27;
  int nilai = (kanal == 26) ? stateLed26 : stateLed27;
  bool ok = client.publish(topik, nilai ? "1" : "0", true);

  Serial.printf("[PUBLISH] %s = %d | %s\n", topik, nilai, ok ? "OK" : "GAGAL");
}

void publishInfo() {
  char ip[24];
  WiFi.localIP().toString().toCharArray(ip, sizeof(ip));

  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"ip\":\"%s\",\"rssi\":%d,\"heap\":%u,\"uptime\":%lu,\"fw\":\"%s\"}",
           ip, (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap(),
           millis() / 1000UL, FW_VERSION);

  bool ok = client.publish(TOPIC_INFO, buf, true);
  Serial.printf("[PUBLISH] info perangkat | %s\n", ok ? "OK" : "GAGAL");
}

void publishHeartbeat() {
  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"uptime\":%lu,\"rssi\":%d,\"heap\":%u}",
           millis() / 1000UL, (int)WiFi.RSSI(),
           (unsigned)ESP.getFreeHeap());

  if (!client.publish(TOPIC_HEARTBEAT, buf, false)) {
    Serial.println("[PUBLISH] heartbeat GAGAL");
  }
  lastHeartbeat = millis();
}


// ============================================================
//  UBAH LED
// ============================================================
void setLed(int kanal, int nilai) {
  int* state = nullptr;

  if (kanal == 26) {
    state = &stateLed26;
  } else if (kanal == 27) {
    state = &stateLed27;
  } else {
    return;
  }

  if (*state != nilai) {
    *state = nilai;
    saveState();
  }

  updateRGBLed();
  publishStatus(kanal);
}


// ============================================================
//  CALLBACK MQTT
// ============================================================
void callback(char* topic, byte* payload, unsigned int length) {
  if (length == 0 || length > 16) {
    Serial.printf("[MQTT] Payload tidak valid (panjang %u)\n", length);
    return;
  }

  String pesan;
  pesan.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    pesan += (char)payload[i];
  }
  pesan.trim();

  Serial.println();
  Serial.println("=== PESAN MQTT DITERIMA ===");
  Serial.printf("Topic  : %s\n", topic);
  Serial.printf("Payload: %s\n", pesan.c_str());

  if (pesan != "0" && pesan != "1") {
    Serial.println("[MQTT] Diabaikan: payload harus 0 atau 1");
    return;
  }

  int nilai = pesan.toInt();

  if (strcmp(topic, TOPIC_SET_26) == 0) {
    setLed(26, nilai);
  } else if (strcmp(topic, TOPIC_SET_27) == 0) {
    setLed(27, nilai);
  } else {
    Serial.println("[MQTT] Diabaikan: topik tidak dikenal");
  }
}


// ============================================================
//  KONEKSI MQTT TLS
// ============================================================
void connectMQTT() {
  Serial.printf("\n[MQTTS] Menghubungkan ke %s:%u ...\n", MQTT_HOST, MQTT_PORT);

  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                        TOPIC_STATUS, 1, true, "offline");
  } else {
    ok = client.connect(clientId.c_str(),
                        TOPIC_STATUS, 1, true, "offline");
  }

  if (!ok) {
    Serial.printf("[MQTTS] GAGAL | state=%d\n", client.state());

    char err[160];
    int tlsErr = espClient.lastError(err, sizeof(err));
    if (tlsErr != 0) {
      Serial.printf("[TLS] error=%d: %s\n", tlsErr, err);
    }
    return;
  }

  mqttWasConnected = true;
  Serial.println("[MQTTS] Terhubung");

  bool onlineOk = client.publish(TOPIC_STATUS, "online", true);
  Serial.printf("[PUBLISH] status online | %s\n", onlineOk ? "OK" : "GAGAL");

  bool sub26 = client.subscribe(TOPIC_SET_26, 1);
  bool sub27 = client.subscribe(TOPIC_SET_27, 1);

  Serial.printf("[SUBSCRIBE] %s | %s\n", TOPIC_SET_26, sub26 ? "OK" : "GAGAL");
  Serial.printf("[SUBSCRIBE] %s | %s\n", TOPIC_SET_27, sub27 ? "OK" : "GAGAL");

  // Beri kesempatan retained command diproses dulu, baru kirim status.
  unsigned long mulai = millis();
  while (millis() - mulai < 400 && client.connected()) {
    client.loop();
    delay(5);
  }

  publishStatus(26);
  publishStatus(27);
  publishInfo();
  publishHeartbeat();
}


// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" ESP32-S3 LED MQTT (TLS 8883)");
  Serial.println(" Kelas 08A / Kelompok 05");
  Serial.printf(" Firmware v%s\n", FW_VERSION);
  Serial.println("==============================");
  Serial.printf("[BOOT] Reset reason: %d\n", (int)esp_reset_reason());

  loadState();
  testNeoPixel();
  updateRGBLed();

  uint64_t chip = ESP.getEfuseMac();
  char id[40];
  snprintf(id, sizeof(id), "esp32-08A-05-%04X%08X",
           (unsigned)((chip >> 32) & 0xFFFF),
           (unsigned)(chip & 0xFFFFFFFF));
  clientId = id;
  Serial.printf("[MQTT] clientId: %s\n", clientId.c_str());

  espClient.setCACert(ROOT_CA);
  espClient.setHandshakeTimeout(15);

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);
  client.setKeepAlive(20);
  client.setSocketTimeout(10);
  client.setBufferSize(512);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("esp32-08A-05");
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("[WIFI] Menghubungkan ke \"%s\"...\n", WIFI_SSID);
}


// ============================================================
//  LOOP
// ============================================================
void loop() {
  // ---------- WiFi ----------
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      wifiWasConnected = false;
      mqttWasConnected = false;
      if (client.connected()) {
        client.disconnect();
      }
      Serial.println("[WIFI] Terputus, menunggu koneksi...");
    }

    if (millis() - lastWiFiAttempt >= 10000UL) {
      lastWiFiAttempt = millis();
      Serial.printf("[WIFI] Mencoba ulang (status=%d)...\n", (int)WiFi.status());
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }

    delay(20);
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    Serial.printf("[WIFI] Terhubung | IP=%s | RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.println("[NTP] Sinkronisasi waktu...");

    lastTimeLog = 0;
    lastMQTTAttempt = 0;
  }

  // ---------- NTP ----------
  // TLS butuh waktu valid untuk validasi sertifikat.
  time_t now = time(nullptr);
  if (now < 1700000000) {
    if (millis() - lastTimeLog >= 5000UL) {
      lastTimeLog = millis();
      Serial.printf("[NTP] Menunggu waktu valid... (sekarang %ld)\n", (long)now);
    }
    delay(20);
    return;
  }

  // ---------- MQTT ----------
  if (!client.connected()) {
    if (mqttWasConnected) {
      mqttWasConnected = false;
      Serial.println("[MQTTS] Koneksi terputus");
    }

    if (lastMQTTAttempt == 0 || millis() - lastMQTTAttempt >= 5000UL) {
      lastMQTTAttempt = millis();
      connectMQTT();
    }
  } else {
    client.loop();

    if (millis() - lastHeartbeat >= 15000UL) {
      publishHeartbeat();
    }
  }

  delay(10);
}
