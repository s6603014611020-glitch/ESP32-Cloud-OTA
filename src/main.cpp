#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============================================================================
// 1. ตั้งค่า Wi-Fi และระบบ Cloud OTA (GitHub)
// ============================================================================
const char* WIFI_SSID       = "Pornpailin";
const char* WIFI_PASS       = "88102547";
const String CURRENT_VERSION = "1.0.0";

// ลิงก์เช็กเวอร์ชันจาก GitHub Repository ของคุณ
const char* VERSION_JSON_URL = "https://raw.githubusercontent.com/s6603014611020-glitch/ESP32-Cloud-OTA/main/version.json";

// ============================================================================
// 2. ตั้งค่า MQTT, Google Sheets และ LINE Alert
// ============================================================================
const char* MQTT_SERVER   = "172.20.10.7"; // IP เครื่อง Broker
const int   MQTT_PORT     = 1883;

const char* GAS_URL       = "https://script.google.com/macros/s/AKfycbzbGObmHvb0V7gH2-W12jSJhys3cRvHcV7H-gi6qs14lU65OE3q5l9dtH0vbDfLkq1u7Q/exec";
const char* LINE_TOKEN    = "5+3FW1vd6l8G5KRGPD4in4IvUtUAU0dYikZLj5XbxZDvi5vO6o3Nq3mhKI39yd8WZzeIzGytM30m8FTibjfDbg9LBrg8ylAzebWHs/XQNB6KFwH7ki6eebY5RzLW5t/NSGBBZGtg1/h/p5OKdEAMEwdB04t89/1O/w1cDnyilFU=";
const char* LINE_USER_ID  = "Uf668838b0544b30fbaedb631381e5ee5";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
volatile bool emergencyFlag = false;

