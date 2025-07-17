#include <Arduino.h>
#include "SPI.h"
#include "SD.h"
#include "U8g2lib.h"
#include "FastLED.h"
/*
for pinouts detail, refer to https://github.com/makerbase-mks/MKS-MINI12864-V3
*/
// EXP1
#define BEEPER 2
#define BTN_ENC 0 
#define LCD_EN 33 // LCD_CS
#define LCD_RS 27 // LCD_DC
#define LCD_D4 21 // LCD_RST
#define LCD_D5 32 // WS2811 data line
// EXP2
#define SPI_MISO 19 // SD_MISO
#define SPI_SCK 18 // SPI_CLK
#define BTN_EN1 34
#define SPI_CS 5 // SD_CS
#define BTN_EN2 35
#define SPI_MOSI 23
#define SPI_DET 4 // SD detect switch (low = inserted)
// instructors
U8G2_ST7567_ENH_DG128064I_F_4W_HW_SPI u8g2(U8G2_R2, LCD_EN, LCD_RS); // ST7567-based displays (MINI 12864)
// U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, LCD_D4, LCD_EN, LCD_RS); // ST7920-based displays (RAMPS 12864)
CRGB leds[3];
// variables
String statusMessage = "ready";
unsigned long looptime0 = 0;
// beeper variables
unsigned long beeperRef = 0;
int beeperCountTrack = 0, beeperInterval, beeperCount;
bool beeperState = false;
// button variables
String buttonState; // will flag shortbutton or longbutton
const int longButtonThreshold = 300; // in ms
unsigned long buttonReference;
bool buttonCheck;
// encoder variables
String encoderRotation; // will flag CW or CCW
int encoderRate = 3; // encoderCount increment based on rotation speed (1 = linear)
volatile int encoderCount;
// sd card variables
bool sdReady = false; // true when SD card is properly setup
/*
simple beeper function
non-blocking with adjustable beep count and interval
use callBeeper() to start a beeping sequence
Only compatible with active beeper, displays with passive beeper need to use tone() instead
*/
void callBeeper(int interval, int count) { // runs once, sets the beeping interval and counts
  beeperRef = millis();
  beeperInterval = interval;
  beeperCount = count;
  beeperCountTrack = 0;
  beeperState = true;
}
// runs in the main loop 
void beeper() {
  unsigned long currentMillis = millis();
  if (currentMillis - beeperRef >= beeperInterval) {
    beeperRef = currentMillis;
    beeperState = !beeperState; // toggle the beeper state
    if(beeperState) beeperCountTrack++;
    if(beeperCountTrack >= beeperCount) beeperState = false;
  }
  digitalWrite(BEEPER, beeperState ? HIGH : LOW);
}
/*
simple button detect function
assigns "shortpress" or "longpress" on the variable buttonState
*/
void buttonISR() { // called by interrupt
  if (!digitalRead(BTN_ENC) && !buttonCheck) { // time sampling when button is pressed
    buttonReference = millis();
    buttonCheck = true;
  }
  if (digitalRead(BTN_ENC) && buttonCheck) { // if button is released and longbutton hasn't been registered
    buttonState = "shortpress";
    callBeeper(40, 1);
    buttonCheck = false;
  }
}
// runs in the main loop
void button() { 
  if (millis() - buttonReference > longButtonThreshold && buttonCheck) { // if time diff is above threshold
    buttonState = "longpress";
    callBeeper(120, 1);
    buttonCheck = false; // prevents shortbutton from being registered
  }
}
/*
reliable rotary encoder reading function
variable rate of change based on rotation speed for much faster value seeking
*/
void encoderISR() { // called when BTN_EN1 is changing through interrupt
  static volatile int encoderLastStatus;
  static volatile unsigned long encoderReference;
  int encoderCurrentStatus = digitalRead(BTN_EN1);
  // check if these conditions are met before registering one pulse
  if (encoderCurrentStatus != encoderLastStatus  && encoderCurrentStatus == 1){
    // calculate the interval since last call to determine the rate of change
    unsigned long encoderInterval = min((unsigned long)150, (millis() - encoderReference));
    int encoderIncrement = map(encoderInterval, 150, 10, 1, encoderRate);
    // sensing which way the encoder is turning and registering values
    if (digitalRead(BTN_EN2) != encoderCurrentStatus) {
      encoderCount -= encoderIncrement;
      encoderRotation = "CCW"; // will stay CCW until manually reset
    } else {
      encoderCount += encoderIncrement;
      encoderRotation = "CW"; // will stay CW until manually reset
    }
    callBeeper(40, 1);
    encoderReference = millis(); // save current time for the next cycle
  }
  encoderLastStatus = encoderCurrentStatus; // save last encoder status
}
/*
sd card setup sketch
initiates SD.begin based on card insertion
will flag sdReady true when successfully initiated
*/
void sdSetup() {
static bool initiateSD = true;
  if (!digitalRead(SPI_DET)) { // if SD card is inserted
    if (initiateSD) { // if initialization hasn't been done
      for (int attempts = 0; attempts < 5; attempts++) { // attempts to initialize SD card
        if (SD.begin(SPI_CS)) { // if SD.begin() successfully initiated
          statusMessage = "SD initiated";
          sdReady = true;
          break; // exit loop on success
        }
      }
      if (!sdReady) { // if SD.begin() fail to initiate
        statusMessage = "SD fail to initiate";
      }
      initiateSD = false; // allows one initialization per card insertion
    }
  } else if (!initiateSD) { // if SD card is removed after initialization attempt
    SD.end();
    statusMessage = "SD removed";
    sdReady = false;
    initiateSD = true;
  }
}

