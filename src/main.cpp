#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoJson.h>

// --- ตั้งค่าข้อมูลเครือข่ายและ Cloud Services ---
const char* WIFI_SSID   = "Pornpailin";         // ใส่ชื่อ Wi-Fi
const char* WIFI_PASS   = "88102547";     // ใส่รหัสผ่าน Wi-Fi

const char* MQTT_SERVER = "172.20.10.7";    // MQTT Broker (Week 5)
const int   MQTT_PORT   = 1883;

const char* GAS_URL     = "https://script.google.com/macros/s/AKfycbzbGObmHvb0V7gH2-W12jSJhys3cRvHcV7H-gi6qs14lU65OE3q5l9dtH0vbDfLkq1u7Q/exec"; // URL จากขั้นตอนที่ 1 (Week 10)
const char* LINE_TOKEN  = "5+3FW1vd6l8G5KRGPD4in4IvUtUAU0dYikZLj5XbxZDvi5vO6o3Nq3mhKI39yd8WZzeIzGytM30m8FTibjfDbg9LBrg8ylAzebWHs/XQNB6KFwH7ki6eebY5RzLW5t/NSGBBZGtg1/h/p5OKdEAMEwdB04t89/1O/w1cDnyilFU="; // Token จากขั้นตอนที่ 2 (Week 6)
const char* LINE_USER_ID= "Uf668838b0544b30fbaedb631381e5ee5";

WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

volatile bool emergencyFlag = false;

// --- ฟังก์ชันส่งแจ้งเตือนเข้า LINE Official Account ---
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

// --- ฟังก์ชันส่งข้อมูลลง Google Sheets ---
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

// --- FreeRTOS Tasks ---
void TaskSensorTouch(void *pvParameters) {
  for (;;) {
    int touchValue = touchRead(4); // ขาพินสัมผัส T0 (GPIO 4)
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

// Task 2: ส่งข้อมูลเข้า MQTT และ Google Sheets (พร้อมคำสั่ง Debug MQTT)
void TaskCloudLog(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      // 🔍 เช็กและลองเชื่อมต่อ MQTT Broker
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
      
      // 🔍 เช็กผลการส่ง Publish ข้อมูลเข้า Topic
      if (mqttClient.connected()) {
        if (mqttClient.publish("esp32/system/metrics", msg)) {
          Serial.printf("[MQTT] 🚀 Publish Success! Data: %s\n", msg);
        } else {
          Serial.println("[MQTT] ⚠️ Publish Failed!");
        }
      } else {
        Serial.println("[MQTT] ⚠️ Cannot publish: Not connected to broker.");
      }

      // ส่งข้อมูลบันทึกลง Google Sheets
      postToGoogleSheets(temp, rssi, heap);
    } else {
      Serial.println("[WiFi] ⚠️ Wi-Fi disconnected!");
    }

    // ทำงานซ้ำทุก ๆ 1 นาที (60,000 ms)
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(60000));
  }
}

// --- Local Web OTA Setup ---
void setupWebOTA() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html",
      "<h2>ESP32 Local Web OTA Update</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update'>"
      "<input type='submit' value='Upload Firmware'>"
      "</form>");
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "Update Failed" : "Update Success! Rebooting...");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) Serial.printf("Update Success: %u B\n", upload.totalSize);
    }
  });

  server.begin();
  MDNS.begin("esp32");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[System] Starting ESP32...");
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  setupWebOTA();

  // สร้าง FreeRTOS Tasks
  xTaskCreatePinnedToCore(TaskSensorTouch, "SensorTouchTask", 12288, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskCloudLog,     "CloudLogTask",    8192, NULL, 1, NULL, 1);
}

void loop() {
  server.handleClient();
  vTaskDelay(pdMS_TO_TICKS(10));
}