#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <ArduinoJson.h>
#include <vector>

#define LIMIT_SWITCH 23
#define LED 22
#define BUZZER 25

const char* ssid = "Xlime";
const char* password = "Asher1234";

const char* serverURL = "http://0.0.0.0:3001";
const char* scheduleURL = "http://0.0.0.0:3001/sched";

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

// STATE
int activeAlarmIndex = -1;
int lastTriggeredMinute = -1;

bool alarmActive = false;
bool buzzerOn = false;
bool alreadyTaken = false;
bool isOpen = false;

// 🔥 NEW SAFETY FLAGS
bool systemArmed = false;         // prevents boot buzzer trigger
unsigned long bootTime = 0;       // stabilization timer

unsigned long buzzerStart = 0;
unsigned long cycleStart = 0;
int currentPhase = 0;

int snoozeCount = 0;
int maxSnooze = 2;

int lastDay = -1;
unsigned long lastFetch = 0;

// ---------------- WIFI ----------------
void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
  }
}

// ---------------- FETCH ----------------
void fetchSchedule() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(scheduleURL);

  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();

    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, payload)) {

      alarms.clear();

      for (JsonObject obj : doc.as<JsonArray>()) {
        Alarm a;
        a.hour = obj["hour"];
        a.minute = obj["minute"];
        a.done = false;
        alarms.push_back(a);
      }
    }
  }

  http.end();
}

// ---------------- SERVER ----------------
void sendToServer(String status, int snooze, String timeStr) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");

  String jsonData =
    "{\"device_id\":\"" + String(deviceID) +
    "\",\"status\":\"" + status +
    "\",\"time\":\"" + timeStr +
    "\",\"snooze\":" + String(snooze) +
    ",\"alarmIndex\":" + String(activeAlarmIndex) + "}";

  http.POST(jsonData);
  http.end();
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // 🔴 HARD BLOCK: prevent boot buzzer glitch
  digitalWrite(BUZZER, LOW);
  digitalWrite(LED, LOW);

  bootTime = millis();  // start stabilization timer
  systemArmed = false;  // system not ready yet

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  fetchSchedule();
}

// ---------------- LOOP ----------------
void loop() {
  ensureWiFi();

  unsigned long now = millis();

  // 🔥 SYSTEM ARM DELAY (CRITICAL FIX)
  // prevents random boot buzzer activation
  if (!systemArmed) {
    if (now - bootTime > 3000) { // 3 seconds safety window
      systemArmed = true;
      digitalWrite(BUZZER, LOW);
    } else {
      digitalWrite(BUZZER, LOW);
      return; // block everything until stable
    }
  }

  // 🔒 HARD SAFETY LOCK (always OFF unless explicitly ON)
  if (!alarmActive && !buzzerOn) {
    digitalWrite(BUZZER, LOW);
  }

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

  // DAILY RESET
  if (d != lastDay) {
    lastDay = d;
    for (auto &a : alarms) a.done = false;
  }

  // ---------------- TRIGGER ----------------
  if (!alarmActive && systemArmed) {

    for (int i = 0; i < alarms.size(); i++) {
      if (!alarms[i].done &&
          h == alarms[i].hour &&
          m == alarms[i].minute &&
          m != lastTriggeredMinute) {

        alarmActive = true;
        activeAlarmIndex = i;

        buzzerOn = true;
        buzzerStart = now;
        cycleStart = now;

        currentPhase = 0;
        snoozeCount = 0;
        alreadyTaken = false;

        lastTriggeredMinute = m;
        break;
      }
    }
  }

  // ---------------- BUZZER PATTERN ----------------
  if (alarmActive && buzzerOn && systemArmed) {

    unsigned long cycle = (now - cycleStart) % 1000;

    if (cycle < 300) {
      digitalWrite(BUZZER, HIGH);
    } else {
      digitalWrite(BUZZER, LOW);
    }

    if (now - buzzerStart >= 60000) {
      buzzerOn = false;
      digitalWrite(BUZZER, LOW);
    }
  }

  // ---------------- SNOOZE ----------------
  if (alarmActive && !buzzerOn && systemArmed) {

    unsigned long waitTime = (currentPhase < 2) ? 300000 : 0;

    if (now - buzzerStart >= waitTime + 60000) {
      currentPhase++;

      if (currentPhase <= 2) {
        buzzerOn = true;
        buzzerStart = now;
        cycleStart = now;
      } else {

        alarmActive = false;
        digitalWrite(BUZZER, LOW);
        digitalWrite(LED, LOW);

        if (activeAlarmIndex >= 0 && activeAlarmIndex < alarms.size()) {
          alarms[activeAlarmIndex].done = true;
        }

        sendToServer("missed", currentPhase, String(timeStr));
      }
    }
  }

  // ---------------- LIMIT SWITCH ----------------
  if (digitalRead(LIMIT_SWITCH) == HIGH) {
    if (!isOpen) isOpen = true;

    if (alarmActive && !alreadyTaken && (now - buzzerStart > 3000)) {

      alarmActive = false;
      alreadyTaken = true;

      digitalWrite(BUZZER, LOW);
      digitalWrite(LED, LOW);

      if (activeAlarmIndex >= 0 && activeAlarmIndex < alarms.size()) {
        alarms[activeAlarmIndex].done = true;
      }

      sendToServer("taken", snoozeCount, String(timeStr));
    }

  } else {
    isOpen = false;
  }

  // ---------------- LED ----------------
  digitalWrite(LED, buzzerOn ? HIGH : LOW);

  delay(200);
}