void screenUpdate() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont11_tf);
  u8g2.drawStr(1, 8, "button");
  u8g2.setCursor(68, 8);
  u8g2.print(buttonState);
  u8g2.drawStr(1, 17, "encoder count");
  u8g2.setCursor(110, 17);
  u8g2.print(encoderCount);
  u8g2.drawStr(1, 26, "encoder rotation");
  u8g2.setCursor(110, 26);
  u8g2.print(encoderRotation);
  u8g2.drawStr(1, 35, "SD inserted?");
  if(!digitalRead(SPI_DET)) u8g2.drawStr(110, 35, "yes");
  else u8g2.drawStr(110, 35, "no");
  u8g2.drawStr(1, 44, "SD ready?");
  if(sdReady == true) u8g2.drawStr(110, 44, "yes");
  else u8g2.drawStr(110, 44, "no");
  u8g2.drawStr(1, 62, statusMessage.c_str());
  u8g2.sendBuffer();
}

void setup() {
  // pinmodes
  pinMode(BEEPER, OUTPUT);
  pinMode(SPI_DET, INPUT_PULLUP);
  pinMode(BTN_EN1, INPUT);
  pinMode(BTN_EN2, INPUT);
  pinMode(BTN_ENC, INPUT);
  // interrupts
  attachInterrupt(digitalPinToInterrupt(BTN_ENC), buttonISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(BTN_EN1), encoderISR, CHANGE);
  // initialize
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
  u8g2.begin();
  u8g2.setContrast(225);
  FastLED.addLeds<WS2811, LCD_D5, RGB>(leds, 3);
  leds[0] = CRGB(255,255,255);
  leds[1] = CRGB(255,255,255);
  leds[2] = CRGB(255,255,255);
  FastLED.show();
}

void loop() {
  beeper();
  if (millis() - looptime0 >= 200) {
    looptime0 = millis();
    screenUpdate();
    button();
    sdSetup();
  }
}

/*
backlight colour presets (rgb)
* neutral white 250, 140, 110
* vfd green 230, 240, 110
* golden yellow 250, 160, 20
* contextual orange 250, 50, 0 
*/