// ============================================================================
// 3. ฟังก์ชันระบบ Cloud OTA (เช็กและอัปเดตเฟิร์มแวร์จาก GitHub)
// ============================================================================
void checkAndDoOTA() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("[OTA] Checking for new firmware version from GitHub...");
  HTTPClient http;
  http.begin(VERSION_JSON_URL);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String latestVersion = doc["version"].as<String>();
      String firmwareUrl  = doc["url"].as<String>();

      Serial.println("[OTA] Current Version: " + CURRENT_VERSION);
      Serial.println("[OTA] Latest Version : " + latestVersion);

      // ถ้าเวอร์ชันบน GitHub ไม่ตรงกับบอร์ด ให้เริ่มอัปเดตข้าม Cloud ทันที
      if (latestVersion != CURRENT_VERSION) {
        Serial.println("[OTA] New version found! Starting update from GitHub...");
        
        WiFiClientSecure client;
        client.setInsecure(); // ข้ามการเช็ก SSL Certificate เพื่อความสะดวกในการโหลด HTTPS

        t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);

        if (ret == HTTP_UPDATE_OK) {
          Serial.println("[OTA] Update completed successfully! Rebooting...");
        } else if (ret == HTTP_UPDATE_FAILED) {
          Serial.printf("[OTA] Update failed. Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
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

// ============================================================================
// 4. ฟังก์ชันส่งแจ้งเตือนเข้า LINE Official Account
// ============================================================================
void sendLineAlert(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://api.line.me/v2/bot/message/push");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(LINE_TOKEN));

    StaticJsonDocument<512> doc;
    doc["to"] = LINE_USER_ID;
    JsonObject msgObj = doc["messages"].createNestedObject();
    msgObj["type"] = "text";
    msgObj["text"] = message;

    String jsonString;
    serializeJson(doc, jsonString);
    http.POST(jsonString);
    http.end();
  }
}

// ============================================================================
// 5. ฟังก์ชันส่งข้อมูลบันทึกลง Google Sheets
// ============================================================================
void postToGoogleSheets(float temp, int rssi, uint32_t heap) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(GAS_URL);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["temp"] = temp;
    doc["rssi"] = rssi;
    doc["heap"] = heap;

    String jsonString;
    serializeJson(doc, jsonString);

    Serial.println("[Sheets] Sending data to Google Sheets...");
    int httpResponseCode = http.POST(jsonString);

    if (httpResponseCode > 0) {
      Serial.printf("[Sheets] Response Code: %d\n", httpResponseCode);
    } else {
      Serial.printf("[Sheets] Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  }
}

// ============================================================================
// 6. FreeRTOS Tasks
// ============================================================================

// Task 1: ตรวจจับปุ่มสัมผัสฉุกเฉินและความร้อนชิป
void TaskSensorTouch(void *pvParameters) {
  for (;;) {
    int touchValue = touchRead(4); // ขา Touch T0 (GPIO 4)
    float chipTemp = temperatureRead();

    if (touchValue < 30 || chipTemp > 80.0) {
      if (!emergencyFlag) {
        emergencyFlag = true;
        String alertMsg = "⚠️ [ESP32 System Alert]\n";
        if (touchValue < 30) alertMsg += "- ตรวจพบการสัมผัสปุ่มฉุกเฉิน (GPIO 4)\n";
        if (chipTemp > 80.0) alertMsg += "- ความร้อนชิปสูงเกินกำหนด: " + String(chipTemp, 1) + "°C\n";
        
        Serial.println("[Alert] Sending LINE Alert...");
        sendLineAlert(alertMsg);
        Serial.println("[Alert] LINE Alert Sent!");
        
        vTaskDelay(pdMS_TO_TICKS(3000));
      }
    } else {
      emergencyFlag = false;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// Task 2: ส่งข้อมูลเข้า MQTT Broker และ Google Sheets ทุกๆ 1 นาที
void TaskCloudLog(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      // เช็กการเชื่อมต่อ MQTT Broker
      if (!mqttClient.connected()) {
        Serial.println("[MQTT] Attempting connection to broker (172.20.10.4:1883)...");
        if (mqttClient.connect("ESP32_System_Client")) {
          Serial.println("[MQTT] ✅ Connected to MQTT Broker successfully!");
        } else {
          Serial.printf("[MQTT] ❌ Failed to connect, state rc = %d\n", mqttClient.state());
        }
      }

      mqttClient.loop();

      float temp = temperatureRead();
      int rssi = WiFi.RSSI();
      uint32_t heap = ESP.getFreeHeap();

      char msg[128];
      snprintf(msg, sizeof(msg), "{\"temp\":%.1f, \"rssi\":%d, \"heap\":%u}", temp, rssi, heap);
      
      // ส่งข้อมูลเข้า Topic สำหรับ Grafana
      if (mqttClient.connected()) {
        if (mqttClient.publish("esp32/system/metrics", msg)) {
          Serial.printf("[MQTT] 🚀 Publish Success! Data: %s\n", msg);
        } else {
          Serial.println("[MQTT] ⚠️ Publish Failed!");
        }
      }

      // ส่งข้อมูลบันทึกลง Google Sheets
      postToGoogleSheets(temp, rssi, heap);
    } else {
      Serial.println("[WiFi] ⚠️ Wi-Fi disconnected!");
    }

    // ทำงานซ้ำทุกๆ 1 นาที (60,000 ms)
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(60000));
  }
}

// ============================================================================
// 7. setup() และ loop()
// ============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[System] Starting ESP32...");
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());

  // 1. เช็กอัปเดต Cloud OTA จาก GitHub ทันทีเมื่อเปิดเครื่อง
  checkAndDoOTA();

  // 2. ตั้งค่า MQTT Server
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  // 3. สร้าง FreeRTOS Tasks สำหรับรันงานเบื้องหลัง
  xTaskCreatePinnedToCore(TaskSensorTouch, "SensorTouchTask", 12288, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskCloudLog,    "CloudLogTask",    8192,  NULL, 1, NULL, 1);
}

void loop() {
  // งานทั้งหมดถูกแยกไปทำใน FreeRTOS Tasks เรียบร้อยแล้ว
  vTaskDelay(pdMS_TO_TICKS(1000));
}