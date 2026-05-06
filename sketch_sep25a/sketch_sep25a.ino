#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_SSD1306.h>

#ifndef SSD1306_SETCONTRAST
#define SSD1306_SETCONTRAST 0x81
#endif

//-----------------------------------------------
Adafruit_SSD1306 display(128, 64, &Wire, D4);

//-----------------------------------------------
#define CLK    D6
#define DT     D7
#define SW     D4
#define BUZZER_PIN D5

// EEPROM layout: 0 magic, 1 sound, 2 defaultDn, 3 idleIdx, 4 offIdx,
// 5 successStyle, 6 invert, 7 contrast, 8-9 session u16 LE, 10-11 goal u16 LE
#define EEPROM_SIZE 32
#define EE_ADDR_MAGIC 0
#define EE_ADDR_SOUND 1
#define EE_ADDR_DEFAULT_DN 2
#define EE_ADDR_IDLE_IDX 3
#define EE_ADDR_OFF_IDX 4
#define EE_ADDR_SUCCESS 5
#define EE_ADDR_INVERT 6
#define EE_ADDR_CONTRAST 7
#define EE_ADDR_SESSION_LO 8
#define EE_ADDR_SESSION_HI 9
#define EE_ADDR_GOAL_LO 10
#define EE_ADDR_GOAL_HI 11
#define EE_ADDR_LAYOUT 12
#define EE_ADDR_POMO_ON 13
#define EE_ADDR_POMO_BREAK 14
#define EE_ADDR_MILESTONE 15
#define EE_ADDR_LAST_MODE 16
#define EE_ADDR_LAST_DOWN 17
#define EE_LAYOUT_V3 3
#define EE_MAGIC 0xA5

static const uint16_t IDLE_MIN_CHOICE[] = {1, 3, 5, 10};
static const uint16_t DISPLAY_OFF_MIN_CHOICE[] = {15, 30, 60};

//-----------------------------------------------
int flowMinutes = 0;   // Total focus minutes
int menuIndex = 0;     // 0 UP, 1 DOWN, 2 Reset, 3 Sound, 4 Set, 5 Quick, 6 Help
const int menuOptionCount = 7;
String menuOptions[menuOptionCount] = {"UP", "DOWN", "Reset", "Sound", "Set", "Quick", "Help"};
bool soundEnabled = true;
uint8_t defaultCountdownMinutes = 20;
uint8_t idleChoiceIndex = 1;       // default 3 min -> IDLE_MIN_CHOICE[1]
uint8_t displayOffChoiceIndex = 1; // default 30 min
uint8_t successStyle = 0;          // 0 full, 1 short, 2 minimal
bool invertEnabled = false;
uint8_t contrastLevel = 2;        // 0 lo, 1 med, 2 hi
uint16_t sessionCount = 0;
uint16_t goalMinutes = 0;        // 0 = no goal
bool pomodoroEnabled = false;
uint8_t pomodoroBreakMinutes = 5;
uint8_t milestoneInterval = 0;   // 0 = disabled
uint8_t lastMode = 1;            // 0 UP, 1 DOWN
uint8_t lastDownDuration = 20;
bool inPomodoroBreak = false;
bool goalCelebrated = false;

unsigned long lastActivityTime = 0;
unsigned long inactivityLimitMs = 3UL * 60000UL;
unsigned long displayOffTimeLimitMs = 30UL * 60000UL;

enum State { MENU, COUNTING_UP, COUNTING_DOWN, SELECTING_DOWN_DURATION, IDLE, SETTINGS, RESET_CONFIRM, HELP };
State currentState = MENU;

enum SetupPage {
  SET_PAGE_DEF,
  SET_PAGE_IDLE,
  SET_PAGE_OFF,
  SET_PAGE_ANIM,
  SET_PAGE_INV,
  SET_PAGE_CON,
  SET_PAGE_SES,
  SET_PAGE_GOAL,
  SET_PAGE_POMO,
  SET_PAGE_BRK,
  SET_PAGE_MILE,
  SET_PAGE_EXIT,
  SET_PAGE_COUNT
};
int setupPage = 0;

int countdownValue = 20;  // Default value for countdown
int countdownSeconds = 20 * 60;  // Active countdown in seconds (COUNTING_DOWN only)
int initialCountdownValue = 20;  // Store the countdown value when selected
unsigned long previousMillis = 0;  // For counting logic
int elapsedMinutes = 0;
bool isCounting = false;
unsigned long buttonDebounceTime = 0;
const unsigned long buttonDebounceDelay = 300;  // Debounce delay
bool buttonWasDown = false;
unsigned long rotaryIgnoreUntilMs = 0;
const unsigned long rotaryPressGuardDelay = 220;
unsigned long lastSwitchLowMs = 0;
const unsigned long switchLowRotaryBlockMs = 140;
unsigned long lastSelectRotateMs = 0;
int lastSelectValueBeforeRotate = 20;

