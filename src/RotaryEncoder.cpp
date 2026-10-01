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

#include "RotaryEncoder.h"

RotaryEncoder* RotaryEncoder::_instances[ROTARY_ENCODER_MAX_INSTANCES] = { nullptr };
RotaryEncoder* RotaryEncoder::_interruptOwners[ROTARY_ENCODER_MAX_INTERRUPT_SLOTS] = { nullptr };

RotaryEncoder::RotaryEncoder(uint8_t pinA, uint8_t pinB, int8_t buttonPin)
  : _pinA(pinA),
    _pinB(pinB),
    _buttonPin(buttonPin),
    _delta(0),
    _accum(0),
    _prevAB(0),
    _dirty(false),
    _started(false),
    _interruptsEnabled(false),
    _interruptA(-1),
    _interruptB(-1),
    _buttonStableState(true),
    _buttonLastRawState(true),
    _buttonLastChangeMs(0),
    _buttonPressMs(0),
    _buttonReleaseMs(0),
    _lastClickMs(0),
    _lastRepeatMs(0),
    _longPressSent(false),
    _clickPending(false),
    _lastStepMs(0),
    _flagPress(false),
    _flagRelease(false),
    _flagClick(false),
    _flagDoubleClick(false),
    _flagLongPress(false),
    _flagRepeat(false),
    _queueHead(0),
    _queueTail(0) {
}

void RotaryEncoder::applyConfigDefaults() {
  if (_config.stepsPerDetent == 0) {
    _config.stepsPerDetent = 1;
  }

  if (_config.accelSlowFactor == 0) {
    _config.accelSlowFactor = 1;
  }

  if (_config.accelFastFactor == 0) {
    _config.accelFastFactor = 1;
  }
}

bool RotaryEncoder::begin(bool useInterrupts) {
  applyConfigDefaults();

  pinMode(_pinA, _config.usePullups ? INPUT_PULLUP : INPUT);
  pinMode(_pinB, _config.usePullups ? INPUT_PULLUP : INPUT);

  if (_buttonPin >= 0 && _config.buttonEnabled) {
    pinMode((uint8_t)_buttonPin, _config.usePullups ? INPUT_PULLUP : INPUT);
    _buttonStableState = digitalRead((uint8_t)_buttonPin);
    _buttonLastRawState = _buttonStableState;
  } else {
    _buttonStableState = true;
    _buttonLastRawState = true;
  }

  _prevAB = readAB();
  _accum = 0;
  _delta = 0;
  _dirty = false;

  _buttonLastChangeMs = millis();
  _buttonPressMs = 0;
  _buttonReleaseMs = 0;
  _lastClickMs = 0;
  _lastRepeatMs = 0;
  _lastStepMs = 0;
  _longPressSent = false;
  _clickPending = false;

  _flagPress = false;
  _flagRelease = false;
  _flagClick = false;
  _flagDoubleClick = false;
  _flagLongPress = false;
  _flagRepeat = false;

  _interruptA = -1;
  _interruptB = -1;

  clearEvents();

  registerInstance();
  _started = true;

  if (useInterrupts) {
    if (!enableInterrupts()) {
      return false;
    }
  }

  return true;
}

bool RotaryEncoder::begin(const Config& config, bool useInterrupts) {
  _config = config;
  return begin(useInterrupts);
}

void RotaryEncoder::end() {
  disableInterrupts();
  unregisterInstance();
  _started = false;
}

void RotaryEncoder::update() {
  if (!_started) {
    return;
  }

  if (!_interruptsEnabled) {
    updateEncoder();
  } else if (_dirty) {
    noInterrupts();
    _dirty = false;
    interrupts();
    updateEncoder();
  }

  if (_config.buttonEnabled && _buttonPin >= 0) {
    updateButton();
  }
}

int16_t RotaryEncoder::read() {
  noInterrupts();
  int16_t value = _delta;
  _delta = 0;
  interrupts();
  return value;
}

int16_t RotaryEncoder::peek() const {
  noInterrupts();
  int16_t value = _delta;
  interrupts();
  return value;
}

