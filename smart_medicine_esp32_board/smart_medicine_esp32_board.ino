#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <ArduinoJson.h>
#include <vector>

#define LIMIT_SWITCH 23
#define LED 22
#define BUZZER 21

const char* ssid = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

const char* serverURL = "url";
const char* scheduleURL = "url/sched";

const char* deviceID = "box_1";

// NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;
const int daylightOffset_sec = 0;

// ALARM STRUCT
struct Alarm {
  int hour;
  int minute;
  bool done;
};

std::vector<Alarm> alarms;

// ACTIVE TRACKING
int activeAlarmIndex = -1;
int lastTriggeredMinute = -1;

// FLAGS
bool alarmActive = false;
bool alreadyTaken = false;
bool isOpen = false;

unsigned long openStartTime = 0;

// SNOOZE
int snoozeCount = 0;
int maxSnooze = 2;
unsigned long alarmStartMillis = 0;

// LED
unsigned long lastBlink = 0;
bool ledState = false;

// TIME
int lastDay = -1;

// TIMERS
unsigned long lastFetch = 0;

// WIFI RECONNECT (NON-BLOCKING)
void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting WiFi...");
    WiFi.begin(ssid, password);
  }
}

// FETCH SCHEDULE
void fetchSchedule() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(scheduleURL);

    int code = http.GET();

    if (code == 200) {
      String payload = http.getString();
      Serial.println("Schedule JSON:");
      Serial.println(payload);

      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.println("JSON Parse Failed!");
        return;
      }

      alarms.clear();

      for (JsonObject obj : doc.as<JsonArray>()) {
        Alarm a;
        a.hour = obj["hour"];
        a.minute = obj["minute"];
        a.done = false;

        alarms.push_back(a);

        Serial.printf("Loaded Alarm: %02d:%02d\n", a.hour, a.minute);
      }

      if (alarms.size() == 0) {
        Serial.println("No alarms loaded!");
      }

    } else {
      Serial.print("GET Failed: ");
      Serial.println(code);
    }

    http.end();
  }
}

// SEND DATA
void sendToServer(String status, int snooze, String timeStr) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{\"device_id\":\"" + String(deviceID) +
                      "\",\"status\":\"" + status +
                      "\",\"time\":\"" + timeStr +
                      "\",\"snooze\":" + String(snooze) +
                      ",\"alarmIndex\":" + String(activeAlarmIndex) + "}";

    Serial.println("Sending:");
    Serial.println(jsonData);

    int code = http.POST(jsonData);

    Serial.print("Response: ");
    Serial.println(code);

    http.end();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(2000);

  fetchSchedule();
}

void loop() {
  ensureWiFi();

  // Refresh schedule every 5 mins
  if (millis() - lastFetch > 300000) {
    fetchSchedule();
    lastFetch = millis();
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int h = timeinfo.tm_hour;
  int m = timeinfo.tm_min;
  int d = timeinfo.tm_mday;

  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", h, m);

  Serial.printf("Time: %s\n", timeStr);

  // DAILY RESET
  if (d != lastDay) {
    Serial.println("New Day Reset");
    lastDay = d;

    for (int i = 0; i < alarms.size(); i++) {
      alarms[i].done = false;
    }
  }

  // TRIGGER ALARM
  if (!alarmActive) {
    for (int i = 0; i < alarms.size(); i++) {
      if (!alarms[i].done &&
          h == alarms[i].hour &&
          m == alarms[i].minute &&
          m != lastTriggeredMinute) {

        Serial.println("ALARM #" + String(i) + " START");

        lastTriggeredMinute = m;
        alarmActive = true;
        activeAlarmIndex = i;

        alarmStartMillis = millis();
        snoozeCount = 0;
        alreadyTaken = false;

        break;
      }
    }
  }

  // ALARM OUTPUT
  if (alarmActive) {
    if (snoozeCount == 0) {
      digitalWrite(LED, HIGH);
    } else {
      if (millis() - lastBlink > 500) {
        ledState = !ledState;
        digitalWrite(LED, ledState);
        lastBlink = millis();
      }
    }
    tone(BUZZER, 1000);
  }

  // LIMIT SWITCH
  if (digitalRead(LIMIT_SWITCH) == HIGH) {
    if (!isOpen) {
      isOpen = true;
      openStartTime = millis();
    }

    if ((millis() - openStartTime > 3000) && alarmActive && !alreadyTaken) {
      Serial.println("TAKEN");

      alarmActive = false;
      alreadyTaken = true;

      if (activeAlarmIndex >= 0 && activeAlarmIndex < alarms.size()) {
        alarms[activeAlarmIndex].done = true;
      }

      digitalWrite(LED, LOW);
      noTone(BUZZER);

      sendToServer("taken", snoozeCount, String(timeStr));
    }

  } else {
    isOpen = false;
  }

  // SNOOZE + MISSED
  if (alarmActive && !alreadyTaken) {
    unsigned long elapsed = millis() - alarmStartMillis;

    if (snoozeCount < maxSnooze &&
        elapsed >= (snoozeCount + 1) * 600000) {

      snoozeCount++;
      Serial.println("SNOOZE #" + String(snoozeCount));
    }

    if (elapsed >= (maxSnooze + 1) * 600000) {
      Serial.println("MISSED");

      alarmActive = false;

      if (activeAlarmIndex >= 0 && activeAlarmIndex < alarms.size()) {
        alarms[activeAlarmIndex].done = true;
      }

      digitalWrite(LED, LOW);
      noTone(BUZZER);

      sendToServer("missed", snoozeCount, String(timeStr));
    }
  }

  delay(200);
}