// Rotary encoder debounce variables
unsigned long lastRotaryTime = 0;
const unsigned long rotaryDebounceDelay = 150;  // Faster debounce for rotary encoder

// IDLE mode extended behavior (displayOffTimeLimitMs from EEPROM)

unsigned long idleStartTime = 0;  // Track when IDLE mode starts
bool displayOff = false;  // Track if the display is off
bool resetArmed = false;
unsigned long resetArmedAtMs = 0;
const unsigned long resetConfirmWindowMs = 3000;
bool wakeConsumedAction = false;
int helpSelectedLine = 0;
const int helpVisibleLines = 4;
const char *helpLines[] = {
  "UP: count, press stop",
  "DOWN: set mins, press",
  "QUICK: last mode",
  "SOUND: tone on/off",
  "SET: defaults/goal",
  "RESET: confirm",
  "IDLE: rotate/press"
};
const int helpLineCount = sizeof(helpLines) / sizeof(helpLines[0]);

//=========================================================
void setup() {  
  initHardware();
  loadAllSettings();
  initDisplay();
  updateDisplay();
  Serial.println("Setup complete, starting loop...");
}

//=========================================================
void loop() {
  unsigned long currentMillis = millis();

  if (wakeConsumedAction) {
    wakeConsumedAction = false;
    handleInactivity(currentMillis);
    return;
  }

  if (currentState == RESET_CONFIRM && (millis() - resetArmedAtMs > resetConfirmWindowMs)) {
    resetArmed = false;
    currentState = MENU;
    updateDisplay();
  }
  
  // Handle button presses and states
  handleButtonPresses(currentMillis);

  // Handle rotary encoder input
  handleRotaryInput();

  // Handle counting logic
  handleCounting(currentMillis);

  // Handle inactivity
  handleInactivity(currentMillis);
}

//=========================================================
// Initialize hardware pins and serial communication
void initHardware() {
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.begin(9600);
}

//=========================================================
void recomputeTimeLimits() {
  idleChoiceIndex = (uint8_t)min(3, (int)idleChoiceIndex);
  displayOffChoiceIndex = (uint8_t)min(2, (int)displayOffChoiceIndex);
  inactivityLimitMs = (unsigned long)IDLE_MIN_CHOICE[idleChoiceIndex] * 60000UL;
  displayOffTimeLimitMs = (unsigned long)DISPLAY_OFF_MIN_CHOICE[displayOffChoiceIndex] * 60000UL;
}

//=========================================================
void loadAllSettings() {
  EEPROM.begin(EEPROM_SIZE);
  byte magic = EEPROM.read(EE_ADDR_MAGIC);
  byte vSound = EEPROM.read(EE_ADDR_SOUND);
  if (magic == EE_MAGIC && vSound <= 1) {
    soundEnabled = (vSound == 1);
  } else {
    soundEnabled = true;
  }

  if (magic == EE_MAGIC && EEPROM.read(EE_ADDR_LAYOUT) == EE_LAYOUT_V3) {
    defaultCountdownMinutes = EEPROM.read(EE_ADDR_DEFAULT_DN);
    idleChoiceIndex = EEPROM.read(EE_ADDR_IDLE_IDX);
    displayOffChoiceIndex = EEPROM.read(EE_ADDR_OFF_IDX);
    successStyle = EEPROM.read(EE_ADDR_SUCCESS);
    invertEnabled = EEPROM.read(EE_ADDR_INVERT) != 0;
    contrastLevel = EEPROM.read(EE_ADDR_CONTRAST);
    sessionCount = (uint16_t)EEPROM.read(EE_ADDR_SESSION_LO)
        | ((uint16_t)EEPROM.read(EE_ADDR_SESSION_HI) << 8);
    goalMinutes = (uint16_t)EEPROM.read(EE_ADDR_GOAL_LO)
        | ((uint16_t)EEPROM.read(EE_ADDR_GOAL_HI) << 8);
    pomodoroEnabled = EEPROM.read(EE_ADDR_POMO_ON) != 0;
    pomodoroBreakMinutes = EEPROM.read(EE_ADDR_POMO_BREAK);
    milestoneInterval = EEPROM.read(EE_ADDR_MILESTONE);
    lastMode = EEPROM.read(EE_ADDR_LAST_MODE);
    lastDownDuration = EEPROM.read(EE_ADDR_LAST_DOWN);
  } else {
    defaultCountdownMinutes = 20;
    idleChoiceIndex = 1;
    displayOffChoiceIndex = 1;
    successStyle = 0;
    invertEnabled = false;
    contrastLevel = 2;
    sessionCount = 0;
    goalMinutes = 0;
    pomodoroEnabled = false;
    pomodoroBreakMinutes = 5;
    milestoneInterval = 0;
    lastMode = 1;
    lastDownDuration = 20;
  }

  if (defaultCountdownMinutes < 1 || defaultCountdownMinutes > 120) defaultCountdownMinutes = 20;
  if (idleChoiceIndex > 3) idleChoiceIndex = 1;
  if (displayOffChoiceIndex > 2) displayOffChoiceIndex = 1;
  if (successStyle > 2) successStyle = 0;
  if (contrastLevel > 2) contrastLevel = 2;
  if (goalMinutes > 480) goalMinutes = 480;
  if (pomodoroBreakMinutes < 1 || pomodoroBreakMinutes > 30) pomodoroBreakMinutes = 5;
  if (milestoneInterval > 30) milestoneInterval = 0;
  if (lastMode > 1) lastMode = 1;
  if (lastDownDuration < 1 || lastDownDuration > 120) lastDownDuration = 20;
  recomputeTimeLimits();
}

