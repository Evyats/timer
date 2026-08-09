#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// Reuse the timer's active pin-assignment block. Change pins only in TimerConfig.h.
#include "../timer/TimerConfig.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "This timer hardware test is configured for the ESP32-S3 Super Mini."
#endif

/*
  ESP32-S3 Super Mini timer hardware diagnostic

  Arduino IDE:
    Board             -> ESP32S3 Dev Module
    USB CDC On Boot   -> Enabled

  Physical board layout, viewed from the front with USB-C at the top:

    LEFT                 RIGHT
    TX / GPIO43          5V
    RX / GPIO44          GND
    GPIO1  Encoder S2    3V3
    GPIO2  Active+LED    GPIO13 Encoder S1
    GPIO3  unused        GPIO12 PIR
    GPIO4  Battery ADC   GPIO11 Sound detector
    GPIO5  Main piezo    GPIO10 Encoder button
    GPIO6  Harmony       GPIO9  OLED SCL
    GPIO7  unused        GPIO8  OLED SDA

  GPIO3 is intentionally unused because it is an ESP32-S3 strapping pin.
  The board's blue charging LED is controlled by its charger circuit, not by
  this sketch.

  Open Serial Monitor at 115200 baud for detailed input transitions.

  OLED grid:
    S1   - raw encoder S1 level
    S2   - raw encoder S2 level
    KEY  - raw encoder pushbutton level
    PIR  - raw PIR output
    MIC  - raw sound-detector output
    BAT  - calculated battery voltage

  Active digital inputs are drawn with an inverted (filled) cell. The MIC cell
  is held active briefly so short sound pulses remain visible.

  Output sequence:
    MAIN passive piezo -> HARMONY passive piezo -> ACTIVE piezo/LED -> repeat
*/

const uint32_t SERIAL_BAUD = 115200;
const uint32_t DISPLAY_UPDATE_MS = 50;
const uint32_t BATTERY_UPDATE_INTERVAL_MS = 500;
const uint32_t SERIAL_STATUS_MS = 1000;
const uint32_t OUTPUT_ON_MS = 500;
const uint32_t OUTPUT_GAP_MS = 400;
const uint32_t MIC_DISPLAY_HOLD_MS = 300;
const uint16_t PASSIVE_TEST_FREQUENCY_HZ = 880;
const uint8_t PASSIVE_PIEZO_PWM_RESOLUTION_BITS = 10;
const uint8_t BATTERY_SAMPLE_COUNT = 8;

const int GRID_TOP = 20;
const int GRID_COLUMNS = 3;
const int GRID_ROWS = 2;
const int CELL_WIDTH = SCREEN_WIDTH / GRID_COLUMNS;
const int CELL_HEIGHT = (SCREEN_HEIGHT - GRID_TOP) / GRID_ROWS;

enum OutputStage : uint8_t {
  OUTPUT_MAIN,
  OUTPUT_HARMONY,
  OUTPUT_ACTIVE,
  OUTPUT_STAGE_COUNT
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t encoderValue = 0;
volatile uint8_t lastEncoderState = 0;
volatile int8_t encoderMovement = 0;
volatile uint32_t encoderTransitionCount = 0;
volatile uint32_t invalidEncoderTransitions = 0;
uint32_t lastReportedEncoderTransitionCount = 0;

volatile int lastS1 = HIGH;
volatile int lastS2 = HIGH;
int lastButton = HIGH;
int lastPir = LOW;
int lastSound = HIGH;
uint32_t micActiveUntilMs = 0;

uint32_t batteryPinMilliVolts = 0;
float batteryVolts = 0.0f;

OutputStage outputStage = OUTPUT_MAIN;
bool outputIsOn = false;
uint32_t nextOutputChangeMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastBatteryUpdateMs = 0;
uint32_t lastSerialStatusMs = 0;
bool piezosEnabled = true;
bool encoderEnabled = true;
bool sensorsEnabled = true;
bool batteryEnabled = true;
bool oledEnabled = true;

void IRAM_ATTR handleEncoderEdge();

bool scanI2cBus() {
  bool oledFound = false;
  uint8_t deviceCount = 0;

  Serial.println("Scanning I2C bus...");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t result = Wire.endTransmission();
    if (result != 0) {
      continue;
    }

    deviceCount++;
    Serial.print("  I2C device found at 0x");
    if (address < 0x10) {
      Serial.print("0");
    }
    Serial.println(address, HEX);
    if (address == OLED_ADDRESS) {
      oledFound = true;
    }
  }

