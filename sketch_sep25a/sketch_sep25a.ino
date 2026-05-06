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
#define EE_LAYOUT_V2 2
#define EE_MAGIC 0xA5

static const uint16_t IDLE_MIN_CHOICE[] = {1, 3, 5, 10};
static const uint16_t DISPLAY_OFF_MIN_CHOICE[] = {15, 30, 60};

//-----------------------------------------------
int flowMinutes = 0;   // Total flow minutes
int menuIndex = 0;     // 0 UP, 1 DOWN, 2 Reset, 3 Sound, 4 Set
const int menuOptionCount = 5;
String menuOptions[menuOptionCount] = {"UP", "DOWN", "Reset", "Sound", "Set"};
bool soundEnabled = true;
uint8_t defaultCountdownMinutes = 20;
uint8_t idleChoiceIndex = 1;       // default 3 min -> IDLE_MIN_CHOICE[1]
uint8_t displayOffChoiceIndex = 1; // default 30 min
uint8_t successStyle = 0;          // 0 full, 1 short, 2 minimal
bool invertEnabled = false;
uint8_t contrastLevel = 2;        // 0 lo, 1 med, 2 hi
uint16_t sessionCount = 0;
uint16_t goalMinutes = 0;        // 0 = no goal

unsigned long lastActivityTime = 0;
unsigned long inactivityLimitMs = 3UL * 60000UL;
unsigned long displayOffTimeLimitMs = 30UL * 60000UL;

enum State { MENU, COUNTING_UP, COUNTING_DOWN, SELECTING_DOWN_DURATION, IDLE, SETTINGS };
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
  SET_PAGE_EXIT,
  SET_PAGE_COUNT
};
int setupPage = 0;

int countdownValue = 20;  // Default value for countdown
int initialCountdownValue = 20;  // Store the countdown value when selected
unsigned long previousMillis = 0;  // For counting logic
int elapsedMinutes = 0;
bool isCounting = false;
unsigned long buttonDebounceTime = 0;
const unsigned long buttonDebounceDelay = 800;  // Debounce delay
unsigned long rotaryIgnoreUntilMs = 0;
const unsigned long rotaryPressGuardDelay = 150;

// Rotary encoder debounce variables
unsigned long lastRotaryTime = 0;
const unsigned long rotaryDebounceDelay = 150;  // Faster debounce for rotary encoder

// IDLE mode extended behavior (displayOffTimeLimitMs from EEPROM)

unsigned long idleStartTime = 0;  // Track when IDLE mode starts
bool displayOff = false;  // Track if the display is off

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

  if (magic == EE_MAGIC && EEPROM.read(EE_ADDR_LAYOUT) == EE_LAYOUT_V2) {
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
  } else {
    defaultCountdownMinutes = 20;
    idleChoiceIndex = 1;
    displayOffChoiceIndex = 1;
    successStyle = 0;
    invertEnabled = false;
    contrastLevel = 2;
    sessionCount = 0;
    goalMinutes = 0;
  }

  if (defaultCountdownMinutes < 1 || defaultCountdownMinutes > 120) defaultCountdownMinutes = 20;
  if (idleChoiceIndex > 3) idleChoiceIndex = 1;
  if (displayOffChoiceIndex > 2) displayOffChoiceIndex = 1;
  if (successStyle > 2) successStyle = 0;
  if (contrastLevel > 2) contrastLevel = 2;
  if (goalMinutes > 480) goalMinutes = 480;
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
  EEPROM.write(EE_ADDR_LAYOUT, EE_LAYOUT_V2);
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
  tone(BUZZER_PIN, 1500, 12);
  delay(14);
  noTone(BUZZER_PIN);
}

//=========================================================
void playButtonClick() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 900, 20);
  delay(22);
  noTone(BUZZER_PIN);
}

//=========================================================
void playSessionStartTone() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 740, 45);
  delay(55);
  tone(BUZZER_PIN, 988, 55);
  delay(65);
  noTone(BUZZER_PIN);
}

//=========================================================
void playIdleTone() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 420, 35);
  delay(40);
  noTone(BUZZER_PIN);
}

//=========================================================
void bumpSessionCount() {
  if (sessionCount < 65535u) sessionCount++;
  saveAllSettings();
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
// Update the OLED display with the current state
void updateDisplay() {
  display.setTextColor(WHITE);
  display.clearDisplay();
  applyContrastInvert();

  // Display top row
  String topRowText;

  if (currentState == SETTINGS) {
    const char *titles[] = {
      "Def time", "Idle", "Off", "Anim", "Invert", "Contrast", "Sessions", "Goal", "Exit"
    };
    topRowText = titles[setupPage];
  } else if (currentState == COUNTING_UP) {
    topRowText = "Focus! \x18";
  } else if (currentState == COUNTING_DOWN) {
    topRowText = "Focus! \x19";
  } else if (currentState == MENU && menuIndex == 3) {
    topRowText = "Sound";
  } else if (currentState == MENU && menuIndex == 4) {
    topRowText = "Setup";
  } else if (currentState == MENU && goalMinutes > 0 && menuIndex <= 2) {
    topRowText = String(flowMinutes) + "/" + String(goalMinutes);
  } else {
    topRowText = "Total: " + String(flowMinutes);
  }

  int topRowTextWidth = topRowText.length() * 12;
  int topRowX = (128 - topRowTextWidth) / 2;

  display.setTextSize(2);
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
  } else if (currentState == COUNTING_DOWN || currentState == SELECTING_DOWN_DURATION) {
    mainRowText = String(countdownValue);
  } else if (currentState == IDLE) {
    mainRowText = "IDLE?";
  }

  int mainRowTextWidth = mainRowText.length() * 24;
  int mainRowX = (128 - mainRowTextWidth) / 2;

  display.setTextSize(4);
  display.setCursor(mainRowX, 30);
  display.print(mainRowText);

  display.display();
}