//=========================================================
void saveAllSettings() {
  EEPROM.write(EE_ADDR_MAGIC, EE_MAGIC);
  EEPROM.write(EE_ADDR_SOUND, soundEnabled ? (byte)1 : (byte)0);
  EEPROM.write(EE_ADDR_DEFAULT_DN, defaultCountdownMinutes);
  EEPROM.write(EE_ADDR_IDLE_IDX, idleChoiceIndex);
  EEPROM.write(EE_ADDR_OFF_IDX, displayOffChoiceIndex);
  EEPROM.write(EE_ADDR_SUCCESS, successStyle);
  EEPROM.write(EE_ADDR_INVERT, invertEnabled ? (byte)1 : (byte)0);
  EEPROM.write(EE_ADDR_CONTRAST, contrastLevel);
  EEPROM.write(EE_ADDR_SESSION_LO, (byte)(sessionCount & 0xFF));
  EEPROM.write(EE_ADDR_SESSION_HI, (byte)((sessionCount >> 8) & 0xFF));
  EEPROM.write(EE_ADDR_GOAL_LO, (byte)(goalMinutes & 0xFF));
  EEPROM.write(EE_ADDR_GOAL_HI, (byte)((goalMinutes >> 8) & 0xFF));
  EEPROM.write(EE_ADDR_POMO_ON, pomodoroEnabled ? (byte)1 : (byte)0);
  EEPROM.write(EE_ADDR_POMO_BREAK, pomodoroBreakMinutes);
  EEPROM.write(EE_ADDR_MILESTONE, milestoneInterval);
  EEPROM.write(EE_ADDR_LAST_MODE, lastMode);
  EEPROM.write(EE_ADDR_LAST_DOWN, lastDownDuration);
  EEPROM.write(EE_ADDR_LAYOUT, EE_LAYOUT_V3);
  EEPROM.commit();
} 

//=========================================================
void applyContrastInvert() {
  display.invertDisplay(invertEnabled);
  const uint8_t levels[] = {0x35, 0x7F, 0xCF};
  uint8_t c = levels[contrastLevel % 3];
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(c);
}

//=========================================================
void playPreviewBeep() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 659, 80);
  delay(90);
  noTone(BUZZER_PIN);
}

//=========================================================
void playRotateTick() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 1750, 8);
  delay(10);
  noTone(BUZZER_PIN);
}

//=========================================================
void playButtonClick() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 760, 24);
  delay(26);
  noTone(BUZZER_PIN);
}

//=========================================================
void playSessionStartTone() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 660, 40);
  delay(48);
  tone(BUZZER_PIN, 988, 70);
  delay(78);
  noTone(BUZZER_PIN);
}

//=========================================================
void playIdleTone() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 340, 28);
  delay(34);
  noTone(BUZZER_PIN);
}

//=========================================================
void playLastSecondsBeep() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 1400, 30);
  delay(34);
  noTone(BUZZER_PIN);
}

//=========================================================
void playMilestoneTone() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 1200, 20);
  delay(24);
  noTone(BUZZER_PIN);
}

//=========================================================
void playGoalCelebrationTone() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 880, 70);
  delay(78);
  tone(BUZZER_PIN, 1175, 80);
  delay(88);
  noTone(BUZZER_PIN);
}

//=========================================================
void bumpSessionCount() {
  if (sessionCount < 65535u) sessionCount++;
  saveAllSettings();
}

//=========================================================
void checkGoalCelebration(int previousFlow) {
  if (goalMinutes == 0) return;
  if (previousFlow < (int)goalMinutes && flowMinutes >= (int)goalMinutes) {
    playGoalCelebrationTone();
    goalCelebrated = true;
  }
}

