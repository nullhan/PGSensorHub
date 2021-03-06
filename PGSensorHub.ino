/*
 * Power Glove UHID - Sensor Hub
 *
 * The basic Arduino example.  Turns on an LED on for one second,
 * then off for one second, and so on...  We use pin 13 because,
 * depending on your Arduino board, it has either a built-in LED
 * or a built-in resistor so that you need only an LED.
 *
 * https://github.com/nullhan/PGSensorHub
 */

#include <Keypad.h>
#include <Wire.h>
#include <LSM303.h>
#include <EEPROM.h>
#include "definitions.h"

#define DEBUG 0

// Keypad
const byte KEYPAD_ROWS = 4;
const byte KEYPAD_COLS = 6;
char PGKeys[KEYPAD_ROWS][KEYPAD_COLS] = {
  {'0','1','2','3','4',BUTTON_PROGRAM},
  {'5','6','7','8','9',BUTTON_ENTER},
  {'U','L','D','R',BUTTON_CENTER,'-'},
  {'-','-',BUTTON_SELECT,BUTTON_START,'B','A'}
};
byte rowPins[KEYPAD_ROWS] = {2, 3, 17, 16};
byte colPins[KEYPAD_COLS] = {5, 6, 7, 8, 11, 12};
Keypad pgKeypad = Keypad( makeKeymap(PGKeys), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS);
char lastPressed = '-';

// Flex sensors
int flexPins[4] = {A6,A7,A8,A9};          // Ring, Middle, Index, Thumb
const int numReadings = 20;
int flexValueReadings[4][numReadings];    // Array of last numReadings flex sensor readings
int readIndex[4] = {0,0,0,0};             // 0 -> numReadings
int flexTotal[4] = {0,0,0,0};             // the running total
int flexValue[4] = {0,0,0,0};             // the average
int flexClosed[4] = {0,0,0,0};            // default closed fist values
int flexRelaxed[4] = {0,0,0,0};           // default relaxed values

const int posTolerance = 10;              // 1 / posTolerance
int flexPosTols[4] = {0,0,0,0};           // tolerance of position (defined by posTolerance)
int flexPosture = 0;                      // 0xXXXXXXXXUUUUTIMR - LSN is important
boolean reversed[4] = {false, false, false, true};
boolean closedValuesSet, relaxedValuesSet;

// IMU
LSM303 compass;

// LED
int ledPin = 4;
int ledState = HIGH;
unsigned long blinkSpeed;
unsigned long ledTimer;

String exportString;
unsigned long lastTime;

int gloveState;

void setup() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, ledState);
  ledTimer = millis();

  Serial1.begin(115200);  // ESP serial
  Serial.begin(115200);  // USB serial

  Wire.begin();
  compass.init();
  compass.enableDefault();

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < numReadings; j++) {
      flexValueReadings[i][j] = 0;
    }
  }

  lastTime = millis();

  gloveState = MODE_SETUP;
  blinkSpeed = BLINK_FAST;
}

void loop() {
  exportString = "";

  readFlexData();
  calcFlexPosture();
  
  char pressed = pgKeypad.getKey();
  if (pressed) {
    lastPressed = pressed;
    if (pressed == BUTTON_PROGRAM && gloveState == MODE_NORMAL) {
      gloveState = MODE_SETUP;
      blinkSpeed = BLINK_FAST;
      closedValuesSet = false;
      relaxedValuesSet = false; 
    } else if (pressed == 'B' && gloveState == MODE_SETUP) {
      for (int i = 0; i < 4; i++) {
        flexRelaxed[i] = flexValue[i];
      }
      relaxedValuesSet = true;
    } else if (pressed == 'A' && gloveState == MODE_SETUP) {
      for (int i = 0; i < 4; i++) {
        flexClosed[i] = flexValue[i];
      }
      closedValuesSet = true;
    }

    if (closedValuesSet && relaxedValuesSet) {
      for (int i = 0; i < 4; i++) {
        flexPosTols[i] = (flexRelaxed[i] - flexClosed[i]) / posTolerance;
      }
      
      gloveState = MODE_NORMAL;
      blinkSpeed = BLINK_NORMAL;
    }
  }

  unsigned long nowTime = millis();
  if (nowTime - lastTime >= 50) {
    lastTime = nowTime;

    // Keypad
    if (lastPressed != BUTTON_NONE) {
      exportString += lastPressed;
      lastPressed = BUTTON_NONE;
    } else {
      exportString += BUTTON_NONE;
    }
    exportString += ',';
    
    // Flex
    switch (flexPosture) {
      case 11:  // Point
        exportString += POSTURE_POINT;
        break;
      case 15:  // Fist
        exportString += POSTURE_FIST;
        break;
      default:  // Other
        exportString += POSTURE_RELAXED;
        break;
    }
    exportString += ',';

    // IMU
    char report[19];
    compass.read();
    snprintf(report, sizeof(report), "%3d,%3d", compass.a.x/40, compass.a.y/40);
    exportString += report;

    Serial.println(exportString);

    if (gloveState == MODE_NORMAL) {
      Serial1.println(exportString);
    }
  }

  if (millis() - ledTimer >= blinkSpeed) {
    ledState = !ledState;
    digitalWrite(ledPin, ledState);
    ledTimer = millis();
  }
}

//void badValueBlink() {
//  for (int i = 0; i < 4; i++) {
//    digitalWrite(ledPin, HIGH);
//    delay(250);
//    digitalWrite(ledPin, LOW);
//    delay(250);
//  }
//}

void readFlexData() {
  // Get sensor data and average it over last numReadings
  for (int i = 0; i < 4; i++) {
    flexTotal[i] -= flexValueReadings[i][readIndex[i]];
    flexValueReadings[i][readIndex[i]] = analogRead(flexPins[i]);
    flexTotal[i] += flexValueReadings[i][readIndex[i]];

    readIndex[i]++;
    if (readIndex[i] >= numReadings) {
      readIndex[i] = 0;
    }

    flexValue[i] = flexTotal[i] / numReadings;
  }  
}

void calcFlexPosture() {
  flexPosture = 0;
  for (int i = 0; i < 4; i++) {
    if (flexValue[i] < flexClosed[i] + flexPosTols[i]) {  // flexValue[i] >= flexClosed[i] && 
      flexPosture |= 1 << i;
    } else if (flexValue[i] > flexRelaxed[i] - flexPosTols[i]) {  //flexValue[i] <= flexRelaxed[i] && 
      flexPosture &= ~(1 << i);
    } else {
      flexPosture |= 1 << i+8;
    }
  }  
}

//int getKeypadNum() {
//  boolean progEntered = false;
//  int keys[3] = {0,0,0};
//  
//  int counter = 0;
//
//  while (!progEntered) {
//    char pressed = pgKeypad.getKey();
//
//    if (pressed) {
//      if (pressed >= '0' && pressed <= '9') {      
//        keys[counter] = pressed - '0';
//        counter++;
//      } else if (pressed == BUTTON_ENTER || counter == 3) {
//        progEntered = true;
//      }
//    }
//  }
//
//  int multiplier = 100;
//  int progValue = 0;
//  for (int i = 0; i < 3; i++) {
//    progValue += keys[i] * multiplier;
//    multiplier /= 10;
//  }
//  
//  if (progValue >= 0 && progValue <= 127) {
//    return progValue;
//  } else {
//    return -1;
//  }
//}
