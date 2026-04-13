#define BLYNK_TEMPLATE_ID "TMPLXXXX"
#define BLYNK_TEMPLATE_NAME "Iot Based Water Monitoring System"

//  REPLACE BEFORE RUNNING 
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_TOKEN"
char ssid[] = "YOUR_WIFI";
char pass[] = "YOUR_PASSWORD";

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

#define TRIG D5
#define ECHO D6
#define RELAY D2

#define EMPTY_DISTANCE 19.5
#define FULL_DISTANCE 7

float ON_PERCENT  = 20;
float OFF_PERCENT = 80;

#define DRY_RUN_MIN_LEVEL 10
#define DRY_RUN_CHECK_TIME 10000
#define DRY_RUN_MIN_CHANGE 1

bool pumpState = false;
bool manualMode = false;
bool dryRunLock = false;

int pumpCycles = 0;

float percentage = 0;
float lastDisplayedLevel = -1;

unsigned long lastReadTime = 0;

unsigned long runtimeStart = 0;
unsigned long totalRuntime = 0;

float previousPercentage = 0;
unsigned long previousTime = 0;
float fillSpeed = 0;
float estimatedTimeLeft = 0;

float dryRunStartLevel = 0;
unsigned long dryRunCheckStart = 0;

// ================= SENSOR =================
float readDistance() {
  float total = 0;
  int validReadings = 0;

  for (int i = 0; i < 5; i++) {
    digitalWrite(TRIG, LOW);
    delayMicroseconds(2);

    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);

    long duration = pulseIn(ECHO, HIGH, 30000);

    if (duration > 0) {
      float d = duration * 0.034 / 2;
      if (d > 2 && d < 400) {
        total += d;
        validReadings++;
      }
    }
    delay(20);
  }

  if (validReadings == 0) return 0;
  return total / validReadings;
}

bool isValidDistance(float d) {
  return (d > 1 && d < 50);
}

// ================= LEVEL PROCESSING =================
float processLevel(float distance) {
  float p = map(distance, EMPTY_DISTANCE, FULL_DISTANCE, 0, 100);
  p = constrain(p, 0, 100);

  if (p < 3) p = 0;

  static float filtered = 0;
  filtered = filtered * 0.85 + p * 0.15;

  static float lastValid = 0;
  if (abs(filtered - lastValid) > 25) {
    filtered = lastValid;
  }

  lastValid = filtered;
  return filtered;
}

// ================= PUMP CONTROL =================
void startPump(String reason) {
  pumpState = true;
  digitalWrite(RELAY, HIGH);

  pumpCycles++;
  runtimeStart = millis();

  dryRunStartLevel = percentage;
  dryRunCheckStart = millis();

  previousPercentage = percentage;
  previousTime = millis();
  fillSpeed = 0;

  Serial.println("Pump started: " + reason);
}

void stopPump(String reason) {
  pumpState = false;
  digitalWrite(RELAY, LOW);

  totalRuntime += (millis() - runtimeStart) / 1000;
  runtimeStart = 0;

  Serial.println("Pump stopped: " + reason);
}

// ================= AUTO MODE =================
void handleAutoMode() {
  if (manualMode || dryRunLock) return;

  if (!pumpState && percentage < ON_PERCENT) {
    startPump("AUTO");
  }

  if (pumpState && percentage >= OFF_PERCENT) {
    stopPump("THRESHOLD");
  }
}

// ================= DRY RUN =================
void handleDryRun() {
  if (!pumpState) return;

  if (millis() - dryRunCheckStart > DRY_RUN_CHECK_TIME) {
    float levelIncrease = percentage - dryRunStartLevel;

    if (levelIncrease < DRY_RUN_MIN_CHANGE) {
      dryRunLock = true;
      Blynk.virtualWrite(V10, 1);

      stopPump("DRY RUN");

      Blynk.logEvent("dry_run", "Water not increasing");
    }
  }
}

// ================= RUNTIME =================
void updateRuntime(unsigned long &currentRuntime) {
  if (pumpState) {
    currentRuntime = (millis() - runtimeStart) / 1000;
  } else {
    currentRuntime = 0;
  }
}

