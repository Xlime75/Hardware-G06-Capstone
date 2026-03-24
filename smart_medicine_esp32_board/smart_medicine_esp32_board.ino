#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

#define LIMIT_SWITCH 23
#define LED 22
#define BUZZER 21

const char* ssid = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

const char* serverURL = "url";

// NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;
const int daylightOffset_sec = 0;

// BASE ALARM
int alarmHour = 7;
int alarmMinute = 0;

// SYSTEM FLAGS
bool alarmActive = false;
bool alarmTriggered = false;
bool alreadyTaken = false;
bool isOpen = false;

unsigned long openStartTime = 0;

// SNOOZE SYSTEM
int snoozeCount = 0;
int maxSnooze = 2;
int snoozeInterval = 10; // minutes

int currentAlarmHour;
int currentAlarmMinute;

void setup() {
  Serial.begin(115200);

  pinMode(LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(2000);

  // initialize first alarm
  currentAlarmHour = alarmHour;
  currentAlarmMinute = alarmMinute;
}

void loop() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) return;

  int h = timeinfo.tm_hour;
  int m = timeinfo.tm_min;
  int s = timeinfo.tm_sec;

  Serial.printf("Time: %02d:%02d:%02d\n", h, m, s);

  // TRIGGER ALARM / SNOOZE
  if (h == currentAlarmHour && m == currentAlarmMinute && !alarmTriggered) {
    Serial.println("ALARM TRIGGERED");
    alarmActive = true;
    alarmTriggered = true;
    alreadyTaken = false;
  }

  // RESET trigger when minute passes
  if (m != currentAlarmMinute) {
    alarmTriggered = false;
  }

  // ALARM OUTPUT
  if (alarmActive) {
    digitalWrite(LED, HIGH);
    tone(BUZZER, 1000);
  }

  // LIMIT SWITCH (OPEN = HIGH)
  if (digitalRead(LIMIT_SWITCH) == HIGH) {

    if (!isOpen) {
      isOpen = true;
      openStartTime = millis();
    }

    // TAKEN (after 3 sec open)
    if ((millis() - openStartTime > 3000) && alarmActive && !alreadyTaken) {
      Serial.println("MEDICINE TAKEN");

      alarmActive = false;
      alreadyTaken = true;
      snoozeCount = 0;

      digitalWrite(LED, LOW);
      noTone(BUZZER);

      sendToServer("taken");
    }

  } else {
    isOpen = false;
  }

  // MISSED / SNOOZE LOGIC
  if (alarmActive && !alreadyTaken) {

    // if 1 minute passed after alarm trigger
    static int lastCheckedMinute = -1;

    if (m != lastCheckedMinute) {
      lastCheckedMinute = m;

      // if still not taken → process snooze/missed
      if (snoozeCount < maxSnooze) {

        snoozeCount++;

        Serial.println("SNOOZE TRIGGERED");

        // compute next alarm
        currentAlarmMinute += snoozeInterval;

        if (currentAlarmMinute >= 60) {
          currentAlarmMinute -= 60;
          currentAlarmHour = (currentAlarmHour + 1) % 24;
        }

        alarmActive = false;

      } else {
        // MISSED
        Serial.println("MEDICINE MISSED");

        alarmActive = false;
        snoozeCount = 0;

        sendToServer("missed");

        // reset to original schedule next day/use
        currentAlarmHour = alarmHour;
        currentAlarmMinute = alarmMinute;
      }
    }
  }

  delay(500);
}

// SEND FUNCTION
void sendToServer(String status) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{\"status\":\"" + status + "\"}";

    int code = http.POST(jsonData);

    Serial.print("Response: ");
    Serial.println(code);

    http.end();
  }
}