void RotaryEncoder::clear() {
  noInterrupts();
  _delta = 0;
  interrupts();
}

bool RotaryEncoder::moved() const {
  return peek() != 0;
}

bool RotaryEncoder::hasEvent() const {
  return !queueIsEmpty();
}

bool RotaryEncoder::getEvent(Event& event) {
  noInterrupts();

  if (_queueHead == _queueTail) {
    interrupts();
    return false;
  }

  event = _queue[_queueTail];
  _queueTail = (uint8_t)((_queueTail + 1) % ROTARY_ENCODER_EVENT_QUEUE_SIZE);

  interrupts();
  return true;
}

void RotaryEncoder::clearEvents() {
  noInterrupts();
  _queueHead = 0;
  _queueTail = 0;
  interrupts();
}

bool RotaryEncoder::press() {
  return popFlag(_flagPress);
}

bool RotaryEncoder::release() {
  return popFlag(_flagRelease);
}

bool RotaryEncoder::click() {
  return popFlag(_flagClick);
}

bool RotaryEncoder::doubleClick() {
  return popFlag(_flagDoubleClick);
}

bool RotaryEncoder::longPress() {
  return popFlag(_flagLongPress);
}

bool RotaryEncoder::repeat() {
  return popFlag(_flagRepeat);
}

const RotaryEncoder::Config& RotaryEncoder::getConfig() const {
  return _config;
}

RotaryEncoder& RotaryEncoder::setReverse(bool enabled) {
  _config.reverse = enabled;
  return *this;
}

RotaryEncoder& RotaryEncoder::setStepsPerDetent(uint8_t steps) {
  _config.stepsPerDetent = (steps == 0) ? 1 : steps;
  return *this;
}

RotaryEncoder& RotaryEncoder::setDebounceTime(uint16_t ms) {
  _config.debounceTime = ms;
  return *this;
}

RotaryEncoder& RotaryEncoder::enableLongPress(uint16_t ms) {
  _config.longPressTime = ms;
  return *this;
}

RotaryEncoder& RotaryEncoder::enableDoubleClick(uint16_t ms) {
  _config.doubleClickTime = ms;
  return *this;
}

RotaryEncoder& RotaryEncoder::setRepeatTiming(uint16_t delayMs, uint16_t intervalMs) {
  _config.repeatDelay = delayMs;
  _config.repeatInterval = intervalMs;
  return *this;
}

RotaryEncoder& RotaryEncoder::setAcceleration(uint16_t windowMs,
                                              uint8_t slowFactor,
                                              uint8_t fastFactor) {
  _config.accelWindow = windowMs;
  _config.accelSlowFactor = (slowFactor == 0) ? 1 : slowFactor;
  _config.accelFastFactor = (fastFactor == 0) ? 1 : fastFactor;
  return *this;
}

RotaryEncoder& RotaryEncoder::enableButton(bool enabled) {
  _config.buttonEnabled = enabled;
  return *this;
}

RotaryEncoder& RotaryEncoder::enableRepeat(bool enabled) {
  _config.repeatEnabled = enabled;
  return *this;
}

RotaryEncoder& RotaryEncoder::enableAcceleration(bool enabled) {
  _config.accelerationEnabled = enabled;
  return *this;
}

RotaryEncoder& RotaryEncoder::useInternalPullups(bool enabled) {
  _config.usePullups = enabled;
  return *this;
}

