#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

#define LIMIT_SWITCH 23
#define LED 22
#define BUZZER 21

const char* ssid = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

// Backend API
const char* serverURL = "url";

// NTP Config
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600; // Philippines
const int daylightOffset_sec = 0;

// Alarm time (will get the time from the system later)
int alarmHour = 7;
int alarmMinute = 0;

bool alarmActive = false;
bool alarmTriggered = false;

void setup() {
  Serial.begin(115200);

  pinMode(LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // To connect to wiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi!");

  // NTP INIT
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Syncing time...");
  delay(2000);
}

void loop() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
    return;
  }

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  int currentSecond = timeinfo.tm_sec;

  Serial.printf("Time: %02d:%02d:%02d\n", currentHour, currentMinute, currentSecond);

  // Alarm logic
  if (currentHour == alarmHour && currentMinute == alarmMinute) {
    if (!alarmTriggered) {
      Serial.println("ALARM STARTED");
      alarmActive = true;
      alarmTriggered = true;
    }
  } else {
    alarmTriggered = false; // reset for next trigger
  }

  // Alarm
  if (alarmActive) {
    digitalWrite(LED, HIGH);
    tone(BUZZER, 1000);
  }

  // Medicine taken (limit switch)
  if (digitalRead(LIMIT_SWITCH) == LOW && alarmActive) {
    Serial.println("Medicine Taken!");

    alarmActive = false;
    digitalWrite(LED, LOW);
    noTone(BUZZER);

    // send data to backend
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;

      http.begin(serverURL);
      http.addHeader("Content-Type", "application/json");

      String jsonData = "{\"status\":\"taken\"}";

      int httpResponseCode = http.POST(jsonData);

      Serial.print("Response Code: ");
      Serial.println(httpResponseCode);

      http.end();
    }

    delay(3000); // debounce / cooldown
  }

  delay(1000);
}
