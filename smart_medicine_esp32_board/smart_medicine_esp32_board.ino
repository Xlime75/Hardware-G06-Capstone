#include <WiFi.h>
#include <HTTPClient.h>

#define LIMIT_SWITCH 23
#define LED 22
#define BUZZER 21

const char* ssid = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

// backend API endpoint
const char* serverURL = "url";

bool alarmActive = true;

void setup() {
  Serial.begin(115200);

  pinMode(LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // to connect to wiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi!");
}

void loop() {
  int switchState = digitalRead(LIMIT_SWITCH);

  if (alarmActive) {
    digitalWrite(LED, HIGH);
    tone(BUZZER, 1000);
  }

  if (switchState == LOW) {
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

    delay(3000);
  }

  delay(200);
}