  if (deviceCount == 0) {
    Serial.println("ERROR: No I2C devices found. Check OLED SDA/SCL/power.");
  } else if (!oledFound) {
    Serial.print("ERROR: No OLED response at configured address 0x");
    Serial.println(OLED_ADDRESS, HEX);
  } else {
    Serial.println("OLED acknowledged on the I2C bus.");
  }

  return oledFound;
}

void runOledVisualSelfTest() {
  Serial.println("OLED visual test: all pixels ON.");
  display.clearDisplay();
  display.fillScreen(SSD1306_WHITE);
  display.display();
  delay(1000);

  Serial.println("OLED visual test: text and border.");
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(18, 18);
  display.print("OLED OK");
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.display();
  delay(1500);

  display.clearDisplay();
  display.display();
}

const char* outputStageName(OutputStage stage) {
  switch (stage) {
    case OUTPUT_MAIN:
      return "MAIN";
    case OUTPUT_HARMONY:
      return "HARM";
    case OUTPUT_ACTIVE:
      return ACTIVE_PIEZO_LED_PIN == ACTIVE_PIEZO_PIN ? "ACT+LED" : "ACTIVE";
    default:
      return "?";
  }
}

void stopAllOutputs() {
  ledcWriteTone(MAIN_PIEZO_PIN, 0);
  ledcWriteTone(HARMONY_PIEZO_PIN, 0);
  digitalWrite(ACTIVE_PIEZO_PIN, LOW);

  if (ACTIVE_PIEZO_LED_PIN != ACTIVE_PIEZO_PIN) {
    digitalWrite(ACTIVE_PIEZO_LED_PIN, LOW);
  }
}

void setPiezosEnabled(bool enabled) {
  if (piezosEnabled == enabled) {
    return;
  }

  if (!enabled) {
    stopAllOutputs();
    outputIsOn = false;
    ledcDetach(MAIN_PIEZO_PIN);
    ledcDetach(HARMONY_PIEZO_PIN);
    pinMode(MAIN_PIEZO_PIN, INPUT);
    pinMode(HARMONY_PIEZO_PIN, INPUT);
    pinMode(ACTIVE_PIEZO_PIN, INPUT);
    if (ACTIVE_PIEZO_LED_PIN != ACTIVE_PIEZO_PIN) {
      pinMode(ACTIVE_PIEZO_LED_PIN, INPUT);
    }
  } else {
    pinMode(ACTIVE_PIEZO_PIN, OUTPUT);
    digitalWrite(ACTIVE_PIEZO_PIN, LOW);
    if (ACTIVE_PIEZO_LED_PIN != ACTIVE_PIEZO_PIN) {
      pinMode(ACTIVE_PIEZO_LED_PIN, OUTPUT);
      digitalWrite(ACTIVE_PIEZO_LED_PIN, LOW);
    }
    ledcAttach(MAIN_PIEZO_PIN, PASSIVE_TEST_FREQUENCY_HZ,
               PASSIVE_PIEZO_PWM_RESOLUTION_BITS);
    ledcAttach(HARMONY_PIEZO_PIN, PASSIVE_TEST_FREQUENCY_HZ,
               PASSIVE_PIEZO_PWM_RESOLUTION_BITS);
    nextOutputChangeMs = millis();
  }

  piezosEnabled = enabled;
  Serial.println(enabled ? "Piezos CONNECTED (test enabled)."
                         : "Piezos DISCONNECTED (GPIOs high-impedance)." );
}

