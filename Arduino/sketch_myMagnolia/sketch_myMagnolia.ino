#include <HX711_ADC.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// ——— HX711 CONFIG ————————————————
const int HX711_dout = 4;
const int HX711_sck  = 5;
HX711_ADC LoadCell(HX711_dout, HX711_sck);

// ——— DFPLAYER CONFIG —————————————
const int DFPLAYER_RX     = 2;  // Arduino → DFPlayer TX
const int DFPLAYER_TX     = 3;  // Arduino → DFPlayer RX
const int DFPLAYER_BUSY   = 6;  // DFPlayer BUSY pin (LOW while playing)
SoftwareSerial dfSerial(DFPLAYER_RX, DFPLAYER_TX);
DFRobotDFPlayerMini dfPlayer;

// ——— THRESHOLDS & STATE ————————————
const float thresholds[]      = { 3.0, 10.0,  40.0, 100.0};  // gram cut‑offs
const int   tracks[]          = { 1,    2,    3, 4  };  // DFPlayer track numbers
int lastTrack                = 0;     // which track has been played (0 = none)

// ——— SETTLING TIMER ——————————————
const unsigned long SETTLE_TIME_MS = 3000;  // how long to wait for weight to stabilize
bool measuring                  = false;  
unsigned long measureStartTime  = 0;
int pendingTrack                = 0;

// ——— EEPROM ADDR ————————————————
const int calVal_eepromAddress = 0;

// ——— TIMING ————————————————————————
unsigned long t = 0;

void setup() {
  Serial.begin(57600);
  delay(10);
  Serial.println("\nStarting...");

  // — HX711 setup —
  LoadCell.begin();
  float calibrationValue = 696.0;
  EEPROM.get(calVal_eepromAddress, calibrationValue);
  LoadCell.start(2000, true);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("HX711 Timeout — check wiring!");
    while (1);
  }
  LoadCell.setCalFactor(calibrationValue);
  Serial.println("HX711 Ready");

  // — DFPlayer setup —
  pinMode(DFPLAYER_RX,   INPUT_PULLUP);
  pinMode(DFPLAYER_TX,   OUTPUT);
  pinMode(DFPLAYER_BUSY, INPUT);
  dfSerial.begin(9600);
  Serial.println("Initializing DFPlayer…");
  uint8_t retries = 0;
  while (!dfPlayer.begin(dfSerial)) {
    Serial.println("  → DFPlayer init failed! Check wiring/power/SD card.");
    if (++retries >= 5) {
      Serial.println("  → Giving up after 5 tries.");
      while (1);
    }
    delay(1000);
  }
  dfPlayer.volume(20);
  Serial.println("DFPlayer Ready");
}

void loop() {
  static bool newDataReady = false;
  const int printInterval = 0;

  // — HX711 update & read —
  if (LoadCell.update()) newDataReady = true;
  if (newDataReady && millis() > t + printInterval) {
    float weight = LoadCell.getData();
    Serial.print("Weight: ");
    Serial.print(weight);
    Serial.println(" g");
    newDataReady = false;
    t = millis();

    bool isIdle = digitalRead(DFPLAYER_BUSY) == HIGH;  // BUSY=LOW while playing

    // 1) If not currently measuring and we have no lastTrack, watch for first crossing of 10 g
    if (!measuring && lastTrack == 0 && weight >= thresholds[0]) {
      measuring = true;
      measureStartTime = millis();
      Serial.println("↑ Above 10 g — waiting for 3 s to stabilize...");
    }

    // 2) If we are measuring, check for drop or timeout
    if (measuring) {
      // dropped back below 10 g? cancel
      if (weight < thresholds[0]) {
        measuring = false;
        Serial.println("↓ Dropped below 10 g — canceling settle.");
      }
      // or enough time elapsed? sample final weight and decide
      else if (millis() - measureStartTime >= SETTLE_TIME_MS) {
        float stableW = weight;
        // determine which track we actually want
        if      (stableW >= thresholds[3]) pendingTrack = tracks[3];
        else if (stableW >= thresholds[2]) pendingTrack = tracks[2];
        else if (stableW >= thresholds[1]) pendingTrack = tracks[1];
        else                                pendingTrack = tracks[0];
        Serial.print("✓ Stable at ");
        Serial.print(stableW);
        Serial.print(" g → pending track ");
        Serial.println(pendingTrack);
        measuring = false;
      }
    }

    // 3) If we have a pendingTrack and player is idle, launch it
    if (pendingTrack != 0 && pendingTrack != lastTrack && isIdle) {
      //dfPlayer.play(pendingTrack);
      dfPlayer.playMp3Folder(pendingTrack);
      Serial.print("** Playing track ");
      Serial.print(pendingTrack);
      Serial.println(" **");
      lastTrack = pendingTrack;
      pendingTrack = 0;
    }

    // 4) After playback finishes AND weight drops below first threshold, reset for next
    if (lastTrack != 0 && isIdle && weight < thresholds[0]) {
      Serial.println("Playback done & <10 g → ready for next.");
      lastTrack = 0;
    }
  }

  // — manual tare via serial (‘t’) —
  if (Serial.available() && Serial.read() == 't') {
    LoadCell.tareNoDelay();
    Serial.println("Tare commanded");
  }

  // — optional: report when tare completes —
  if (LoadCell.getTareStatus()) {
    Serial.println("Tare complete");
  }
}