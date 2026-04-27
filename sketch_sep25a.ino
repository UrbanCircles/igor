#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <TimeLib.h>

//-----------------------------------------------
Adafruit_SSD1306 display(128, 64, &Wire, -1);

//-----------------------------------------------
#define CLK    D6
#define DT     D7
#define SW     D4

//-----------------------------------------------
// Time & NTP (Kathmandu UTC+5:45)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 20700, 60000); // 20700 sec = 5h45m
unsigned long lastTimeRefresh = 0;
const unsigned long TIME_REFRESH_INTERVAL = 1000;

//-----------------------------------------------
int flowMinutes = 0;
int menuIndex = 0;
String menuOptions[4] = {"UP", "DOWN", "POMO", "Reset"};
unsigned long lastActivityTime = 0;
const unsigned long inactivityLimit = 3 * 60000;

enum State { 
  MENU, COUNTING_UP, COUNTING_DOWN, SELECTING_DOWN_DURATION,
  POMODORO_WORK, POMODORO_BREAK, POMODORO_LONG_BREAK,
  IDLE 
};
State currentState = MENU;

int countdownValue = 20;
int initialCountdownValue = 20;
unsigned long previousMillis = 0;
int elapsedMinutes = 0;
bool isCounting = false;

// Pomodoro
int workDuration = 25;
int breakDuration = 5;
int longBreakDuration = 15;
int pomodoroCycle = 0;

// Debounce
unsigned long buttonDebounceTime = 0;
const unsigned long buttonDebounceDelay = 800;
unsigned long lastRotaryTime = 0;
const unsigned long rotaryDebounceDelay = 150;

// IDLE mode
const unsigned long displayOffTimeLimit = 30 * 60000;
unsigned long idleStartTime = 0;
bool displayOff = false;

//=========================================================
void setup() {  
  initHardware();
  initDisplay();
  connectWiFiAndTime();
  updateDisplay();
  Serial.begin(115200);
  Serial.println("IGOR + POMO ready");
}

//=========================================================
void loop() {
  unsigned long currentMillis = millis();
  handleRotaryInput();
  handleButtonPresses(currentMillis);
  handleCounting(currentMillis);
  handleInactivity(currentMillis);
  refreshTimeDisplay(currentMillis);
}

//=========================================================
void initHardware() {
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT);
}

void initDisplay() {
  Wire.begin(D2, D1);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 failed"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("IGOR + POMO");
  display.println("Connecting...");
  display.display();
}

void connectWiFiAndTime() {
  WiFiManager wm;
  wm.autoConnect("IGOR-Setup");
  display.println("WiFi OK");
  display.println("Getting time...");
  display.display();
  timeClient.begin();
  timeClient.update();
  lastTimeRefresh = millis();
}

String getFormattedTime() {
  time_t raw = timeClient.getEpochTime();
  setTime(raw);
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", hour(), minute(), second());
  return String(buf);
}

String getFormattedDate() {
  char buf[11];
  sprintf(buf, "%04d-%02d-%02d", year(), month(), day());
  return String(buf);
}

