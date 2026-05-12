#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <ArduinoJson.h>
#include <vector>

#define LIMIT_SWITCH 23
#define LED 22
#define BUZZER 25

const char* ssid = "Bading kayo";
const char* password = "123456789";

const char* scheduleURL = "http://10.59.230.75:3000/api/esp32/sched";

const char* deviceID = "box_1";

// TIME
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;
const int daylightOffset_sec = 0;

// ---------------- ALARM STRUCT ----------------
struct Alarm {
  int hour;
  int minute;
  bool done;
};

std::vector<Alarm> alarms;

// ---------------- STATE ----------------
bool alarmActive = false;
bool buzzerOn = false;
bool systemArmed = true;

int activeAlarmIndex = -1;
int lastTriggeredMinute = -1;

int currentPhase = 0;
unsigned long buzzerStart = 0;
unsigned long cycleStart = 0;

unsigned long lastFetch = 0;
int lastDay = -1;

// ---------------- BUTTON STATE ----------------
unsigned long pressStart = 0;
bool pressing = false;
bool holdTriggered = false;

// ---------------- SELF TEST ----------------
void selfTest() {
  Serial.println("Running self-test...");

  for (int i = 0; i < 3; i++) {
    digitalWrite(LED, HIGH);
    digitalWrite(BUZZER, HIGH);
    delay(150);
    digitalWrite(LED, LOW);
    digitalWrite(BUZZER, LOW);
    delay(150);
  }
}

// ---------------- WIFI ----------------
void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }

  Serial.println("\nConnected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting WiFi...");
    connectWiFi();
  }
}

// ---------------- FETCH SCHEDULE ----------------
void fetchSchedule() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(scheduleURL);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  Serial.println("\nFetching schedule...");

  int code = http.GET();
  Serial.print("HTTP Code: ");
  Serial.println(code);

  if (code == 200) {
    String payload = http.getString();

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.c_str());
      http.end();
      return;
    }

    alarms.clear();

    for (JsonObject obj : doc.as<JsonArray>()) {
      Alarm a;
      a.hour = obj["hour"] | 0;
      a.minute = obj["minute"] | 0;
      a.done = false;

      alarms.push_back(a);

      Serial.print("Loaded alarm: ");
      Serial.print(a.hour);
      Serial.print(":");
      if (a.minute < 10) Serial.print("0");
      Serial.println(a.minute);
    }

    Serial.print("Total alarms: ");
    Serial.println(alarms.size());

  } else {
    Serial.println("Failed to fetch schedule.");
  }

  http.end();
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(BUZZER, LOW);
  digitalWrite(LED, LOW);

  selfTest();
  connectWiFi();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  Serial.println("Waiting for time sync...");
  delay(2000);

  fetchSchedule();
}

// ---------------- LOOP ----------------
void loop() {
  ensureWiFi();

  unsigned long now = millis();

  // refresh schedule
  if (now - lastFetch > 30000) {
    fetchSchedule();
    lastFetch = now;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int h = timeinfo.tm_hour;
  int m = timeinfo.tm_min;
  int d = timeinfo.tm_mday;

  // reset daily
  if (d != lastDay) {
    lastDay = d;
    for (auto &a : alarms) a.done = false;
  }

  // ---------------- TRIGGER ----------------
  if (!alarmActive && systemArmed) {
    for (int i = 0; i < alarms.size(); i++) {
      if (!alarms[i].done &&
          h == alarms[i].hour &&
          abs(m - alarms[i].minute) <= 1 &&
          m != lastTriggeredMinute) {

        Serial.println("ALARM TRIGGERED!");

        alarmActive = true;
        activeAlarmIndex = i;

        buzzerOn = true;
        buzzerStart = now;
        cycleStart = now;

        lastTriggeredMinute = m;
        break;
      }
    }
  }

  // ---------------- BUZZER ----------------
  if (alarmActive && buzzerOn) {
    unsigned long cycle = (now - cycleStart) % 1000;
    digitalWrite(BUZZER, (cycle < 300) ? HIGH : LOW);

    if (now - buzzerStart >= 60000) {
      buzzerOn = false;
      digitalWrite(BUZZER, LOW);
    }
  }

  // ---------------- SNOOZE ----------------
  if (alarmActive && !buzzerOn) {
    if (now - buzzerStart >= 300000) {

      currentPhase++;

      if (currentPhase <= 2) {
        Serial.println("Snooze triggered");

        buzzerOn = true;
        buzzerStart = now;
        cycleStart = now;

      } else {
        Serial.println("Missed medicine");

        alarmActive = false;
        buzzerOn = false;
        digitalWrite(BUZZER, LOW);
        digitalWrite(LED, LOW);

        if (activeAlarmIndex >= 0) {
          alarms[activeAlarmIndex].done = true;
        }
      }
    }
  }

  // ---------------- BUTTON RELEASE HOLD ----------------
bool buttonReleased = digitalRead(LIMIT_SWITCH) == HIGH;

if (buttonReleased) {

  if (!pressing) {
    pressing = true;
    pressStart = now;
    holdTriggered = false;
  }

  unsigned long releaseTime = now - pressStart;

  if (!holdTriggered && releaseTime >= 3000) {
    holdTriggered = true;

    Serial.println("Medicine taken");

    alarmActive = false;
    buzzerOn = false;
    currentPhase = 0;

    digitalWrite(BUZZER, LOW);
    digitalWrite(LED, LOW);

    if (activeAlarmIndex >= 0) {
      alarms[activeAlarmIndex].done = true;
    }
  }

} else {
  pressing = false;
  holdTriggered = false;
}

  digitalWrite(LED, buzzerOn ? HIGH : LOW);

  delay(50);
}
