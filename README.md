# FSR bench test (build order, step 2)

Capture loaded vs unloaded ADC values from the FSR406 + divider on the
ESP32-C3 SuperMini. These numbers set the in-bed threshold later.

## Wiring (desk bench, no PCB yet)

| From | To |
|---|---|
| SuperMini 3V3 | FSR tab 1 |
| FSR tab 2 | SuperMini GPIO4 |
| SuperMini GPIO4 | 10 kΩ resistor to SuperMini GND |

FSR resistance drops under force, so the reading **rises** with pressure.
Handle the FSR by the tail end only; never crease it.

## Upload

1. Arduino IDE with the Espressif ESP32 core installed.
2. Board: **ESP32C3 Dev Module**. Set **USB CDC On Boot: Enabled**
   (else no serial output on the SuperMini's native USB).
3. Plug in the SuperMini, select its `/dev/cu.usbmodem*` port, upload.
4. Open the serial monitor at 115200.

This overwrites the Muse gadget firmware on the board. That is expected:
the gadget (with FSR support integrated) gets reflashed after calibration.

## Procedure

1. With nothing on the sensor, type `u` in the serial monitor a few times.
2. Press the puck (or a book) onto the sensing area with roughly torso
   force, hold steady, type `l` a few times.
3. Repeat 1–2 twice more; note how repeatable each state is.

## Report back

- Three `TAG UNLOADED` lines and three `TAG LOADED` lines.
- Roughly how hard you pressed for "loaded" (e.g. book, palm press).
- Any drift: does `live` creep while untouched or while held?

Next: threshold + 60 s dwell firmware, integrated into the gadget build.