//=========================================================
// Detect button presses with debounce logic
bool buttonPressed() {
  if (digitalRead(SW) == LOW && (millis() - buttonDebounceTime > buttonDebounceDelay)) {
    unsigned long now = millis();
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
        resetFlowMinutes();  // Reset the total focus time to 0
      } else if (menuIndex == 3) {  // Sound on/off
        soundEnabled = !soundEnabled;
        saveAllSettings();
        if (soundEnabled) playPreviewBeep();
        Serial.print("Sound "); Serial.println(soundEnabled ? "ON" : "OFF");
      } else if (menuIndex == 4) {  // Setup
        currentState = SETTINGS;
        setupPage = 0;
        Serial.println("Enter SETTINGS.");
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
      
    case SELECTING_DOWN_DURATION:
      confirmCountdownSelection();
      break;

    case COUNTING_UP:
      stopCountingUp();
      break;

    case COUNTING_DOWN:
      stopCountingDown();
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
  lastActivityTime = millis();  // Reset inactivity timer
  playSessionStartTone();
  Serial.println("Counting UP started.");
}

//=========================================================
// Start selecting the countdown duration
void startSelectingDownDuration() {
  currentState = SELECTING_DOWN_DURATION;
  countdownValue = defaultCountdownMinutes;
  lastActivityTime = millis();  // Reset inactivity timer
  Serial.println("Selecting DOWN duration.");
}

//=========================================================
// Confirm countdown selection and start counting down
void confirmCountdownSelection() {
  initialCountdownValue = countdownValue;
  currentState = COUNTING_DOWN;
  isCounting = true;
  lastActivityTime = millis();  // Reset inactivity timer
  playSessionStartTone();
  Serial.print("Counting DOWN started with "); Serial.print(countdownValue); Serial.println(" minutes.");
}

//=========================================================
// Stop counting up and return to menu
void stopCountingUp() {
  flowMinutes += elapsedMinutes;
  bumpSessionCount();
  successAnimation();
  currentState = MENU;
  isCounting = false;
  Serial.println("Counting UP stopped. Returning to MENU.");
}

//=========================================================
// Stop counting down and return to menu
void stopCountingDown() {
  flowMinutes += (initialCountdownValue - countdownValue);
  bumpSessionCount();
  successAnimation();
  currentState = MENU;
  isCounting = false;
  Serial.println("Counting DOWN stopped. Returning to MENU.");
}

//=========================================================
// Reset the total flow minutes counter to 0
void resetFlowMinutes() {
  flowMinutes = 0;
  Serial.println("Flow minutes reset to 0.");
  updateDisplay();  // Update the display to show the reset value
}

//=========================================================
// Handle counting up or down logic
void handleCounting(unsigned long currentMillis) {
  if (!isCounting || (currentMillis - previousMillis < 60000)) return;

  previousMillis = currentMillis;
  
  if (currentState == COUNTING_UP) {
    elapsedMinutes++;
    updateDisplay();
    Serial.print("Counting UP: "); Serial.println(elapsedMinutes);
  } else if (currentState == COUNTING_DOWN) {
    countdownValue--;
    if (countdownValue <= 0) {
      flowMinutes += initialCountdownValue;
      bumpSessionCount();
      successAnimation();
      currentState = MENU;
      isCounting = false;
      Serial.println("Countdown finished, returning to MENU.");
    }
    updateDisplay();
    Serial.print("Counting DOWN: "); Serial.println(countdownValue);
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
  if (digitalRead(SW) == LOW) return;  // Do not rotate while button is held
  if (millis() < rotaryIgnoreUntilMs) return;  // Ignore post-click jitter window

  int rotation = getRotation();
  if (rotation == 0) return;  // No rotation detected
  
  playRotateTick();
  lastActivityTime = millis();  // Reset inactivity timer on any valid rotation
  Serial.print(millis());  // Print the current time in milliseconds
  Serial.print(" - Rotation detected, activity timer reset. Rotation: ");
  Serial.println(rotation);

  if (currentState == MENU) {
    menuIndex = (menuIndex + rotation + menuOptionCount) % menuOptionCount;
    updateDisplay();
    Serial.print(millis());  // Print the current time in milliseconds
    Serial.print(" - Menu option: "); Serial.println(menuOptions[menuIndex]);
  } else if (currentState == SELECTING_DOWN_DURATION) {
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
        break;
      default:
        break;
    }
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
    if ((currentState == MENU || currentState == SELECTING_DOWN_DURATION || currentState == SETTINGS) &&
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