//=========================================================
String formatMinutesSeconds(int totalSeconds) {
  int m = max(0, totalSeconds) / 60;
  int s = max(0, totalSeconds) % 60;
  String out = String(m) + ":";
  if (s < 10) out += "0";
  out += String(s);
  return out;
}

//=========================================================
void playSuccessMelody() {
  if (!soundEnabled) return;
  const int p = BUZZER_PIN;
  tone(p, 523, 100);
  delay(110);
  tone(p, 659, 100);
  delay(110);
  tone(p, 784, 180);
  delay(190);
  noTone(p);
}

//=========================================================
// Initialize the OLED display
void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  applyContrastInvert();
  Serial.println("Display initialized.");
}

//=========================================================
int centeredTextX(const String &text, uint8_t textSize) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(textSize);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int x = (128 - (int)w) / 2;
  return constrain(x, 0, 127);
}

//=========================================================
// Update the OLED display with the current state
void updateDisplay() {
  display.setTextColor(WHITE);
  display.clearDisplay();
  applyContrastInvert();

  if (currentState == HELP) {
    int helpTopLine = constrain(helpSelectedLine - (helpVisibleLines / 2), 0, max(0, helpLineCount - helpVisibleLines));
    String header = "Help " + String(helpSelectedLine + 1) + "/" + String(helpLineCount);
    display.setTextSize(1);
    display.setCursor(centeredTextX(header, 1), 0);
    display.print(header);

    for (int i = 0; i < helpVisibleLines; i++) {
      int lineIndex = helpTopLine + i;
      if (lineIndex >= helpLineCount) break;
      int y = 18 + (i * 9);  // Keep help body fully in blue zone
      display.setCursor(0, y);
      display.print(helpLines[lineIndex]);
    }

    String helpHint = "R:scroll  P:back";
    display.setCursor(centeredTextX(helpHint, 1), 56);
    display.print(helpHint);
    display.display();
    return;
  }

  // Display top row
  String topRowText;
  String hintText;

  if (currentState == SETTINGS) {
    const char *titles[] = {
      "Def", "Idle", "Off", "Anim", "Invert", "Ctrst", "Sess", "Goal", "Pomo", "Break", "Miles", "Exit"
    };
    topRowText = String(titles[setupPage]) + " " + String(setupPage + 1) + "/" + String((int)SET_PAGE_COUNT);
    hintText = "R:change  P:next";
  } else if (currentState == COUNTING_UP) {
    topRowText = "Focus! \x18";
    hintText = "";
  } else if (currentState == COUNTING_DOWN) {
    topRowText = inPomodoroBreak ? "Break \x19" : "Focus! \x19";
    hintText = "";
  } else if (currentState == RESET_CONFIRM) {
    topRowText = "Reset";
    hintText = "Press=Yes  Rotate=No";
  } else if (currentState == MENU && menuIndex == 3) {
    topRowText = "Sound";
    hintText = "Rotate / Press";
  } else if (currentState == MENU && menuIndex == 4) {
    topRowText = "Setup";
    hintText = "Press to edit";
  } else if (currentState == MENU && menuIndex == 5) {
    topRowText = "Quick";
    hintText = "Press to run";
  } else if (currentState == MENU && menuIndex == 6) {
    topRowText = "Help";
    hintText = "Press to open";
  } else if (currentState == MENU && goalMinutes > 0 && menuIndex <= 2) {
    topRowText = String(flowMinutes) + "/" + String(goalMinutes);
    hintText = "Rotate / Press";
  } else if (currentState == SELECTING_DOWN_DURATION) {
    topRowText = "Set Min";
    hintText = "R:change  P:start";
  } else if (currentState == IDLE) {
    topRowText = "Idle";
    hintText = "Rotate or Press";
  } else {
    topRowText = "Total: " + String(flowMinutes);
    hintText = "Rotate / Press";
  }

  uint8_t topRowSize = 2;
  int topRowX = centeredTextX(topRowText, topRowSize);
  if (topRowX == 0) {
    topRowSize = 1;
    topRowX = centeredTextX(topRowText, topRowSize);
  }

  display.setTextSize(topRowSize);
  display.setCursor(topRowX, 0);
  display.print(topRowText);

  // Display main row
  String mainRowText;

  if (currentState == SETTINGS) {
    switch (setupPage) {
      case SET_PAGE_DEF:
        mainRowText = String(defaultCountdownMinutes);
        break;
      case SET_PAGE_IDLE:
        mainRowText = String(IDLE_MIN_CHOICE[idleChoiceIndex]);
        break;
      case SET_PAGE_OFF:
        mainRowText = String(DISPLAY_OFF_MIN_CHOICE[displayOffChoiceIndex]);
        break;
      case SET_PAGE_ANIM:
        mainRowText = (successStyle == 0) ? "FULL" : ((successStyle == 1) ? "SHRT" : "MIN");
        break;
      case SET_PAGE_INV:
        mainRowText = invertEnabled ? "ON" : "OFF";
        break;
      case SET_PAGE_CON:
        mainRowText = (contrastLevel == 0) ? "LO" : ((contrastLevel == 1) ? "MED" : "HI");
        break;
      case SET_PAGE_SES:
        mainRowText = String(sessionCount);
        break;
      case SET_PAGE_GOAL:
        mainRowText = (goalMinutes == 0) ? "off" : String(goalMinutes);
        break;
      case SET_PAGE_EXIT:
        mainRowText = "OK";
        break;
      case SET_PAGE_POMO:
        mainRowText = pomodoroEnabled ? "ON" : "OFF";
        break;
      case SET_PAGE_BRK:
        mainRowText = String(pomodoroBreakMinutes);
        break;
      case SET_PAGE_MILE:
        mainRowText = (milestoneInterval == 0) ? "OFF" : String(milestoneInterval);
        break;
      default:
        mainRowText = "?";
        break;
    }
  } else if (currentState == MENU) {
    if (menuIndex == 3) {
      mainRowText = soundEnabled ? "ON" : "OFF";
    } else {
      mainRowText = menuOptions[menuIndex];
    }
  } else if (currentState == COUNTING_UP) {
    mainRowText = String(elapsedMinutes);
  } else if (currentState == RESET_CONFIRM) {
    mainRowText = "RESET";
  } else if (currentState == COUNTING_DOWN) {
    mainRowText = formatMinutesSeconds(countdownSeconds);
  } else if (currentState == SELECTING_DOWN_DURATION) {
    mainRowText = String(countdownValue);
  } else if (currentState == IDLE) {
    mainRowText = "IDLE?";
  }

  uint8_t mainRowSize = 4;
  int mainRowX = centeredTextX(mainRowText, mainRowSize);
  if (currentState == COUNTING_DOWN || currentState == SELECTING_DOWN_DURATION) {
    mainRowSize = 3;
    mainRowX = centeredTextX(mainRowText, mainRowSize);
  }
  if (currentState == COUNTING_DOWN) {
    mainRowSize = 4;
    mainRowX = centeredTextX(mainRowText, mainRowSize);
  }
  if (currentState == SELECTING_DOWN_DURATION) {
    mainRowSize = 4;
    mainRowX = centeredTextX(mainRowText, mainRowSize);
  }
  if (currentState == COUNTING_UP) {
    mainRowSize = 4;
    mainRowX = centeredTextX(mainRowText, mainRowSize);
  }
  if (mainRowX == 0 && mainRowText.length() > 4) {
    mainRowSize = 3;
    mainRowX = centeredTextX(mainRowText, mainRowSize);
  }

  int mainRowY = (mainRowSize == 4) ? 18 : 26;  // More vertical space for large timer digits
  display.setTextSize(mainRowSize);
  display.setCursor(mainRowX, mainRowY);
  display.print(mainRowText);

  if (currentState == COUNTING_DOWN) {
    int total = max(1, initialCountdownValue * 60);
    int remaining = constrain(countdownSeconds, 0, total);
    int barX = 0, barY = 58, barW = 128, barH = 6;
    display.drawRect(barX, barY, barW, barH, WHITE);
    int fillW = map(total - remaining, 0, total, 0, barW - 2);
    if (fillW > 0) display.fillRect(barX + 1, barY + 1, fillW, barH - 2, WHITE);
  }

  if (hintText.length() > 0) {
    display.setTextSize(1);
    int hintY = (currentState == COUNTING_DOWN) ? 50 : 56;
    display.setCursor(centeredTextX(hintText, 1), hintY);
    display.print(hintText);
  }

  display.display();
}

