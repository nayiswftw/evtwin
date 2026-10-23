#pragma once
// ============================================================================
//  PIN MAP: fill in from YOUR board's schematic / pinout page.
//  Set every pin below to a real GPIO number; the build stops until you do.
//  - Display pins: copy them from the Waveshare wiki page for your exact board.
//  - Joystick X/Y must be ADC1-capable pins (GPIO 1-10 on the ESP32-S3).
//  - Pick joystick / button / buzzer pins that the display is NOT using.
// ============================================================================

// ---- Display (ST7789, 240x320, SPI) ----
#define PIN_TFT_SCK   -1
#define PIN_TFT_MOSI  -1
#define PIN_TFT_CS    -1
#define PIN_TFT_DC    -1
#define PIN_TFT_RST   -1   // -1 if the reset line is not wired to a GPIO
#define PIN_TFT_BL    -1   // backlight; -1 if always on
#define TFT_ROTATION  1    // 1 = landscape 320x240, try 3 if upside down
#define TFT_INVERT    true // flip to false if colours look inverted

// ---- Controls ----
#define PIN_JOY_X     -1   // KY-023 VRx (analog)
#define PIN_JOY_Y     -1   // KY-023 VRy (analog)
#define PIN_JOY_SW    -1   // KY-023 SW  (click)
#define PIN_BTN_SPIKE -1   // tactile button to GND
#define PIN_BTN_RESET -1   // tactile button to GND (hold 1 s)
#define PIN_BTN_BACK  -1   // tactile button to GND (switch Bays <-> Grid mode)
#define PIN_BUZZER    -1   // passive buzzer +

// Flip these if the stick moves the wrong way
#define JOY_X_SIGN  1      // +1: pushing right gives a higher reading
#define JOY_Y_SIGN -1      // +1: pushing up gives a higher reading
#define JOY_DEAD    700    // dead zone (ADC counts, 0..4095)

#if PIN_TFT_SCK < 0 || PIN_TFT_MOSI < 0 || PIN_TFT_CS < 0 || PIN_TFT_DC < 0 || \
    PIN_JOY_X < 0 || PIN_JOY_Y < 0 || PIN_JOY_SW < 0 || PIN_BTN_SPIKE < 0 || \
    PIN_BTN_RESET < 0 || PIN_BTN_BACK < 0 || PIN_BUZZER < 0
#error "Open src/config.h and set the GPIO numbers for your board."
#endif