bool RotaryEncoder::enableInterrupts() {
  int interruptA = digitalPinToInterrupt(_pinA);
  int interruptB = digitalPinToInterrupt(_pinB);

  if (interruptA == NOT_AN_INTERRUPT || interruptB == NOT_AN_INTERRUPT) {
    return false;
  }

  if (interruptA < 0 || interruptA >= ROTARY_ENCODER_MAX_INTERRUPT_SLOTS) {
    return false;
  }

  if (interruptB < 0 || interruptB >= ROTARY_ENCODER_MAX_INTERRUPT_SLOTS) {
    return false;
  }

  disableInterrupts();

  _interruptA = (int8_t)interruptA;
  _interruptB = (int8_t)interruptB;

  _interruptOwners[interruptA] = this;
  _interruptOwners[interruptB] = this;

  switch (interruptA) {
    case 0: attachInterrupt((uint8_t)interruptA, isrRouter0, CHANGE); break;
    case 1: attachInterrupt((uint8_t)interruptA, isrRouter1, CHANGE); break;
    case 2: attachInterrupt((uint8_t)interruptA, isrRouter2, CHANGE); break;
    case 3: attachInterrupt((uint8_t)interruptA, isrRouter3, CHANGE); break;
    case 4: attachInterrupt((uint8_t)interruptA, isrRouter4, CHANGE); break;
    case 5: attachInterrupt((uint8_t)interruptA, isrRouter5, CHANGE); break;
    case 6: attachInterrupt((uint8_t)interruptA, isrRouter6, CHANGE); break;
    case 7: attachInterrupt((uint8_t)interruptA, isrRouter7, CHANGE); break;
    case 8: attachInterrupt((uint8_t)interruptA, isrRouter8, CHANGE); break;
    case 9: attachInterrupt((uint8_t)interruptA, isrRouter9, CHANGE); break;
    case 10: attachInterrupt((uint8_t)interruptA, isrRouter10, CHANGE); break;
    case 11: attachInterrupt((uint8_t)interruptA, isrRouter11, CHANGE); break;
    case 12: attachInterrupt((uint8_t)interruptA, isrRouter12, CHANGE); break;
    case 13: attachInterrupt((uint8_t)interruptA, isrRouter13, CHANGE); break;
    case 14: attachInterrupt((uint8_t)interruptA, isrRouter14, CHANGE); break;
    case 15: attachInterrupt((uint8_t)interruptA, isrRouter15, CHANGE); break;
    default: return false;
  }

  if (interruptB != interruptA) {
    switch (interruptB) {
      case 0: attachInterrupt((uint8_t)interruptB, isrRouter0, CHANGE); break;
      case 1: attachInterrupt((uint8_t)interruptB, isrRouter1, CHANGE); break;
      case 2: attachInterrupt((uint8_t)interruptB, isrRouter2, CHANGE); break;
      case 3: attachInterrupt((uint8_t)interruptB, isrRouter3, CHANGE); break;
      case 4: attachInterrupt((uint8_t)interruptB, isrRouter4, CHANGE); break;
      case 5: attachInterrupt((uint8_t)interruptB, isrRouter5, CHANGE); break;
      case 6: attachInterrupt((uint8_t)interruptB, isrRouter6, CHANGE); break;
      case 7: attachInterrupt((uint8_t)interruptB, isrRouter7, CHANGE); break;
      case 8: attachInterrupt((uint8_t)interruptB, isrRouter8, CHANGE); break;
      case 9: attachInterrupt((uint8_t)interruptB, isrRouter9, CHANGE); break;
      case 10: attachInterrupt((uint8_t)interruptB, isrRouter10, CHANGE); break;
      case 11: attachInterrupt((uint8_t)interruptB, isrRouter11, CHANGE); break;
      case 12: attachInterrupt((uint8_t)interruptB, isrRouter12, CHANGE); break;
      case 13: attachInterrupt((uint8_t)interruptB, isrRouter13, CHANGE); break;
      case 14: attachInterrupt((uint8_t)interruptB, isrRouter14, CHANGE); break;
      case 15: attachInterrupt((uint8_t)interruptB, isrRouter15, CHANGE); break;
      default: return false;
    }
  }

  _interruptsEnabled = true;
  return true;
}

void RotaryEncoder::disableInterrupts() {
  if (_interruptA >= 0) {
    detachInterrupt((uint8_t)_interruptA);
    if (_interruptA < ROTARY_ENCODER_MAX_INTERRUPT_SLOTS &&
        _interruptOwners[_interruptA] == this) {
      _interruptOwners[_interruptA] = nullptr;
    }
  }

  if (_interruptB >= 0) {
    if (_interruptB != _interruptA) {
      detachInterrupt((uint8_t)_interruptB);
    }
    if (_interruptB < ROTARY_ENCODER_MAX_INTERRUPT_SLOTS &&
        _interruptOwners[_interruptB] == this) {
      _interruptOwners[_interruptB] = nullptr;
    }
  }

  _interruptA = -1;
  _interruptB = -1;
  _interruptsEnabled = false;
}