//=========================================================
// Detect button presses with debounce logic
bool buttonPressed() {
  bool isDown = (digitalRead(SW) == LOW);
  if (isDown) lastSwitchLowMs = millis();
  if (!isDown) {
    buttonWasDown = false;
    return false;
  }

  if (!buttonWasDown && (millis() - buttonDebounceTime > buttonDebounceDelay)) {
    unsigned long now = millis();
    buttonWasDown = true;
    buttonDebounceTime = now;  // Debounce
    lastActivityTime = now;  // Reset inactivity timer
    rotaryIgnoreUntilMs = now + rotaryPressGuardDelay;  // Ignore tiny encoder movement during click
    playButtonClick();
    return true;
  }
  return false;
}

//=========================================================
// Handle button presses and manage state transitions
void handleButtonPresses(unsigned long currentMillis) {
  if (!buttonPressed()) return;

  switch (currentState) {
    case MENU:
      if (menuIndex == 0) {  // UP selected
        startCountingUp();
      } else if (menuIndex == 1) {  // DOWN selected
        startSelectingDownDuration();
      } else if (menuIndex == 2) {  // Reset selected
        currentState = RESET_CONFIRM;
        resetArmed = true;
        resetArmedAtMs = millis();
      } else if (menuIndex == 3) {  // Sound on/off
        soundEnabled = !soundEnabled;
        saveAllSettings();
        if (soundEnabled) playPreviewBeep();
        Serial.print("Sound "); Serial.println(soundEnabled ? "ON" : "OFF");
      } else if (menuIndex == 4) {  // Setup
        currentState = SETTINGS;
        setupPage = 0;
        Serial.println("Enter SETTINGS.");
      } else if (menuIndex == 5) {  // Quick start
        if (lastMode == 0) {
          startCountingUp();
        } else {
          inPomodoroBreak = false;
          countdownValue = lastDownDuration;
          confirmCountdownSelection();
        }
      } else if (menuIndex == 6) {  // Help
        currentState = HELP;
        helpSelectedLine = 0;
      }
      break;

    case SETTINGS:
      if (setupPage == SET_PAGE_EXIT) {
        currentState = MENU;
        setupPage = 0;
        Serial.println("SETTINGS exit.");
      } else {
        saveAllSettings();
        recomputeTimeLimits();
        applyContrastInvert();
        setupPage++;
        Serial.print("SETTINGS page "); Serial.println(setupPage);
      }
      break;

    case RESET_CONFIRM:
      if (resetArmed && (millis() - resetArmedAtMs <= resetConfirmWindowMs)) {
        resetFlowMinutes();
      }
      currentState = MENU;
      resetArmed = false;
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

    case HELP:
      currentState = MENU;
      break;
  }
  updateDisplay();
}