//=========================================================
// FIXED: font sizes adjusted for 0.96" OLED
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(WHITE);

  // --- TOP ROW (textSize 1) fits time+date ---
  String topRowText;
  if (currentState == COUNTING_UP) {
    topRowText = "Focus! \x18";
  } else if (currentState == COUNTING_DOWN) {
    topRowText = "Focus! \x19";
  } else if (currentState == POMODORO_WORK) {
    topRowText = "Pomo Work \x18";
  } else if (currentState == POMODORO_BREAK) {
    topRowText = "Break \x19";
  } else if (currentState == POMODORO_LONG_BREAK) {
    topRowText = "Long Break";
  } else {
    topRowText = getFormattedTime() + " " + getFormattedDate();
  }
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(topRowText);

  // --- MAIN VALUE (textSize 3) big but fits 3 digits ---
  String mainText;
  switch (currentState) {
    case MENU: mainText = menuOptions[menuIndex]; break;
    case COUNTING_UP: mainText = String(elapsedMinutes); break;
    case COUNTING_DOWN:
    case SELECTING_DOWN_DURATION:
      mainText = String(countdownValue); break;
    case POMODORO_WORK:
    case POMODORO_BREAK:
    case POMODORO_LONG_BREAK:
      mainText = String(countdownValue); break;
    case IDLE: mainText = "IDLE?"; break;
    default: mainText = "";
  }
  display.setTextSize(3);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(mainText, 0, 0, &x1, &y1, &w, &h);
  int mainX = (128 - w) / 2;
  int mainY = 20;  // below top row
  display.setCursor(mainX, mainY);
  display.print(mainText);

  // --- BOTTOM STATUS (textSize 1) ---
  display.setTextSize(1);
  display.setCursor(0, 56);
  if (currentState == MENU) {
    display.print("Flow:" + String(flowMinutes) + "min");
  } else if (currentState == SELECTING_DOWN_DURATION) {
    display.print("Rotate -> Set min");
  } else if (isCounting) {
    display.print("Click to stop");
  } else {
    display.print("Click start");
  }
  display.display();
}

//=========================================================
bool buttonPressed() {
  if (digitalRead(SW) == LOW && (millis() - buttonDebounceTime > buttonDebounceDelay)) {
    buttonDebounceTime = millis();
    lastActivityTime = millis();
    return true;
  }
  return false;
}

void handleButtonPresses(unsigned long currentMillis) {
  if (!buttonPressed()) return;

  switch (currentState) {
    case MENU:
      if (menuIndex == 0) startCountingUp();
      else if (menuIndex == 1) startSelectingDownDuration();
      else if (menuIndex == 2) startPomodoro();
      else if (menuIndex == 3) resetFlowMinutes();
      break;
    case SELECTING_DOWN_DURATION:
      confirmCountdownSelection();
      break;
    case COUNTING_UP:
      stopCountingUp();
      break;
    case COUNTING_DOWN:
      stopCountingDown();
      break;
    case POMODORO_WORK:
    case POMODORO_BREAK:
    case POMODORO_LONG_BREAK:
      stopPomodoro();
      break;
    default: break;
  }
  updateDisplay();
}

void startCountingUp() {
  currentState = COUNTING_UP;
  elapsedMinutes = 0;
  isCounting = true;
  lastActivityTime = millis();
  updateDisplay();
}

void stopCountingUp() {
  flowMinutes += elapsedMinutes;
  successAnimation();
  currentState = MENU;
  isCounting = false;
  updateDisplay();
}

void startSelectingDownDuration() {
  currentState = SELECTING_DOWN_DURATION;
  countdownValue = 20;
  lastActivityTime = millis();
  updateDisplay();
}

void confirmCountdownSelection() {
  initialCountdownValue = countdownValue;
  currentState = COUNTING_DOWN;
  isCounting = true;
  lastActivityTime = millis();
  updateDisplay();
}

void stopCountingDown() {
  int minutesDone = initialCountdownValue - countdownValue;
  if (minutesDone > 0) flowMinutes += minutesDone;
  successAnimation();
  currentState = MENU;
  isCounting = false;
  updateDisplay();
}

void startPomodoro() {
  pomodoroCycle = 0;
  countdownValue = workDuration;
  initialCountdownValue = workDuration;
  currentState = POMODORO_WORK;
  isCounting = true;
  lastActivityTime = millis();
  updateDisplay();
}

void stopPomodoro() {
  if (currentState == POMODORO_WORK) {
    int worked = workDuration - countdownValue;
    if (worked > 0) flowMinutes += worked;
  }
  successAnimation();
  currentState = MENU;
  isCounting = false;
  updateDisplay();
}

void resetFlowMinutes() {
  flowMinutes = 0;
  updateDisplay();
}