void setEncoderEnabled(bool enabled) {
  if (encoderEnabled == enabled) {
    return;
  }

  if (!enabled) {
    detachInterrupt(digitalPinToInterrupt(ENCODER_S1_PIN));
    detachInterrupt(digitalPinToInterrupt(ENCODER_S2_PIN));
    pinMode(ENCODER_S1_PIN, INPUT);
    pinMode(ENCODER_S2_PIN, INPUT);
    pinMode(ENCODER_BUTTON_PIN, INPUT);
  } else {
    pinMode(ENCODER_S1_PIN, INPUT_PULLUP);
    pinMode(ENCODER_S2_PIN, INPUT_PULLUP);
    pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
    lastS1 = digitalRead(ENCODER_S1_PIN);
    lastS2 = digitalRead(ENCODER_S2_PIN);
    lastEncoderState = (lastS1 << 1) | lastS2;
    lastButton = digitalRead(ENCODER_BUTTON_PIN);
    encoderMovement = 0;
    attachInterrupt(digitalPinToInterrupt(ENCODER_S1_PIN), handleEncoderEdge, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_S2_PIN), handleEncoderEdge, CHANGE);
  }

  encoderEnabled = enabled;
  Serial.println(enabled ? "Encoder CONNECTED."
                         : "Encoder DISCONNECTED (GPIOs high-impedance)." );
}

void setSensorsEnabled(bool enabled) {
  if (sensorsEnabled == enabled) {
    return;
  }

  pinMode(PIR_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);
  sensorsEnabled = enabled;
  Serial.println(enabled ? "PIR and microphone CONNECTED."
                         : "PIR and microphone polling DISABLED." );
}

void setBatteryEnabled(bool enabled) {
  if (batteryEnabled == enabled) {
    return;
  }

  pinMode(BATTERY_ADC_PIN, INPUT);
  batteryEnabled = enabled;
  Serial.println(enabled ? "Battery ADC CONNECTED."
                         : "Battery ADC sampling DISABLED." );
}

void setOledEnabled(bool enabled) {
  if (oledEnabled == enabled) {
    return;
  }

  if (!enabled) {
    display.clearDisplay();
    display.display();
    Wire.end();
    pinMode(SDA_PIN, INPUT);
    pinMode(SCL_PIN, INPUT);
  } else {
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);
    scanI2cBus();
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    lastDisplayMs = 0;
  }

  oledEnabled = enabled;
  Serial.println(enabled ? "OLED/I2C CONNECTED."
                         : "OLED/I2C DISCONNECTED (GPIOs high-impedance)." );
}

void printSerialMenu() {
  Serial.println("Serial isolation controls:");
  Serial.println("  p = toggle piezos     e = toggle encoder");
  Serial.println("  s = toggle PIR/mic    b = toggle battery ADC");
  Serial.println("  o = toggle OLED/I2C   x = disconnect everything");
  Serial.println("  a = connect everything, ? = show this menu");
}

void handleSerialCommands() {
  while (Serial.available()) {
    char command = (char)tolower(Serial.read());
    switch (command) {
      case 'p': setPiezosEnabled(!piezosEnabled); break;
      case 'e': setEncoderEnabled(!encoderEnabled); break;
      case 's': setSensorsEnabled(!sensorsEnabled); break;
      case 'b': setBatteryEnabled(!batteryEnabled); break;
      case 'o': setOledEnabled(!oledEnabled); break;
      case 'x':
        setPiezosEnabled(false);
        setEncoderEnabled(false);
        setSensorsEnabled(false);
        setBatteryEnabled(false);
        setOledEnabled(false);
        break;
      case 'a':
        setPiezosEnabled(true);
        setEncoderEnabled(true);
        setSensorsEnabled(true);
        setBatteryEnabled(true);
        setOledEnabled(true);
        break;
      case '?': printSerialMenu(); break;
      default: break;
    }
  }
}

void startCurrentOutput() {
  stopAllOutputs();

  switch (outputStage) {
    case OUTPUT_MAIN:
      ledcWriteTone(MAIN_PIEZO_PIN, PASSIVE_TEST_FREQUENCY_HZ);
      break;

    case OUTPUT_HARMONY:
      ledcWriteTone(HARMONY_PIEZO_PIN, PASSIVE_TEST_FREQUENCY_HZ);
      break;

    case OUTPUT_ACTIVE:
      digitalWrite(ACTIVE_PIEZO_PIN, HIGH);
      if (ACTIVE_PIEZO_LED_PIN != ACTIVE_PIEZO_PIN) {
        digitalWrite(ACTIVE_PIEZO_LED_PIN, HIGH);
      }
      break;

    default:
      break;
  }

  outputIsOn = true;
  nextOutputChangeMs = millis() + OUTPUT_ON_MS;

  Serial.print(millis());
  Serial.print(" ms output ON: ");
  Serial.println(outputStageName(outputStage));
}

