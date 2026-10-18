# Bed-Occupancy Sensor

An ESP32-C3 gadget that knows whether someone is in bed: a force-sensitive
resistor (FSR) under the mattress, sampled once a second, exposed to Muse
as a `bed.occupancy` command. Built for morning accountability — a wake-up
check that can tell if I'm actually out of bed.

## How it works

The FSR is wired as a voltage divider (3V3 → FSR → ADC pin → 460 Ω pulldown
→ GND), so the ADC reading rises with pressure. A FreeRTOS task samples the
divider once per second (8-sample average) and feeds each reading into a
small state machine:

- **Above threshold → in bed immediately.** No dwell; a false "in bed" is
  harmless.
- **Below threshold → out of bed only after 60 s continuously below.**
  Any single above-threshold sample (a roll-over) restarts the dwell, so
  tossing and turning never flaps the state.

The state machine is fenced off from the sampler so it compiles natively:
`tests/` extracts it and checks dwell timing, spike-restart, the
on-the-line edge case, and millisecond-counter wrap.

## Hardware

- ESP32-C3 SuperMini (ESP32-C3FN4, native USB-C)
- FSR under the mattress, ~1/3 from the head end, sensing side up
- 30 mm × 7 mm chamfered PLA puck between FSR and mattress to focus
  pressure on the sensing area
- Stack: bed slat → FSR → puck → mattress

## Calibration

The occupied threshold is a raw 12-bit ADC value (default 2900), tuned in
place since every mattress loads the sensor differently. On my build:
~1800 mattress-alone, 3600–4000 in bed.

- `fsr-bench/` is an Arduino sketch that streams live raw readings over
  serial (send `l` / `u` to tag loaded / unloaded) for capturing those
  numbers before the threshold is set.
- On the gadget, `bed_threshold=<raw>` over the serial console stores a new
  threshold (500–4000) in NVS — no reflash needed. `bed_status` prints the
  live state.

## The `bed.occupancy` command

Once paired through the Muse Gadgets SDK, the device answers:

```json
{ "occupied": true, "raw": 3642, "threshold": 2900, "stable_s": 41203 }
```
# Bed Sensor Add-on

`stable_s` indicates the duration (in seconds) for which the reported state has been continuously held.

## 📂 Repository Layout

* **bed_sensor.c / bed_sensor.h** — Contains the sampler task, debounce state machine, `bed.occupancy` handler, and console commands.

* **sdkconfig.c3-supermini** — Board overlay configuration. Enables the sensor, configures ADC GPIO 4, and sets up the status LED.


### Testing & Calibration
* Host harness (`link_bed_sensor_harness.c`) and runner (`test_link_bed_sensor.py`) used to validate the state machine. This runs locally on a host computer and requires no hardware.
 Arduino calibration sketch designed specifically for the ESP32-C3 SuperMini.