bool RotaryEncoder::usingInterrupts() const {
  return _interruptsEnabled;
}

uint8_t RotaryEncoder::readAB() const {
  uint8_t a = digitalRead(_pinA) ? 1 : 0;
  uint8_t b = digitalRead(_pinB) ? 1 : 0;
  return (uint8_t)((a << 1) | b);
}

void RotaryEncoder::updateEncoder() {
  handleABChange();
}

void RotaryEncoder::handleABChange() {
  uint8_t nowAB = readAB();
  if (nowAB == _prevAB) {
    return;
  }

  int8_t step = decodeTransition(_prevAB, nowAB);
  _prevAB = nowAB;

  if (_config.reverse) {
    step = -step;
  }

  if (step == 0) {
    _accum = 0;
    return;
  }

  if ((_accum > 0 && step < 0) || (_accum < 0 && step > 0)) {
    _accum = 0;
  }

  _accum += step;

  if (_accum >= (int8_t)_config.stepsPerDetent) {
    uint32_t nowMs = millis();
    uint8_t factor = computeStepFactor(nowMs);
    _delta += factor;
    pushEventFromMain(EventType::RotateCW, factor, nowMs);
    _accum = 0;
    _lastStepMs = nowMs;
  } else if (_accum <= -(int8_t)_config.stepsPerDetent) {
    uint32_t nowMs = millis();
    uint8_t factor = computeStepFactor(nowMs);
    _delta -= factor;
    pushEventFromMain(EventType::RotateCCW, -((int16_t)factor), nowMs);
    _accum = 0;
    _lastStepMs = nowMs;
  }
}

int8_t RotaryEncoder::decodeTransition(uint8_t prev, uint8_t now) const {
  static const int8_t table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  return table[(prev << 2) | now];
}

uint8_t RotaryEncoder::computeStepFactor(uint32_t nowMs) const {
  if (!_config.accelerationEnabled) {
    return 1;
  }

  if (_lastStepMs == 0) {
    return _config.accelSlowFactor;
  }

  uint32_t dt = nowMs - _lastStepMs;
  if (dt <= _config.accelWindow) {
    return _config.accelFastFactor;
  }

  return _config.accelSlowFactor;
}

void RotaryEncoder::updateButton() {
  uint32_t now = millis();
  bool raw = digitalRead((uint8_t)_buttonPin);

  if (raw != _buttonLastRawState) {
    _buttonLastRawState = raw;
    _buttonLastChangeMs = now;
  }

  if ((now - _buttonLastChangeMs) >= _config.debounceTime) {
    if (raw != _buttonStableState) {
      _buttonStableState = raw;

      if (_buttonStableState == LOW) {
        _flagPress = true;
        pushEventFromMain(EventType::Press, 0, now);

        _buttonPressMs = now;
        _lastRepeatMs = now;
        _longPressSent = false;
      } else {
        _flagRelease = true;
        pushEventFromMain(EventType::Release, 0, now);

        _buttonReleaseMs = now;

        if (!_longPressSent) {
          if (_config.doubleClickTime > 0) {
            if (_clickPending && (now - _lastClickMs <= _config.doubleClickTime)) {
              _flagDoubleClick = true;
              pushEventFromMain(EventType::DoubleClick, 0, now);
              _clickPending = false;
            } else {
              _clickPending = true;
              _lastClickMs = now;
            }
          } else {
            _flagClick = true;
            pushEventFromMain(EventType::Click, 0, now);
          }
        }
      }
    }
  }

  // Delayed single click when double click detection is enabled
  if (_config.doubleClickTime > 0 && _clickPending) {
    if ((now - _lastClickMs) > _config.doubleClickTime) {
      _clickPending = false;
      _flagClick = true;
      pushEventFromMain(EventType::Click, 0, now);
    }
  }

  // Held button handling
  if (_buttonStableState == LOW) {
    if (_config.longPressTime > 0 &&
        !_longPressSent &&
        (now - _buttonPressMs >= _config.longPressTime)) {
      _longPressSent = true;
      _flagLongPress = true;
      pushEventFromMain(EventType::LongPress, 0, now);
    }

    if (_config.repeatEnabled &&
        (now - _buttonPressMs >= _config.repeatDelay)) {
      if ((now - _lastRepeatMs) >= _config.repeatInterval) {
        _lastRepeatMs = now;
        _flagRepeat = true;
        pushEventFromMain(EventType::Repeat, 0, now);
      }
    }
  }
}

