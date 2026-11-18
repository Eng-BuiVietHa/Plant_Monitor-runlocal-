#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <DHTesp.h>

// ==========================================
// 1. CẤU HÌNH PHẦN CỨNG & TÊN MIỀN
// ==========================================
const int PIN_DHT11 = 4;   // Chân D4 nối chân DATA của DHT11
const int PIN_RELAY = 18;  // Chân D18 (GPIO 18) nối chân IN của Relay

#define RELAY_ON  LOW
#define RELAY_OFF HIGH

const char* AP_SSID = "TuoiCay_ESP32";
const char* AP_PASS = "12345678";

// Tên miền tùy chỉnh thay cho 192.168.4.1 -> Truy cập: http://tuoicay.local
const char* MDNS_NAME     = "tuoicay";
const char* CUSTOM_DOMAIN = "tuoicay.local";

// Ngưỡng tự động khi chọn chế độ "Tự động"
const float TEMP_THRESHOLD = 30.0;
const float HUMI_THRESHOLD = 60.0;

DHTesp dht;
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

// ==========================================
// 2. BIẾN TRẠNG THÁI & ĐỒNG HỒ NỘI BỘ
// ==========================================
String currentMode = "off";
float temperature = 0.0;
float humidity = 0.0;

// Cấu hình hẹn giờ (được lưu bền vững trong NVS Flash)
int startHour = 6;
int startMin = 30;
int endHour = 7;
int endMin = 0;
bool timerEnabled = true;

// Trạng thái đồng bộ thời gian (luôn reset về false khi mất điện/khởi động lại)
bool isTimeSynced = false;
int currentHour = 0;
int currentMin = 0;
int currentSec = 0;
unsigned long lastSecondTick = 0;
unsigned long lastSensorRead = 0;

int extractJsonInt(const String& json, const char* key, int defaultVal) {
  String pattern = String("\"") + key + "\":";
  int idx = json.indexOf(pattern);
  if (idx == -1) return defaultVal;
  return json.substring(idx + pattern.length()).toInt();
}

void setRelay(bool turnOn) {
  digitalWrite(PIN_RELAY, turnOn ? RELAY_ON : RELAY_OFF);
}

// ==========================================
// 3. LƯU & KHÔI PHỤC CÀI ĐẶT KHI MẤT ĐIỆN (NVS)
// ==========================================
void loadSettingsFromNVS() {
  prefs.begin("plant-cfg", true); // Mở chế độ chỉ đọc
  currentMode  = prefs.getString("mode", "off");
  startHour    = prefs.getInt("sHour", 6);
  startMin     = prefs.getInt("sMin", 30);
  endHour      = prefs.getInt("eHour", 7);
  endMin       = prefs.getInt("eMin", 0);
  timerEnabled = prefs.getBool("tEn", true);
  prefs.end();

  Serial.printf("Da nap cai dat tu Flash: Mode=%s | Hen gio=%02d:%02d->%02d:%02d (%s)\n",
                currentMode.c_str(), startHour, startMin, endHour, endMin,
                timerEnabled ? "BAT" : "TAT");
}

void saveTimerToNVS() {
  prefs.begin("plant-cfg", false); // Mở chế độ ghi
  prefs.putInt("sHour", startHour);
  prefs.putInt("sMin", startMin);
  prefs.putInt("eHour", endHour);
  prefs.putInt("eMin", endMin);
  prefs.putBool("tEn", timerEnabled);
  prefs.end();
}

void saveModeToNVS() {
  prefs.begin("plant-cfg", false);
  prefs.putString("mode", currentMode);
  prefs.end();
}

// ==========================================
// 4. LOGIC KIỂM TRA HẸN GIỜ & ĐIỀU KHIỂN RELAY
// ==========================================
bool isWithinTimerWindow() {
  // Nếu chưa đồng bộ giờ chuẩn sau khi có điện lại -> NGẮT hẹn giờ
  if (!isTimeSynced || !timerEnabled) {
    return false;
  }

  int nowMins   = currentHour * 60 + currentMin;
  int startMins = startHour * 60 + startMin;
  int endMins   = endHour * 60 + endMin;

  if (startMins == endMins) return false;
  if (startMins < endMins) {
    return (nowMins >= startMins && nowMins < endMins);
  } else {
    return (nowMins >= startMins || nowMins < endMins);
  }
}

void readDHT11() {
  TempAndHumidity data = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE && !isnan(data.temperature) && !isnan(data.humidity)) {
    temperature = data.temperature;
    humidity = data.humidity;
  }
}

