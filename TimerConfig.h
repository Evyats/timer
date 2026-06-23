#ifndef TIMER_CONFIG_H
#define TIMER_CONFIG_H

#include <Arduino.h>

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_ADDRESS = 0x3C;

//// Regular ESP32 pin assignment:
// const int SDA_PIN = 21;
// const int SCL_PIN = 22;
// const int BATTERY_ADC_PIN = 34;
// const int ENCODER_S1_PIN = 32;
// const int ENCODER_S2_PIN = 33;
// const int ENCODER_BUTTON_PIN = 13;
// const int PIR_PIN = 18;
// const int SOUND_PIN = 19;
// const uint8_t MAIN_PIEZO_PIN = 25;
// const uint8_t HARMONY_PIEZO_PIN = 26;
// const uint8_t ACTIVE_PIEZO_PIN = 27;
// const uint8_t ACTIVE_PIEZO_LED_PIN = 14;

//// ESP32-C3 Super Mini pin assignment:
// GPIO2, GPIO8, and GPIO9 are C3 strapping pins. GPIO8 is currently being tested
// for encoder S1.
// const int SDA_PIN = 4;
// const int SCL_PIN = 5;
// const int BATTERY_ADC_PIN = 0;  // ADC1 pin.
// const int ENCODER_S1_PIN = 1;
// const int ENCODER_S2_PIN = 10;
// const int ENCODER_BUTTON_PIN = 6;
// const int PIR_PIN = 20;
// const int SOUND_PIN = 21;
// const uint8_t MAIN_PIEZO_PIN = 7;
// const uint8_t HARMONY_PIEZO_PIN = 8;
// const uint8_t ACTIVE_PIEZO_PIN = 3;
// const uint8_t ACTIVE_PIEZO_LED_PIN = ACTIVE_PIEZO_PIN;

//// Seeed Studio XIAO ESP32C3 pin assignment:
// GPIO2, GPIO8, and GPIO9 are C3 strapping pins. D9 is also connected to BOOT.
const int SDA_PIN = 6;                // D4
const int SCL_PIN = 7;                // D5
const int BATTERY_ADC_PIN = 3;        // D1 / ADC1_CH3
const int ENCODER_S1_PIN = 20;        // D7
const int ENCODER_S2_PIN = 10;        // D10
const int ENCODER_BUTTON_PIN = 9;     // D9 / BOOT
const int PIR_PIN = 4;                // D2 / wake-capable
const int SOUND_PIN = 21;             // D6
const uint8_t MAIN_PIEZO_PIN = 2;     // D0
const uint8_t HARMONY_PIEZO_PIN = 8;  // D8
const uint8_t ACTIVE_PIEZO_PIN = 5;   // D3
const uint8_t ACTIVE_PIEZO_LED_PIN = ACTIVE_PIEZO_PIN;

const bool REVERSE_ENCODER_DIRECTION = true;
const uint32_t BUTTON_DEBOUNCE_MS = 35;
const bool LOG_ENCODER_RAW_STATES = true;

const int PIR_MOTION_STATE = HIGH;
const int SOUND_DETECTED_STATE = LOW;
const unsigned long PIR_WARMUP_MS = 3000;
const uint32_t PIR_CONFIRM_MS = 200;
const uint32_t PIR_DISPLAY_HOLD_MS = 2000;
const uint32_t BUTTON_DISPLAY_HOLD_MS = PIR_DISPLAY_HOLD_MS;
const uint32_t LIGHT_SLEEP_AFTER_MS = 120000;
const uint64_t LIGHT_SLEEP_TO_DEEP_SLEEP_US = 5ULL * 60ULL * 1000000ULL;
const bool ENABLE_LIGHT_SLEEP = true;
const uint8_t SOUND_TRIGGER_MIN_PULSES = 3;
const uint32_t SOUND_TRIGGER_WINDOW_MS = 250;
const uint32_t SOUND_TRIGGER_COOLDOWN_MS = 600;
const bool LOG_RAW_SOUND_EDGES = false;

const uint8_t ACTIVE_PIEZO_VOICE = 0;
const int PIEZO_SELF_TEST_TONE_HZ = 880;
const int PIEZO_SELF_TEST_MS = 300;
const uint16_t PIEZO_SLIDE_UPDATE_INTERVAL_MS = 10;
const uint32_t TIMER_START_DELAY_MS = 2000;
const uint32_t TIMER_COLON_BLINK_DELAY_MS = 1000;

const uint32_t TIME_SYNC_TOTAL_TIMEOUT_MS = 10000;
const char* const ISRAEL_TZ = "IST-2IDT,M3.4.4/26,M10.5.0";
const char* const NTP_SERVER_1 = "pool.ntp.org";
const char* const NTP_SERVER_2 = "time.google.com";
const char* const NTP_SERVER_3 = "time.cloudflare.com";

const int STEP_SECONDS = 60;
const int MAX_SECONDS = 99 * 60 + 59;
const int BATTERY_ADC_SAMPLES = 32;
const int BATTERY_PERCENT_SMOOTHING_WINDOW = 8;
const uint32_t BATTERY_UPDATE_MS = 3000;
const uint32_t BATTERY_SAMPLE_INTERVAL_MS = 2;
const float BATTERY_R1_OHMS = 100000.0;
const float BATTERY_R2_OHMS = 100000.0;

#endif
