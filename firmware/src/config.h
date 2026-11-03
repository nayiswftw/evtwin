#pragma once
// Pin map for Waveshare ESP32-S3-LCD-2-C and the four-button, RGB LED,
// and active buzzer modules. The onboard camera is held in power-down.

// Onboard ST7789 display (240x320; landscape after rotation 1).
#define PIN_TFT_SCK   39
#define PIN_TFT_MOSI  38
#define PIN_TFT_CS    45
#define PIN_TFT_DC    42
#define PIN_TFT_RST   -1
#define PIN_TFT_BL    1
#define TFT_ROTATION  1
#define TFT_INVERT    true

// This LCD-2-C pinout includes the OV5640 camera. Hold its active-high PWDN
// signal asserted because the camera bus GPIOs below are reused by the modules.
#define PIN_CAM_PWDN 17

// Four touch outputs. Power the module from 3V3 to keep signals ESP32-safe.
#define PIN_BTN_LEFT  4
#define PIN_BTN_RIGHT 13
#define PIN_BTN_OK    6
#define PIN_BTN_BACK  7
// This four-key module idles HIGH and pulls its output LOW while touched.
#define TOUCH_ACTIVE_HIGH 0

// DIP RGB module is common cathode and includes current-limiting resistors.
#define PIN_LED_R 8
#define PIN_LED_G 9
#define PIN_LED_B 10
#define RGB_COMMON_CATHODE 1

// Active buzzer signal input.
#define PIN_BUZZER 11
