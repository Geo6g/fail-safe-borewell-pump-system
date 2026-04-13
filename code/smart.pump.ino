#define BLYNK_TEMPLATE_ID "TMPL3CQAAdlZK"
#define BLYNK_TEMPLATE_NAME "smart borewell management system"
#define BLYNK_AUTH_TOKEN "w6NfwIQ2N7hA8nzPylMgRV-HjMSzB8zV"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

char ssid[] = "Hi";
char pass[] = "12345678";

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
float distance = 0;

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



BLYNK_CONNECTED() {

  Serial.println("Syncing dashboard state...");

  Blynk.syncVirtual(V5); 
  Blynk.syncVirtual(V1); 
  Blynk.syncVirtual(V6); 
  Blynk.syncVirtual(V7);
}



// MODE SWITCH
BLYNK_WRITE(V5) {

  manualMode = param.asInt();

  if (manualMode) {

    Serial.println("Mode changed: MANUAL MODE");

    if (pumpState) {

      pumpState = false;
      digitalWrite(RELAY, LOW);

      totalRuntime += (millis() - runtimeStart) / 1000;
      runtimeStart = 0;

      Serial.println("Pump stopped due to MANUAL mode override");
    }

  } else {

    Serial.println("Mode changed: AUTO MODE");
  }
}



// MANUAL PUMP BUTTON
BLYNK_WRITE(V1) {

  if (manualMode) {

    bool newState = param.asInt();

    if (!pumpState && newState && percentage >= DRY_RUN_MIN_LEVEL) {

      dryRunLock = false;
      Blynk.virtualWrite(V10,0);

      pumpState = true;
      digitalWrite(RELAY, HIGH);

      pumpCycles++;

      runtimeStart = millis();

      dryRunStartLevel = percentage;
      dryRunCheckStart = millis();

      previousPercentage = percentage;
      previousTime = millis();
      fillSpeed = 0;

      Serial.println("Pump started manually");
    }

    if (pumpState && !newState) {

      pumpState = false;
      digitalWrite(RELAY, LOW);

      totalRuntime += (millis() - runtimeStart) / 1000;
      runtimeStart = 0;

      Serial.println("Pump stopped manually");
    }
  }
}



// START THRESHOLD
BLYNK_WRITE(V6) {

  float newOn = param.asFloat();

  if (newOn < OFF_PERCENT - 5) {
    ON_PERCENT = newOn;
  }

  Blynk.virtualWrite(V6, ON_PERCENT);
}



// STOP THRESHOLD
BLYNK_WRITE(V7) {

  float newOff = param.asFloat();

  if (newOff > ON_PERCENT + 5) {
    OFF_PERCENT = newOff;
  }

  Blynk.virtualWrite(V7, OFF_PERCENT);
}



void setup() {

  Serial.begin(115200);
  delay(2000);

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



void loop() {

  Blynk.run();

  if (millis() - lastReadTime > 1000) {

    lastReadTime = millis();

    distance = readDistance();

    if (distance > 1 && distance < 50) {

      percentage = map(distance, EMPTY_DISTANCE, FULL_DISTANCE, 0, 100);
      percentage = constrain(percentage, 0, 100);

      if (percentage < 3) percentage = 0;

      static float filteredPercentage = 0;
      filteredPercentage = filteredPercentage * 0.85 + percentage * 0.15;
      percentage = filteredPercentage;

      // SPIKE FILTER
      static float lastValidLevel = 0;

      if (abs(percentage - lastValidLevel) > 25) {
        percentage = lastValidLevel;
      }

      lastValidLevel = percentage;



// AUTO MODE
      if (!manualMode && !dryRunLock) {

        if (!pumpState && percentage < ON_PERCENT) {

          pumpState = true;
          digitalWrite(RELAY, HIGH);

          pumpCycles++;

          runtimeStart = millis();

          dryRunStartLevel = percentage;
          dryRunCheckStart = millis();

          previousPercentage = percentage;
          previousTime = millis();
          fillSpeed = 0;

          Serial.println("Pump started automatically");
        }

        if (pumpState && percentage >= OFF_PERCENT) {

          pumpState = false;
          digitalWrite(RELAY, LOW);

          totalRuntime += (millis() - runtimeStart) / 1000;
          runtimeStart = 0;

          Serial.println("Pump stopped at stop threshold");
        }
      }



// DRY RUN DETECTION
      if (pumpState) {

        if (millis() - dryRunCheckStart > DRY_RUN_CHECK_TIME) {

          float levelIncrease = percentage - dryRunStartLevel;

          if (levelIncrease < DRY_RUN_MIN_CHANGE) {

            dryRunLock = true;
            Blynk.virtualWrite(V10,1);

            pumpState = false;
            digitalWrite(RELAY, LOW);

            totalRuntime += (millis() - runtimeStart) / 1000;
            runtimeStart = 0;

            Serial.println("DRY RUN detected - Pump stopped");

            Blynk.logEvent("dry_run", "Pump running but water not increasing.");
          }
        }
      }



// CURRENT RUNTIME
      unsigned long currentRuntime = 0;

      if (pumpState) {
        currentRuntime = (millis() - runtimeStart) / 1000;
      }



// ESTIMATED TIME
      unsigned long now = millis();

      float levelChange = percentage - previousPercentage;
      float timeChange = (now - previousTime) / 1000.0;

      if (timeChange > 0.5 && levelChange > 0) {

        float newSpeed = levelChange / timeChange;

        if (newSpeed > 0 && newSpeed < 10) {

          fillSpeed = (fillSpeed * 0.7) + (newSpeed * 0.3);
        }
      }

      float remainingPercent = OFF_PERCENT - percentage;

      if (pumpState && fillSpeed > 0.01 && remainingPercent > 0)
        estimatedTimeLeft = remainingPercent / fillSpeed;
      else
        estimatedTimeLeft = 0;



      previousPercentage = percentage;
      previousTime = now;



// SEND DATA
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



// SERIAL DEBUG
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
  }
}