void updateOutputTest() {
  if (!piezosEnabled) {
    return;
  }

  uint32_t now = millis();
  if ((int32_t)(now - nextOutputChangeMs) < 0) {
    return;
  }

  if (outputIsOn) {
    stopAllOutputs();
    outputIsOn = false;
    nextOutputChangeMs = now + OUTPUT_GAP_MS;
    return;
  }

  outputStage = static_cast<OutputStage>((outputStage + 1) % OUTPUT_STAGE_COUNT);
  startCurrentOutput();
}

void IRAM_ATTR handleEncoderEdge() {
  int s1 = digitalRead(ENCODER_S1_PIN);
  int s2 = digitalRead(ENCODER_S2_PIN);
  uint8_t state = (s1 << 1) | s2;

  portENTER_CRITICAL_ISR(&encoderMux);
  lastS1 = s1;
  lastS2 = s2;

  if (state == lastEncoderState) {
    portEXIT_CRITICAL_ISR(&encoderMux);
    return;
  }

  uint8_t transition = (lastEncoderState << 2) | state;
  lastEncoderState = state;
  int direction = 0;

  switch (transition) {
    case 0b0001:
    case 0b0111:
    case 0b1110:
    case 0b1000:
      direction = 1;
      break;

    case 0b0010:
    case 0b1011:
    case 0b1101:
    case 0b0100:
      direction = -1;
      break;

    default:
      invalidEncoderTransitions = invalidEncoderTransitions + 1;
      encoderMovement = 0;
      encoderTransitionCount = encoderTransitionCount + 1;
      portEXIT_CRITICAL_ISR(&encoderMux);
      return;
  }

  encoderMovement += direction;
  encoderTransitionCount = encoderTransitionCount + 1;

  if (encoderMovement >= 4) {
    encoderMovement = 0;
    encoderValue += REVERSE_ENCODER_DIRECTION ? -1 : 1;
  } else if (encoderMovement <= -4) {
    encoderMovement = 0;
    encoderValue += REVERSE_ENCODER_DIRECTION ? 1 : -1;
  }

  portEXIT_CRITICAL_ISR(&encoderMux);
}

void readEncoderSnapshot(int& s1, int& s2, int32_t& value,
                         uint32_t& transitions, uint32_t& invalidTransitions) {
  portENTER_CRITICAL(&encoderMux);
  s1 = lastS1;
  s2 = lastS2;
  value = encoderValue;
  transitions = encoderTransitionCount;
  invalidTransitions = invalidEncoderTransitions;
  portEXIT_CRITICAL(&encoderMux);
}

void updateEncoderLog() {
  int s1;
  int s2;
  int32_t value;
  uint32_t transitions;
  uint32_t invalidTransitions;
  readEncoderSnapshot(s1, s2, value, transitions, invalidTransitions);

  if (transitions == lastReportedEncoderTransitionCount) {
    return;
  }

  uint32_t newTransitions = transitions - lastReportedEncoderTransitionCount;
  lastReportedEncoderTransitionCount = transitions;

  Serial.print(millis());
  Serial.print(" ms encoder S1=");
  Serial.print(s1);
  Serial.print(" S2=");
  Serial.print(s2);
  Serial.print(" new transitions=");
  Serial.print(newTransitions);
  Serial.print(" invalid total=");
  Serial.print(invalidTransitions);
  Serial.print(" value=");
  Serial.println(value);
}

void logDigitalChange(const char* name, int pin, int state, int activeState) {
  Serial.print(millis());
  Serial.print(" ms ");
  Serial.print(name);
  Serial.print(" GPIO");
  Serial.print(pin);
  Serial.print("=");
  Serial.print(state == HIGH ? "HIGH" : "LOW");
  Serial.print(" active=");
  Serial.println(state == activeState ? "yes" : "no");
}