void handleCounting(unsigned long currentMillis) {
  if (!isCounting || (currentMillis - previousMillis < 60000)) return;
  previousMillis = currentMillis;

  switch (currentState) {
    case COUNTING_UP:
      elapsedMinutes++;
      updateDisplay();
      break;
    case COUNTING_DOWN:
      countdownValue--;
      if (countdownValue <= 0) {
        flowMinutes += initialCountdownValue;
        successAnimation();
        currentState = MENU;
        isCounting = false;
      }
      updateDisplay();
      break;
    case POMODORO_WORK:
      countdownValue--;
      if (countdownValue <= 0) {
        flowMinutes += workDuration;
        pomodoroCycle++;
        if (pomodoroCycle % 4 == 0) {
          countdownValue = longBreakDuration;
          initialCountdownValue = longBreakDuration;
          currentState = POMODORO_LONG_BREAK;
        } else {
          countdownValue = breakDuration;
          initialCountdownValue = breakDuration;
          currentState = POMODORO_BREAK;
        }
        successAnimation();
        updateDisplay();
      } else updateDisplay();
      break;
    case POMODORO_BREAK:
    case POMODORO_LONG_BREAK:
      countdownValue--;
      if (countdownValue <= 0) {
        countdownValue = workDuration;
        initialCountdownValue = workDuration;
        currentState = POMODORO_WORK;
        updateDisplay();
      } else updateDisplay();
      break;
    default: break;
  }
}

void successAnimation() {
  display.clearDisplay();
  int centerX = 64, centerY = 32;
  for (int radius = 2; radius <= 30; radius += 2) {
    display.drawCircle(centerX, centerY, radius, WHITE);
    display.display();
    delay(100);
    if (radius % 4 == 0) {
      display.clearDisplay();
      display.display();
      delay(2);
    }
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(20, 20);
  display.print("SUCCESS!");
  display.display();
  delay(1000);
  display.clearDisplay();
}

int getRotation() {
  static int previousCLK = digitalRead(CLK);
  int currentCLK = digitalRead(CLK);
  if (currentCLK == LOW && previousCLK == HIGH && (millis() - lastRotaryTime > rotaryDebounceDelay)) {
    lastRotaryTime = millis();
    int DTValue = digitalRead(DT);
    previousCLK = currentCLK;
    return (DTValue != currentCLK) ? 1 : -1;
  }
  previousCLK = currentCLK;
  return 0;
}

void handleRotaryInput() {
  int rotation = getRotation();
  if (rotation == 0) return;
  lastActivityTime = millis();

  if (currentState == MENU) {
    menuIndex = (menuIndex + rotation + 4) % 4;
    updateDisplay();
  } else if (currentState == SELECTING_DOWN_DURATION) {
    countdownValue = max(1, countdownValue + rotation);
    updateDisplay();
  }
}

void handleInactivity(unsigned long currentMillis) {
  if (currentMillis >= lastActivityTime) {
    unsigned long idleTime = currentMillis - lastActivityTime;
    if ((currentState == MENU || currentState == SELECTING_DOWN_DURATION) && idleTime > inactivityLimit) {
      if (currentState != IDLE) {
        currentState = IDLE;
        idleStartTime = currentMillis;
        updateDisplay();
      }
    }
  }
  if (currentState == IDLE && !displayOff && (currentMillis - idleStartTime > displayOffTimeLimit)) {
    displayOff = true;
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }
  if (currentState == IDLE && (getRotation() != 0 || buttonPressed())) {
    currentState = MENU;
    lastActivityTime = currentMillis;
    if (displayOff) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      displayOff = false;
    }
    updateDisplay();
  }
}

void refreshTimeDisplay(unsigned long currentMillis) {
  if (currentState == MENU && (currentMillis - lastTimeRefresh > TIME_REFRESH_INTERVAL)) {
    lastTimeRefresh = currentMillis;
    timeClient.update();
    updateDisplay();
  }
}
