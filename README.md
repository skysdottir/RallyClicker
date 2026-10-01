# A heltec t114-based sequence and time tracker for rally usage
## This is super prototype code, it seems to work but I make no guarantees.

![A little device with a knob and screen. The screen displays time and log entries](hardware/clicker.jpg)

---

## Hardware needed
- Heltec T114
- Heltec GPS module (for time sync)
- Battery
  - 30mm x 40mm x 8mm
  - 1000mah
- Rotary encoder with click-button (for human input)
  - I used a generic KY-040 dev board
- MTS-101 mini toggle switch

## Wiring diagram

![The interior of the device, with wiring displayed. The rotary encoder is connected to the T114](hardware/wiring-nobattery.jpg)

## Pin assignments

- **0.29** Rotary encoder CLK pin
- **0.30** Rotary encoder DT pin
- **0.28** Rotary encoder SW pin
  - (Yes, the IO pins are in this order on the board for some reason)

- **V3.3** Rotary encoder + pin
  - Might also work from Ve3.3, but haven't tried
- **G** Rotary encoder ground pin

---

## Software process

RallyClicker is a PlatformIO project, developed in VS Code. That'll be by far the easiest way to build, run, and flash the project.

In traditional arduino fashion, everything is mashed into main.cpp, with RotaryEncoder .cpp and .h hanging out (instead of properly imported) because I needed to fix a bug.

## Acknowledgements

This project would not exist without the open source community

Graphics and neopixel libraries from Adafruit
Radio library (unused but needed for board) by Jan Gromeš
TinyGPSPlus library by Mikal Hart
Rotary encoder lib borrowed from Paul Thomsen 
Time library by Paul Stoffregen
