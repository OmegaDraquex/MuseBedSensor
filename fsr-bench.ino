// Bed-occupancy sensor: FSR bench tool (build order, step 2).
//
// Reads the voltage divider (3V3 -> FSR -> GPIO4 -> 10k -> GND) and prints
// raw ADC + millivolts, so loaded vs unloaded values can be captured before
// the threshold is designed. Target: ESP32-C3 SuperMini (hw-466ab).
//
// Arduino IDE: board "ESP32C3 Dev Module", "USB CDC On Boot: Enabled".
// Flashing this replaces the Muse gadget firmware on the board; the gadget
// gets reflashed (with the FSR code integrated) after calibration.

const int FSR_PIN = 4;          // ADC1_CH4, on the SuperMini headers
const int SAMPLES = 16;         // samples per reading
const int PRINT_MS = 200;       // live print interval

void setup() {
    Serial.begin(115200);
    // Wait briefly for USB CDC; continue headless if no monitor is attached.
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) {
    }
    analogReadResolution(12);            // 0..4095
    analogSetAttenuation(ADC_11db);      // ~0..3.3 V divider swing
    Serial.println(F("# fsr-bench: raw + mV at 5 Hz; send 'l' = tag LOADED, 'u' = tag UNLOADED"));
}

int read_avg() {
    long sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        sum += analogRead(FSR_PIN);
        delay(2);
    }
    return (int)(sum / SAMPLES);
}

void print_reading(const __FlashStringHelper *tag) {
    int raw = read_avg();
    int mv = raw * 3300 / 4095;  // approximate; threshold uses raw
    Serial.print(tag);
    Serial.print(F(" raw="));
    Serial.print(raw);
    Serial.print(F(" mv="));
    Serial.println(mv);
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == 'l' || c == 'L') print_reading(F("TAG LOADED   "));
        else if (c == 'u' || c == 'U') print_reading(F("TAG UNLOADED "));
    }
    print_reading(F("live         "));
    delay(PRINT_MS);
}
