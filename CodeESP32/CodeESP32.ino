#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>

#include "webControlESP.h"
#include "secrets.h" // copy secrets.example.h -> secrets.h

// Pins
#define LED 2
#define TRIGGER_PIN 0       // BOOT button: short press = show IP, hold 3 s = erase Wi-Fi
#define RAIN_SENSOR_PIN 33
#define RX_PIN 22           // from HC-05 TXD
#define TX_PIN 21           // to HC-05 RXD
#define GPS Serial2

// Settings
const int RAIN_WET_THRESHOLD = 3600;                              // rain sensor reading <= this counts as wet (0-4095)
const unsigned long WEATHER_CHECK_INTERVAL_MS = 10UL * 60 * 1000; // 10 minutes
const uint32_t DEFAULT_MAX_RUN_SECONDS = 600;                     // used until a max time is set on the dashboard
const uint32_t MAX_RUN_SECONDS_LIMIT = 24UL * 60 * 60;            // largest max time the dashboard accepts
const int SETUP_PORTAL_TIMEOUT_S = 30;
const int BUTTON_PORTAL_TIMEOUT_S = 120;

SoftwareSerial HC05(RX_PIN, TX_PIN);
TinyGPSPlus gps;
WiFiManager wm;
WebServer server(80);
Preferences prefs;

bool pumpOn = false;
uint32_t pumpedSeconds = 0;
uint32_t maxRunSeconds = DEFAULT_MAX_RUN_SECONDS;
unsigned long lastPumpTick = 0;
unsigned long lastWeatherCheck = 0;

void connectWiFi();
void blinkSuccess();
void blinkFail();
void checkButton();
void setPump(bool on);
void updatePumpTimer();
void checkWeather();
bool fetchHeavyRain(double latitude, double longitude);
bool hasHeavyRain(const String& weatherJson);
void sendStatus();
void handlePump();
void handleReset();
void handleMaxTime();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  analogReadResolution(12);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(LED, OUTPUT);

  prefs.begin("floodbarrier", false);
  maxRunSeconds = prefs.getUInt("maxRun", DEFAULT_MAX_RUN_SECONDS);

  // Make sure the pump is off before Wi-Fi setup, which can block and restart the board
  HC05.begin(9600);
  setPump(false);

  connectWiFi();

  GPS.begin(9600);

  server.on("/", [] {
    server.send(200, "text/html", webpage);
  });
  server.on("/status", sendStatus);
  server.on("/pump", handlePump);
  server.on("/reset", handleReset);
  server.on("/maxtime", handleMaxTime);
  server.begin();

  Serial.println("Server start");

  setPump(false); // again, in case the Bluetooth link wasn't up yet at boot
}

void loop() {
  checkButton();

  while (GPS.available()) {
    gps.encode(GPS.read());
  }

  checkWeather();
  updatePumpTimer();
  server.handleClient();
  delay(10);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.println("\n Starting");

  std::vector<const char *> menu = {"wifi", "info", "sep", "restart", "exit"};
  wm.setMenu(menu);
  wm.setClass("invert");
  wm.setConfigPortalTimeout(SETUP_PORTAL_TIMEOUT_S);

  if (wm.autoConnect("Set Up Transmitter", SETUP_AP_PASSWORD)) { // password protected ap
    Serial.print("WiFi connected! IP Address: ");
    Serial.println(WiFi.localIP());
    blinkSuccess();
  } else {
    Serial.println("Failed to connect or hit timeout");
    blinkFail();
    ESP.restart();
  }
}

void blinkSuccess() {
  digitalWrite(LED, HIGH);
  delay(250);
  digitalWrite(LED, LOW);
  delay(250);
  digitalWrite(LED, HIGH);
  delay(250);
  digitalWrite(LED, LOW);
  delay(250);
}

void blinkFail() {
  digitalWrite(LED, HIGH);
  delay(1000);
  digitalWrite(LED, LOW);
  delay(500);
  digitalWrite(LED, HIGH);
  delay(1000);
  digitalWrite(LED, LOW);
  delay(500);
}

void checkButton() {
  if (digitalRead(TRIGGER_PIN) != LOW) return;
  delay(50); // debounce
  if (digitalRead(TRIGGER_PIN) != LOW) return;

  // The portal below blocks the loop (no run-time cutoff), so stop the pump first
  setPump(false);

  Serial.println("Button Pressed");
  delay(3000);
  if (digitalRead(TRIGGER_PIN) == LOW) {
    Serial.println("Button Held: erasing Wi-Fi settings, restarting");
    wm.resetSettings();
    ESP.restart();
  }

  Serial.println("Starting config portal");
  wm.setConfigPortalTimeout(BUTTON_PORTAL_TIMEOUT_S);
  if (wm.startConfigPortal("Check IP Address")) {
    Serial.println("WiFi connected!");
    blinkSuccess();
  } else {
    Serial.println("Config portal closed or hit timeout");
    blinkFail();
  }
}