void updateDigitalInputs() {
  uint32_t now = millis();

  if (encoderEnabled) {
    int button = digitalRead(ENCODER_BUTTON_PIN);
    if (button != lastButton) {
      lastButton = button;
      logDigitalChange("KEY", ENCODER_BUTTON_PIN, button, LOW);
    }
  }

  if (!sensorsEnabled) {
    return;
  }

  int pir = digitalRead(PIR_PIN);
  if (pir != lastPir) {
    lastPir = pir;
    logDigitalChange("PIR", PIR_PIN, pir, PIR_MOTION_STATE);
  }

  int sound = digitalRead(SOUND_PIN);
  if (sound != lastSound) {
    lastSound = sound;
    logDigitalChange("MIC", SOUND_PIN, sound, SOUND_DETECTED_STATE);
  }

  if (sound == SOUND_DETECTED_STATE) {
    micActiveUntilMs = now + MIC_DISPLAY_HOLD_MS;
  }
}

void updateBatteryReading() {
  uint32_t totalMilliVolts = 0;

  for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; ++i) {
    totalMilliVolts += analogReadMilliVolts(BATTERY_ADC_PIN);
  }

  batteryPinMilliVolts = totalMilliVolts / BATTERY_SAMPLE_COUNT;
  float dividerRatio = (BATTERY_R1_OHMS + BATTERY_R2_OHMS) / BATTERY_R2_OHMS;
  batteryVolts = (batteryPinMilliVolts / 1000.0f) * dividerRatio;
}