//=========================================================
// Start counting up
void startCountingUp() {
  currentState = COUNTING_UP;
  elapsedMinutes = 0;
  isCounting = true;
  previousMillis = millis();
  inPomodoroBreak = false;
  lastMode = 0;
  lastActivityTime = millis();  // Reset inactivity timer
  playSessionStartTone();
  saveAllSettings();
  Serial.println("Counting UP started.");
}

//=========================================================
// Start selecting the countdown duration
void startSelectingDownDuration() {
  currentState = SELECTING_DOWN_DURATION;
  countdownValue = defaultCountdownMinutes;
  lastMode = 1;
  lastActivityTime = millis();  // Reset inactivity timer
  saveAllSettings();
  Serial.println("Selecting DOWN duration.");
}

//=========================================================
// Confirm countdown selection and start counting down
void confirmCountdownSelection() {
  // If a tiny accidental rotate happened right before press, keep previous selected value.
  if (millis() - lastSelectRotateMs <= rotaryPressGuardDelay) {
    countdownValue = max(1, lastSelectValueBeforeRotate);
  }

  if (!inPomodoroBreak) {
    lastMode = 1;
    lastDownDuration = countdownValue;
  }
  initialCountdownValue = countdownValue;
  countdownSeconds = max(1, countdownValue) * 60;
  currentState = COUNTING_DOWN;
  isCounting = true;
  previousMillis = millis();
  lastActivityTime = millis();  // Reset inactivity timer
  playSessionStartTone();
  saveAllSettings();
  Serial.print("Counting DOWN started with "); Serial.print(countdownValue); Serial.println(" minutes.");
}

//=========================================================
// Stop counting up and return to menu
void stopCountingUp() {
  int previousFlow = flowMinutes;
  flowMinutes += elapsedMinutes;
  bumpSessionCount();
  checkGoalCelebration(previousFlow);
  successAnimation();
  currentState = MENU;
  isCounting = false;
  inPomodoroBreak = false;
  Serial.println("Counting UP stopped. Returning to MENU.");
}

//=========================================================
// Stop counting down and return to menu
void stopCountingDown() {
  int previousFlow = flowMinutes;
  if (!inPomodoroBreak) {
    int elapsedSeconds = max(0, initialCountdownValue * 60 - countdownSeconds);
    flowMinutes += (elapsedSeconds / 60);
    bumpSessionCount();
    checkGoalCelebration(previousFlow);
  }
  successAnimation();
  currentState = MENU;
  isCounting = false;
  inPomodoroBreak = false;
  Serial.println("Counting DOWN stopped. Returning to MENU.");
}

//=========================================================
// Reset the total flow minutes counter to 0
void resetFlowMinutes() {
  flowMinutes = 0;
  goalCelebrated = false;
  Serial.println("Flow minutes reset to 0.");
  updateDisplay();  // Update the display to show the reset value
}