// ================= ESTIMATION =================
void calculateEstimation() {
  unsigned long now = millis();

  float levelChange = percentage - previousPercentage;
  float timeChange = (now - previousTime) / 1000.0;

  if (timeChange > 0.5 && levelChange > 0) {
    float newSpeed = levelChange / timeChange;

    if (newSpeed > 0 && newSpeed < 10) {
      fillSpeed = (fillSpeed * 0.7) + (newSpeed * 0.3);
    }
  }

  float remaining = OFF_PERCENT - percentage;

  if (pumpState && fillSpeed > 0.01 && remaining > 0)
    estimatedTimeLeft = remaining / fillSpeed;
  else
    estimatedTimeLeft = 0;

  previousPercentage = percentage;
  previousTime = now;
}

// ================= BLYNK =================
void sendToBlynk(unsigned long currentRuntime) {
  if (lastDisplayedLevel < 0 || abs(percentage - lastDisplayedLevel) >= 2) {
    lastDisplayedLevel = percentage;
    Blynk.virtualWrite(V0, percentage);
  }

  Blynk.virtualWrite(V2, pumpCycles);
  Blynk.virtualWrite(V3, pumpState);
  Blynk.virtualWrite(V8, currentRuntime);
  Blynk.virtualWrite(V9, totalRuntime);
  Blynk.virtualWrite(V4, (int)estimatedTimeLeft);
  Blynk.virtualWrite(V10, dryRunLock);
}

// ================= DEBUG =================
void debugSerial(unsigned long currentRuntime) {
  Serial.print("Mode: ");
  Serial.print(manualMode ? "MANUAL" : "AUTO");

  Serial.print(" | Level: ");
  Serial.print(percentage);

  Serial.print("% | Pump: ");
  Serial.print(pumpState ? "ON" : "OFF");

  Serial.print(" | Runtime: ");
  Serial.print(currentRuntime);

  Serial.print(" | Total: ");
  Serial.print(totalRuntime);

  Serial.print(" | DryRun: ");
  Serial.print(dryRunLock ? "YES" : "NO");

  Serial.println();
}

// ================= BLYNK HANDLERS =================
BLYNK_CONNECTED() {
  Blynk.syncVirtual(V5);
  Blynk.syncVirtual(V1);
  Blynk.syncVirtual(V6);
  Blynk.syncVirtual(V7);
}

BLYNK_WRITE(V5) {
  manualMode = param.asInt();

  if (manualMode && pumpState) {
    stopPump("MANUAL MODE");
  }
}

BLYNK_WRITE(V1) {
  if (!manualMode) return;

  bool newState = param.asInt();

  if (!pumpState && newState && percentage >= DRY_RUN_MIN_LEVEL) {
    dryRunLock = false;
    Blynk.virtualWrite(V10, 0);
    startPump("MANUAL");
  }

  if (pumpState && !newState) {
    stopPump("MANUAL STOP");
  }
}

BLYNK_WRITE(V6) {
  float newOn = param.asFloat();
  if (newOn < OFF_PERCENT - 5) ON_PERCENT = newOn;
  Blynk.virtualWrite(V6, ON_PERCENT);
}

BLYNK_WRITE(V7) {
  float newOff = param.asFloat();
  if (newOff > ON_PERCENT + 5) OFF_PERCENT = newOff;
  Blynk.virtualWrite(V7, OFF_PERCENT);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(RELAY, OUTPUT);

  digitalWrite(RELAY, LOW);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();

  previousTime = millis();
}

// ================= LOOP =================
void loop() {
  Blynk.run();

  if (millis() - lastReadTime > 1000) {
    lastReadTime = millis();

    float d = readDistance();
    if (!isValidDistance(d)) return;

    percentage = processLevel(d);

    handleAutoMode();
    handleDryRun();

    unsigned long currentRuntime;
    updateRuntime(currentRuntime);

    calculateEstimation();
    sendToBlynk(currentRuntime);
    debugSerial(currentRuntime);
  }
}