void drawCell(int column, int row, const char* label, const String& value, bool active) {
  int x = column * CELL_WIDTH;
  int y = GRID_TOP + row * CELL_HEIGHT;
  int width = column == GRID_COLUMNS - 1 ? SCREEN_WIDTH - x : CELL_WIDTH;
  int height = row == GRID_ROWS - 1 ? SCREEN_HEIGHT - y : CELL_HEIGHT;

  if (active) {
    display.fillRect(x, y, width, height, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRect(x, y, width, height, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setCursor(x + 2, y + 2);
  display.print(label);
  display.setCursor(x + 2, y + 11);
  display.print(value);
}

void drawDisplay() {
  int encoderS1;
  int encoderS2;
  int32_t displayedEncoderValue;
  uint32_t encoderTransitions;
  uint32_t encoderErrors;
  readEncoderSnapshot(
    encoderS1,
    encoderS2,
    displayedEncoderValue,
    encoderTransitions,
    encoderErrors
  );

  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 1);
  display.print(displayedEncoderValue);

  display.setTextSize(1);
  display.setCursor(49, 1);
  display.print(outputStageName(outputStage));
  display.setCursor(49, 10);
  display.print(outputIsOn ? "ON" : "gap");

  display.setCursor(102, 1);
  display.print("E:");
  display.print(encoderErrors);

  drawCell(0, 0, "S1", encoderS1 == HIGH ? "HIGH" : "LOW", encoderS1 == LOW);
  drawCell(1, 0, "S2", encoderS2 == HIGH ? "HIGH" : "LOW", encoderS2 == LOW);
  drawCell(2, 0, "KEY", lastButton == HIGH ? "HIGH" : "LOW", lastButton == LOW);
  drawCell(0, 1, "PIR", lastPir == PIR_MOTION_STATE ? "ACTIVE" : "idle",
           lastPir == PIR_MOTION_STATE);
  drawCell(1, 1, "MIC", lastSound == HIGH ? "HIGH" : "LOW",
           (int32_t)(micActiveUntilMs - millis()) > 0);
  drawCell(2, 1, "BAT", String(batteryVolts, 2) + "V", false);

  display.display();
}

void printStatus() {
  int encoderS1;
  int encoderS2;
  int32_t displayedEncoderValue;
  uint32_t encoderTransitions;
  uint32_t encoderErrors;
  readEncoderSnapshot(
    encoderS1,
    encoderS2,
    displayedEncoderValue,
    encoderTransitions,
    encoderErrors
  );

  Serial.print(millis());
  Serial.print(" ms status S1=");
  Serial.print(encoderS1);
  Serial.print(" S2=");
  Serial.print(encoderS2);
  Serial.print(" KEY=");
  Serial.print(lastButton);
  Serial.print(" PIR=");
  Serial.print(lastPir);
  Serial.print(" MIC=");
  Serial.print(lastSound);
  Serial.print(" encoder=");
  Serial.print(displayedEncoderValue);
  Serial.print(" transitions=");
  Serial.print(encoderTransitions);
  Serial.print(" invalid=");
  Serial.print(encoderErrors);
  Serial.print(" batteryPin=");
  Serial.print(batteryPinMilliVolts);
  Serial.print("mV battery=");
  Serial.print(batteryVolts, 3);
  Serial.println("V");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  pinMode(ENCODER_S1_PIN, INPUT_PULLUP);
  pinMode(ENCODER_S2_PIN, INPUT_PULLUP);
  pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);
  pinMode(ACTIVE_PIEZO_PIN, OUTPUT);

  if (ACTIVE_PIEZO_LED_PIN != ACTIVE_PIEZO_PIN) {
    pinMode(ACTIVE_PIEZO_LED_PIN, OUTPUT);
  }

  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

  bool mainPiezoReady = ledcAttach(
    MAIN_PIEZO_PIN,
    PASSIVE_TEST_FREQUENCY_HZ,
    PASSIVE_PIEZO_PWM_RESOLUTION_BITS
  );
  bool harmonyPiezoReady = ledcAttach(
    HARMONY_PIEZO_PIN,
    PASSIVE_TEST_FREQUENCY_HZ,
    PASSIVE_PIEZO_PWM_RESOLUTION_BITS
  );

  lastS1 = digitalRead(ENCODER_S1_PIN);
  lastS2 = digitalRead(ENCODER_S2_PIN);
  lastEncoderState = (lastS1 << 1) | lastS2;
  lastButton = digitalRead(ENCODER_BUTTON_PIN);
  lastPir = digitalRead(PIR_PIN);
  lastSound = digitalRead(SOUND_PIN);

  attachInterrupt(digitalPinToInterrupt(ENCODER_S1_PIN), handleEncoderEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_S2_PIN), handleEncoderEdge, CHANGE);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  scanI2cBus();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("ERROR: SSD1306 display initialization failed.");
    while (true) {
      delay(100);
    }
  }

  runOledVisualSelfTest();

  Serial.println();
  Serial.println("Timer hardware diagnostic");
  Serial.println("Input grid: S1, S2, KEY, PIR, MIC, BAT");
  Serial.print("Encoder pins S1/S2/KEY: ");
  Serial.print(ENCODER_S1_PIN);
  Serial.print("/");
  Serial.print(ENCODER_S2_PIN);
  Serial.print("/");
  Serial.println(ENCODER_BUTTON_PIN);
  Serial.print("Sensor pins PIR/MIC/BAT: ");
  Serial.print(PIR_PIN);
  Serial.print("/");
  Serial.print(SOUND_PIN);
  Serial.print("/");
  Serial.println(BATTERY_ADC_PIN);
  Serial.print("Output pins MAIN/HARMONY/ACTIVE/LED: ");
  Serial.print(MAIN_PIEZO_PIN);
  Serial.print("/");
  Serial.print(HARMONY_PIEZO_PIN);
  Serial.print("/");
  Serial.print(ACTIVE_PIEZO_PIN);
  Serial.print("/");
  Serial.println(ACTIVE_PIEZO_LED_PIN);
  Serial.print("Main passive piezo PWM: ");
  Serial.println(mainPiezoReady ? "ready" : "ERROR");
  Serial.print("Harmony passive piezo PWM: ");
  Serial.println(harmonyPiezoReady ? "ready" : "ERROR");
  printSerialMenu();

  updateBatteryReading();
  startCurrentOutput();
  drawDisplay();
}

void loop() {
  uint32_t now = millis();

  handleSerialCommands();
  if (encoderEnabled) {
    updateEncoderLog();
  }
  updateDigitalInputs();
  updateOutputTest();

  if (oledEnabled && now - lastDisplayMs >= DISPLAY_UPDATE_MS) {
    lastDisplayMs = now;
    drawDisplay();
  }

  if (batteryEnabled && now - lastBatteryUpdateMs >= BATTERY_UPDATE_INTERVAL_MS) {
    lastBatteryUpdateMs = now;
    updateBatteryReading();
  }

  if (now - lastSerialStatusMs >= SERIAL_STATUS_MS) {
    lastSerialStatusMs = now;
    printStatus();
  }
}
