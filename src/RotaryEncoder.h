/*
 * RotaryEncoder Library
 *
 * A lightweight and user-friendly rotary encoder library
 * for Arduino-compatible platforms.
 *
 * Developed by Paul Thomsen
 *
 * Copyright (c) 2026 Ruben Paul Thomsen
 *
 * This library is open-source and released under the MIT License.
 * You are free to use, modify, and distribute this software
 * in accordance with the terms of the license.
 *
 * If you find this library useful, please consider supporting
 * open source development by sharing your projects or contributing.
 *
 * Full license text available in the LICENSE file.
 * All text above must be included in any redistribution.
 */

#ifndef ROTARY_ENCODER_H
#define ROTARY_ENCODER_H

#include <Arduino.h>

#ifndef ROTARY_ENCODER_MAX_INSTANCES
#define ROTARY_ENCODER_MAX_INSTANCES 8
#endif

#ifndef ROTARY_ENCODER_EVENT_QUEUE_SIZE
#define ROTARY_ENCODER_EVENT_QUEUE_SIZE 8
#endif

#ifndef ROTARY_ENCODER_MAX_INTERRUPT_SLOTS
#define ROTARY_ENCODER_MAX_INTERRUPT_SLOTS 16
#endif

#ifndef NOT_AN_INTERRUPT
#define NOT_AN_INTERRUPT -1
#endif

class RotaryEncoder {
public:
  enum class EventType : uint8_t {
    None = 0,
    RotateCW,
    RotateCCW,
    Press,
    Release,
    Click,
    DoubleClick,
    LongPress,
    Repeat
  };

  struct Event {
    EventType type;
    int16_t value;
    uint32_t timestamp;
  };

  struct Config {
    bool reverse = false;
    bool usePullups = true;

    // Number of valid quadrature sub-steps per logical step
    uint8_t stepsPerDetent = 4;

    bool buttonEnabled = true;
    bool repeatEnabled = false;
    bool accelerationEnabled = false;

    uint16_t debounceTime = 25;

    // 0 = disabled
    uint16_t longPressTime = 0;
    uint16_t doubleClickTime = 0;

    uint16_t repeatDelay = 450;
    uint16_t repeatInterval = 120;

    uint16_t accelWindow = 80;
    uint8_t accelSlowFactor = 1;
    uint8_t accelFastFactor = 2;
  };

  RotaryEncoder(uint8_t pinA, uint8_t pinB, int8_t buttonPin = -1);

  bool begin(bool useInterrupts = false);
  bool begin(const Config& config, bool useInterrupts = false);
  void end();

  void update();

  // Rotation
  int16_t read();
  int16_t peek() const;
  void clear();
  bool moved() const;

  // Event queue
  bool hasEvent() const;
  bool getEvent(Event& event);
  void clearEvents();

  // Simple one-shot flag API
  bool press();
  bool release();
  bool click();
  bool doubleClick();
  bool longPress();
  bool repeat();

  // Optional aliases
  bool pressed()  { return press(); }
  bool released() { return release(); }
  bool clicked()  { return click(); }
  bool repeated() { return repeat(); }

  const Config& getConfig() const;

  // Chainable setters
  RotaryEncoder& setReverse(bool enabled = true);
  RotaryEncoder& setStepsPerDetent(uint8_t steps);
  RotaryEncoder& setDebounceTime(uint16_t ms);
  RotaryEncoder& enableLongPress(uint16_t ms);     // 0 = disabled
  RotaryEncoder& enableDoubleClick(uint16_t ms);   // 0 = disabled
  RotaryEncoder& setRepeatTiming(uint16_t delayMs, uint16_t intervalMs);
  RotaryEncoder& setAcceleration(uint16_t windowMs,
                                 uint8_t slowFactor = 1,
                                 uint8_t fastFactor = 2);

  RotaryEncoder& enableButton(bool enabled = true);
  RotaryEncoder& enableRepeat(bool enabled = true);
  RotaryEncoder& enableAcceleration(bool enabled = true);
  RotaryEncoder& useInternalPullups(bool enabled = true);

  // Optional interrupts
  bool enableInterrupts();
  void disableInterrupts();
  bool usingInterrupts() const;

private:
  uint8_t _pinA;
  uint8_t _pinB;
  int8_t _buttonPin;

  Config _config;

  volatile int16_t _delta;
  volatile int8_t _accum;
  volatile uint8_t _prevAB;
  volatile bool _dirty;

  bool _started;
  bool _interruptsEnabled;

  int8_t _interruptA;
  int8_t _interruptB;

  // Button state
  bool _buttonStableState;
  bool _buttonLastRawState;
  uint32_t _buttonLastChangeMs;
  uint32_t _buttonPressMs;
  uint32_t _buttonReleaseMs;
  uint32_t _lastClickMs;
  uint32_t _lastRepeatMs;
  bool _longPressSent;
  bool _clickPending;

  // Acceleration
  uint32_t _lastStepMs;

  // One-shot flags
  bool _flagPress;
  bool _flagRelease;
  bool _flagClick;
  bool _flagDoubleClick;
  bool _flagLongPress;
  bool _flagRepeat;

  // Event queue
  Event _queue[ROTARY_ENCODER_EVENT_QUEUE_SIZE];
  volatile uint8_t _queueHead;
  volatile uint8_t _queueTail;

  uint8_t readAB() const;
  void updateEncoder();
  void updateButton();
  void handleABChange();
  int8_t decodeTransition(uint8_t prev, uint8_t now) const;
  uint8_t computeStepFactor(uint32_t nowMs) const;

  void pushEventFromMain(EventType type, int16_t value, uint32_t nowMs);
  void pushEventFromISR(EventType type, int16_t value, uint32_t nowMs);

  bool popFlag(bool& flag);
  void applyConfigDefaults();
  bool queueIsEmpty() const;

  void registerInstance();
  void unregisterInstance();

  static void markDirtyFromInterrupt(uint8_t interruptNumber);

  static RotaryEncoder* _instances[ROTARY_ENCODER_MAX_INSTANCES];
  static RotaryEncoder* _interruptOwners[ROTARY_ENCODER_MAX_INTERRUPT_SLOTS];

  static void isrRouter0();
  static void isrRouter1();
  static void isrRouter2();
  static void isrRouter3();
  static void isrRouter4();
  static void isrRouter5();
  static void isrRouter6();
  static void isrRouter7();
  static void isrRouter8();
  static void isrRouter9();
  static void isrRouter10();
  static void isrRouter11();
  static void isrRouter12();
  static void isrRouter13();
  static void isrRouter14();
  static void isrRouter15();
};

#endif