//=========================================================
// Handle counting up or down logic
void handleCounting(unsigned long currentMillis) {
  if (!isCounting) return;
  
  if (currentState == COUNTING_UP) {
    if (currentMillis - previousMillis < 60000) return;
    previousMillis = currentMillis;
    elapsedMinutes++;
    if (milestoneInterval > 0 && (elapsedMinutes % milestoneInterval == 0)) {
      playMilestoneTone();
    }
    updateDisplay();
    Serial.print("Counting UP: "); Serial.println(elapsedMinutes);
  } else if (currentState == COUNTING_DOWN) {
    if (currentMillis - previousMillis < 1000) return;
    previousMillis = currentMillis;
    countdownSeconds = max(0, countdownSeconds - 1);
    if (countdownSeconds > 0 && countdownSeconds <= 5) {
      playLastSecondsBeep();
    }
    if (countdownSeconds <= 0) {
      if (inPomodoroBreak) {
        inPomodoroBreak = false;
        successAnimation();
        currentState = MENU;
        isCounting = false;
        Serial.println("Break finished, returning to MENU.");
      } else {
        int previousFlow = flowMinutes;
        flowMinutes += initialCountdownValue;
        bumpSessionCount();
        checkGoalCelebration(previousFlow);
        successAnimation();
        if (pomodoroEnabled) {
          inPomodoroBreak = true;
          countdownValue = pomodoroBreakMinutes;
          countdownSeconds = pomodoroBreakMinutes * 60;
          initialCountdownValue = pomodoroBreakMinutes;
          currentState = COUNTING_DOWN;
          isCounting = true;
          previousMillis = currentMillis;
          playSessionStartTone();
          Serial.println("Pomodoro break started.");
        } else {
          currentState = MENU;
          isCounting = false;
          Serial.println("Countdown finished, returning to MENU.");
        }
      }
    }
    updateDisplay();
    Serial.print("Counting DOWN: "); Serial.println(formatMinutesSeconds(countdownSeconds));
  }
}

//=========================================================
// Success animation when a session ends
void successAnimation() {
  const int centerX = 64, centerY = 32;

  if (successStyle == 2) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(36, 20);
    display.print("SUCCESS!");
    display.display();
    playSuccessMelody();
    delay(500);
    display.clearDisplay();
    display.display();
    return;
  }

  const int rMax = (successStyle == 1) ? 18 : 30;
  const int stepDelay = (successStyle == 1) ? 50 : 100;

  display.clearDisplay();
  for (int radius = 2; radius <= rMax; radius += 2) {
    display.drawCircle(centerX, centerY, radius, WHITE);
    display.display();
    delay(stepDelay);

    if (radius % 4 == 0) {
      display.clearDisplay();
      display.display();
      delay(successStyle == 1 ? 1 : 2);
    }
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(20, 20);
  display.print("SUCCESS!");
  display.display();
  playSuccessMelody();
  delay(1000);
  display.clearDisplay();
  display.display();
}

//=========================================================
// Rotary Encoder Rotation Detection
int getRotation() {
  static int previousCLK = digitalRead(CLK);
  int currentCLK = digitalRead(CLK);
  
  if (currentCLK == LOW && previousCLK == HIGH && (millis() - lastRotaryTime > rotaryDebounceDelay)) {
    lastRotaryTime = millis();  // Debounce
    int DTValue = digitalRead(DT);  // Read DT to determine direction

    previousCLK = currentCLK;  // Update previous CLK for next iteration

    return (DTValue != currentCLK) ? 1 : -1;  // Clockwise or counterclockwise
  }
  
  previousCLK = currentCLK;
  return 0;  // No rotation
}