// Every pump on/off goes through here, so the pump and the dashboard always agree.
void setPump(bool on) {
  if (on && pumpedSeconds >= maxRunSeconds) {
    Serial.println("Max run time reached, press RESET to pump again");
    on = false;
  }
  if (on && !pumpOn) lastPumpTick = millis();

  pumpOn = on;
  HC05.write(on ? '1' : '0');
  Serial.println(on ? "Pump ON" : "Pump OFF");
}

void updatePumpTimer() {
  if (!pumpOn) return;

  // Catch up whole seconds even if the loop was blocked (e.g. by a weather request)
  while (millis() - lastPumpTick >= 1000) {
    pumpedSeconds++;
    lastPumpTick += 1000;
  }

  if (pumpedSeconds >= maxRunSeconds) {
    pumpedSeconds = maxRunSeconds;
    setPump(false);
  }
}

void checkWeather() {
  if (!gps.location.isValid()) return; // need a GPS fix first

  // Check right after the first fix, then every WEATHER_CHECK_INTERVAL_MS
  if (lastWeatherCheck != 0 && millis() - lastWeatherCheck < WEATHER_CHECK_INTERVAL_MS) return;
  lastWeatherCheck = millis();

  bool heavyRain = fetchHeavyRain(gps.location.lat(), gps.location.lng());
  bool sensorWet = analogRead(RAIN_SENSOR_PIN) <= RAIN_WET_THRESHOLD;
  Serial.printf("Weather check: heavy rain = %d, sensor wet = %d\n", heavyRain, sensorWet);

  if (heavyRain && sensorWet) {
    setPump(true);
  }
}

bool fetchHeavyRain(double latitude, double longitude) {
  String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(latitude, 6)
             + "&lon=" + String(longitude, 6) + "&appid=" + OWM_API_KEY;

  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, url)) {
    Serial.println("Weather request: unable to connect");
    return false;
  }

  int httpCode = http.GET();
  String body = (httpCode == HTTP_CODE_OK) ? http.getString() : String();
  http.end();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Weather request failed: %d\n", httpCode);
    return false;
  }
  return hasHeavyRain(body);
}

// True if any reported condition is heavy rain. OpenWeatherMap condition codes
// (https://openweathermap.org/weather-conditions): 202 thunderstorm with heavy rain,
// 502 heavy intensity rain, 503 very heavy rain, 504 extreme rain, 522 heavy intensity shower rain
bool hasHeavyRain(const String& weatherJson) {
  // The response contains e.g. "weather":[{"id":502,"main":"Rain",...},{"id":701,...}]
  int start = weatherJson.indexOf("\"weather\":[");
  if (start < 0) return false;
  int end = weatherJson.indexOf(']', start);
  if (end < 0) return false;

  String conditions = weatherJson.substring(start, end);
  for (int i = conditions.indexOf("\"id\":"); i >= 0; i = conditions.indexOf("\"id\":", i + 1)) {
    int id = conditions.substring(i + 5).toInt();
    if (id == 202 || id == 502 || id == 503 || id == 504 || id == 522) return true;
  }
  return false;
}

// Example output: {"pumpOn":true,"pumpedSeconds":100,"maxRunSeconds":600}
void sendStatus() {
  String json = String("{\"pumpOn\":") + (pumpOn ? "true" : "false")
              + ",\"pumpedSeconds\":" + pumpedSeconds
              + ",\"maxRunSeconds\":" + maxRunSeconds + "}";
  server.send(200, "application/json", json);
}

// /pump?state=on or /pump?state=off
void handlePump() {
  String state = server.arg("state");
  if (state == "on") {
    setPump(true);
  } else if (state == "off") {
    setPump(false);
  }
  sendStatus();
}

void handleReset() {
  setPump(false);
  pumpedSeconds = 0;
  sendStatus();
}

// /maxtime?seconds=600
void handleMaxTime() {
  long seconds = server.arg("seconds").toInt();
  if (seconds < 1 || seconds > (long)MAX_RUN_SECONDS_LIMIT) {
    server.send(400, "text/plain", "seconds must be between 1 and 86400");
    return;
  }
  maxRunSeconds = seconds;
  prefs.putUInt("maxRun", maxRunSeconds); // saved to flash, survives power cuts
  sendStatus();
}
