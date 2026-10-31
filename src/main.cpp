#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// 1. ใส่ชื่อ Wi-Fi และรหัสผ่านของคุณ
const char* ssid = "Pornpailin";
const char* password = "88102547";
const String CURRENT_VERSION = "1.0.0"; 

// 2. ลิงก์ Raw URL ของไฟล์ version.json บน GitHub ของคุณ
const char* VERSION_JSON_URL = "https://raw.githubusercontent.com/s6603014611020-glitch/ESP32-Cloud-OTA/main/version.json";

void checkAndDoOTA() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("[OTA] Checking for new firmware version...");
  HTTPClient http;
  http.begin(VERSION_JSON_URL);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // แปลงข้อมูล JSON (แก้ไขรองรับ ArduinoJson v6)
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String latestVersion = doc["version"].as<String>();
      String firmwareUrl  = doc["url"].as<String>();

      Serial.println("[OTA] Current Version: " + CURRENT_VERSION);
      Serial.println("[OTA] Latest Version : " + latestVersion);

      // ถ้าเวอร์ชันบน Cloud ใหม่กว่า ให้สั่งอัปเดตทันที
      if (latestVersion != CURRENT_VERSION) {
        Serial.println("[OTA] New version found! Starting update...");
        
        WiFiClientSecure client;
        client.setInsecure(); // ข้ามการตรวจสอบ SSL Certificate เพื่อความสะดวก

        t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);

        switch (ret) {
          case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Update failed. Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;
          case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No updates available.");
            break;
          case HTTP_UPDATE_OK:
            Serial.println("[OTA] Update successfully completed! Rebooting...");
            break;
        }
      } else {
        Serial.println("[OTA] Firmware is up to date.");
      }
    }
  } else {
    Serial.printf("[OTA] Failed to fetch version.json, HTTP code: %d\n", httpCode);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  // เช็กการอัปเดตทันทีเมื่อเชื่อมต่อสำเร็จ
  checkAndDoOTA();
}

void loop() {
  delay(10000);
}