//=========================================================
// Handle rotary input for menu and countdown selection
void handleRotaryInput() {
  if (currentState == IDLE) return;
  if (digitalRead(SW) == LOW) return;  // Do not rotate while button is held
  if (millis() - lastSwitchLowMs < switchLowRotaryBlockMs) return;  // Block around press edges/bounce
  if (millis() < rotaryIgnoreUntilMs) return;  // Ignore post-click jitter window

  int rotation = getRotation();
  if (rotation == 0) return;  // No rotation detected
  
  playRotateTick();
  lastActivityTime = millis();  // Reset inactivity timer on any valid rotation
  Serial.print(millis());  // Print the current time in milliseconds
  Serial.print(" - Rotation detected, activity timer reset. Rotation: ");
  Serial.println(rotation);

  if (currentState == MENU) {
    if (resetArmed) resetArmed = false;
    menuIndex = (menuIndex + rotation + menuOptionCount) % menuOptionCount;
    updateDisplay();
    Serial.print(millis());  // Print the current time in milliseconds
    Serial.print(" - Menu option: "); Serial.println(menuOptions[menuIndex]);
  } else if (currentState == SELECTING_DOWN_DURATION) {
    lastSelectValueBeforeRotate = countdownValue;
    lastSelectRotateMs = millis();
    countdownValue = max(1, countdownValue + rotation);
    updateDisplay();
    Serial.print(millis());  // Print the current time in milliseconds
    Serial.print(" - Countdown value: "); Serial.println(countdownValue);
  } else if (currentState == SETTINGS) {
    switch (setupPage) {
      case SET_PAGE_DEF:
        defaultCountdownMinutes = (uint8_t)constrain((int)defaultCountdownMinutes + rotation, 1, 120);
        break;
      case SET_PAGE_IDLE:
        idleChoiceIndex = (uint8_t)((idleChoiceIndex + rotation + 4) % 4);
        recomputeTimeLimits();
        break;
      case SET_PAGE_OFF:
        displayOffChoiceIndex = (uint8_t)((displayOffChoiceIndex + rotation + 3) % 3);
        recomputeTimeLimits();
        break;
      case SET_PAGE_ANIM:
        successStyle = (uint8_t)((successStyle + rotation + 3) % 3);
        break;
      case SET_PAGE_INV:
        if (rotation != 0) invertEnabled = !invertEnabled;
        applyContrastInvert();
        break;
      case SET_PAGE_CON:
        contrastLevel = (uint8_t)((contrastLevel + rotation + 3) % 3);
        applyContrastInvert();
        break;
      case SET_PAGE_SES: {
        int s = (int)sessionCount + rotation;
        sessionCount = (uint16_t)constrain(s, 0, 9999);
        break;
      }
      case SET_PAGE_GOAL:
        goalMinutes = (uint16_t)constrain((int)goalMinutes + rotation * 5, 0, 480);
        goalCelebrated = false;
        break;
      case SET_PAGE_POMO:
        if (rotation != 0) pomodoroEnabled = !pomodoroEnabled;
        break;
      case SET_PAGE_BRK:
        pomodoroBreakMinutes = (uint8_t)constrain((int)pomodoroBreakMinutes + rotation, 1, 30);
        break;
      case SET_PAGE_MILE:
        milestoneInterval = (uint8_t)constrain((int)milestoneInterval + rotation, 0, 30);
        break;
      default:
        break;
    }
    updateDisplay();
  } else if (currentState == RESET_CONFIRM) {
    resetArmed = false;
    currentState = MENU;
    updateDisplay();
  } else if (currentState == HELP) {
    helpSelectedLine = constrain(helpSelectedLine + rotation, 0, helpLineCount - 1);
    updateDisplay();
  }
}
//=========================================================
// Handle inactivity and switch to IDLE if necessary
void handleInactivity(unsigned long currentMillis) {
  // Comment out frequent serial prints to improve performance
  /*
  Serial.print(millis());
  Serial.print(" - Current time (millis): ");
  Serial.println(currentMillis);
  
  Serial.print(millis());
  Serial.print(" - Last activity time (millis): ");
  Serial.println(lastActivityTime);
  */

  // Make sure the subtraction does not cause an overflow/underflow
  if (currentMillis >= lastActivityTime) {
    unsigned long timeSinceLastActivity = currentMillis - lastActivityTime;

    /*
    Serial.print(millis());
    Serial.print(" - Time since last activity (ms): ");
    Serial.println(timeSinceLastActivity);
    */

    // Check if the user is in the MENU or selecting countdown duration mode
    if ((currentState == MENU || currentState == SELECTING_DOWN_DURATION || currentState == SETTINGS || currentState == HELP) &&
        (timeSinceLastActivity > inactivityLimitMs)) {
      if (currentState != IDLE) {
        if (currentState == SETTINGS) {
          saveAllSettings();
          recomputeTimeLimits();
        }
        currentState = IDLE;
        idleStartTime = millis();  // Record when IDLE mode starts
        playIdleTone();
        updateDisplay();
        Serial.print(millis());  // Print the current time in milliseconds
        Serial.println(" - IDLE state entered due to inactivity.");
      }
    }
  } else {
    // Comment out warning to reduce unnecessary serial prints
    // Serial.println(" - Warning: currentMillis is less than lastActivityTime!");
  }

  if (currentState == IDLE && !displayOff && (currentMillis - idleStartTime > displayOffTimeLimitMs)) {
    displayOff = true;
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    Serial.print(millis());
    Serial.println(" - Display turned off after IDLE timeout.");
  }

  // Exit IDLE if any rotary or button action happens
  if (currentState == IDLE && (getRotation() != 0 || buttonPressed())) {
    currentState = MENU;
    lastActivityTime = millis();  // Reset inactivity timer upon exiting IDLE
    wakeConsumedAction = true;    // Wake only, ignore first action
    
    // Turn the display back on if it was off
    if (displayOff) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      displayOff = false;
      Serial.print(millis());  // Print the current time in milliseconds
      Serial.println(" - Display turned back on.");
    }

    updateDisplay();
    Serial.print(millis());  // Print the current time in milliseconds
    Serial.println(" - Exiting IDLE mode. Back to MENU.");
  }
}