void RotaryEncoder::pushEventFromMain(EventType type, int16_t value, uint32_t nowMs) {
  noInterrupts();
  pushEventFromISR(type, value, nowMs);
  interrupts();
}

void RotaryEncoder::pushEventFromISR(EventType type, int16_t value, uint32_t nowMs) {
  uint8_t nextHead = (uint8_t)((_queueHead + 1) % ROTARY_ENCODER_EVENT_QUEUE_SIZE);

  if (nextHead == _queueTail) {
    _queueTail = (uint8_t)((_queueTail + 1) % ROTARY_ENCODER_EVENT_QUEUE_SIZE);
  }

  _queue[_queueHead].type = type;
  _queue[_queueHead].value = value;
  _queue[_queueHead].timestamp = nowMs;
  _queueHead = nextHead;
}

bool RotaryEncoder::popFlag(bool& flag) {
  bool value = flag;
  flag = false;
  return value;
}

bool RotaryEncoder::queueIsEmpty() const {
  return _queueHead == _queueTail;
}

void RotaryEncoder::registerInstance() {
  for (uint8_t i = 0; i < ROTARY_ENCODER_MAX_INSTANCES; ++i) {
    if (_instances[i] == nullptr) {
      _instances[i] = this;
      return;
    }
  }
}

void RotaryEncoder::unregisterInstance() {
  for (uint8_t i = 0; i < ROTARY_ENCODER_MAX_INSTANCES; ++i) {
    if (_instances[i] == this) {
      _instances[i] = nullptr;
      return;
    }
  }
}

void RotaryEncoder::markDirtyFromInterrupt(uint8_t interruptNumber) {
  if (interruptNumber >= ROTARY_ENCODER_MAX_INTERRUPT_SLOTS) {
    return;
  }

  RotaryEncoder* owner = _interruptOwners[interruptNumber];
  if (owner != nullptr) {
    owner->_dirty = true;
  }
}

void RotaryEncoder::isrRouter0()  { markDirtyFromInterrupt(0); }
void RotaryEncoder::isrRouter1()  { markDirtyFromInterrupt(1); }
void RotaryEncoder::isrRouter2()  { markDirtyFromInterrupt(2); }
void RotaryEncoder::isrRouter3()  { markDirtyFromInterrupt(3); }
void RotaryEncoder::isrRouter4()  { markDirtyFromInterrupt(4); }
void RotaryEncoder::isrRouter5()  { markDirtyFromInterrupt(5); }
void RotaryEncoder::isrRouter6()  { markDirtyFromInterrupt(6); }
void RotaryEncoder::isrRouter7()  { markDirtyFromInterrupt(7); }
void RotaryEncoder::isrRouter8()  { markDirtyFromInterrupt(8); }
void RotaryEncoder::isrRouter9()  { markDirtyFromInterrupt(9); }
void RotaryEncoder::isrRouter10() { markDirtyFromInterrupt(10); }
void RotaryEncoder::isrRouter11() { markDirtyFromInterrupt(11); }
void RotaryEncoder::isrRouter12() { markDirtyFromInterrupt(12); }
void RotaryEncoder::isrRouter13() { markDirtyFromInterrupt(13); }
void RotaryEncoder::isrRouter14() { markDirtyFromInterrupt(14); }
void RotaryEncoder::isrRouter15() { markDirtyFromInterrupt(15); }