void updateRelayLogic() {
  if (currentMode == "cool") {
    setRelay(true);
  } 
  else if (isWithinTimerWindow()) {
    setRelay(true);
  } 
  else if (currentMode == "auto") {
    bool shouldRun = (temperature >= TEMP_THRESHOLD) || (humidity <= HUMI_THRESHOLD);
    setRelay(shouldRun);
  } 
  else {
    setRelay(false);
  }
}

// ==========================================
// 5. KHỞI TẠO HỆ THỐNG, TÊN MIỀN & WEB SERVER
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);

  dht.setup(PIN_DHT11, DHTesp::DHT11);

  // Khôi phục lịch hẹn giờ đã cài trước khi mất điện (nhưng isTimeSynced vẫn là false)
  loadSettingsFromNVS();

  if (!LittleFS.begin(true)) {
    Serial.println("Loi khoi tao LittleFS!");
    return;
  }

  // Phát Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress apIP = WiFi.softAPIP();

  // Cấu hình tên miền tùy chỉnh thay cho 192.168.4.1
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
  }
  // Chuyển hướng DNS mọi tên miền nội bộ về IP của ESP32 (hỗ trợ tốt cả Android & iOS)
  dnsServer.start(53, "*", apIP);

  Serial.printf("San sang! Truy cap: http://%s hoac http://%s\n",
                CUSTOM_DOMAIN, apIP.toString().c_str());

  server.serveStatic("/", LittleFS, "/");

  // API đồng bộ trạng thái & nhận giờ chuẩn từ điện thoại
  server.on("/api/status", HTTP_GET, []() {
    if (server.hasArg("h") && server.hasArg("m") && server.hasArg("s")) {
      currentHour = server.arg("h").toInt();
      currentMin  = server.arg("m").toInt();
      currentSec  = server.arg("s").toInt();
      lastSecondTick = millis();

      if (!isTimeSynced) {
        isTimeSynced = true;
        Serial.printf("Da dong bo gio chuan: %02d:%02d:%02d -> Khoi phuc hen gio!\n",
                      currentHour, currentMin, currentSec);
        updateRelayLogic();
      }
    }

    char json[210];
    snprintf(json, sizeof(json),
             "{\"temp\":%.1f,\"humi\":%.0f,\"mode\":\"%s\",\"synced\":%s,"
             "\"timer\":{\"startHour\":%d,\"startMin\":%d,\"endHour\":%d,\"endMin\":%d,\"enabled\":%s}}",
             temperature, humidity, currentMode.c_str(),
             isTimeSynced ? "true" : "false",
             startHour, startMin, endHour, endMin, timerEnabled ? "true" : "false");
    server.send(200, "application/json", json);
  });

  // API đổi chế độ
  server.on("/api/mode", HTTP_POST, []() {
    if (server.hasArg("set")) {
      currentMode = server.arg("set");
      saveModeToNVS();
      updateRelayLogic();
    }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // API lưu cài đặt hẹn giờ mới vào Flash
  server.on("/api/timer", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      String body = server.arg("plain");
      startHour = extractJsonInt(body, "startHour", startHour);
      startMin  = extractJsonInt(body, "startMin", startMin);
      endHour   = extractJsonInt(body, "endHour", endHour);
      endMin    = extractJsonInt(body, "endMin", endMin);
      timerEnabled = (body.indexOf("\"enabled\":true") != -1);

      saveTimerToNVS();
      updateRelayLogic();
      Serial.printf("Da luu hen gio vao Flash: %02d:%02d -> %02d:%02d (%s)\n",
                    startHour, startMin, endHour, endMin,
                    timerEnabled ? "BAT" : "TAT");
    }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  // Bộ đếm thời gian độc lập trên ESP32 (duy trì giờ chuẩn sau khi ngắt kết nối điện thoại)
  if (isTimeSynced && (millis() - lastSecondTick >= 1000)) {
    lastSecondTick += 1000;
    currentSec++;
    if (currentSec >= 60) {
      currentSec = 0;
      currentMin++;
      if (currentMin >= 60) {
        currentMin = 0;
        currentHour = (currentHour + 1) % 24;
      }
      // Kiểm tra lại lịch hẹn giờ mỗi khi sang phút mới
      updateRelayLogic();
    }
  }

  // Đọc cảm biến DHT11 mỗi 2 giây
  if (millis() - lastSensorRead >= 2000) {
    lastSensorRead = millis();
    readDHT11();
    updateRelayLogic();
  }
}