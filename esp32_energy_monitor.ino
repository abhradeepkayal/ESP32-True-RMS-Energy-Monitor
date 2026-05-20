#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ===== HARDWARE CONFIG =====
Adafruit_ADS1115 ads;
#define RELAY_PIN 26

// ===== SAMPLING CONFIG =====
// 400 samples at 860SPS captures about 0.46 seconds (23 full 50Hz cycles)
#define SAMPLES 400

// ===== CALIBRATION =====
#define V_CALIBRATION 306
#define I_CALIBRATION 2.36

#define PHASE_SHIFT 1.45
// ===== ADC VOLTAGE CONSTANTS =====
// At GAIN_TWO (Voltage sensor), 1 bit = 0.0000625V
#define VOLT_STEP_V 0.0000625
// At GAIN_SIXTEEN (Current sensor), 1 bit = 0.0000078125V
#define VOLT_STEP_I 0.0000078125

int abnormal_count = 0;

void setup()
{
    Serial.begin(115200);
    Wire.begin(21, 22);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // most relay modules are ACTIVE LOW

    if (!ads.begin())
    {
        Serial.println("ADS1115 not found at 0x48!");
        while (1)
            ;
    }

    // Set to max speed
    ads.setDataRate(RATE_ADS1115_860SPS);

    Serial.println("=== 16-Bit Energy Monitor Initialized ===");
}

void loop()
{
    float sumVsq = 0, sumIsq = 0, sumVI = 0;
    float lastV = 0; // Stores the previous voltage sample for interpolation

    // --- PHASE 1: Find DC Midpoints (Static Offsets) ---
    // Midpoints are calculated once per loop to handle thermal drift
    ads.setGain(GAIN_TWO);
    long vMid_raw = 0;
    for (int i = 0; i < 50; i++)
        vMid_raw += ads.readADC_SingleEnded(2);
    float vMid = (float)vMid_raw / 50.0;

    ads.setGain(GAIN_SIXTEEN);
    long iMid_raw = 0;
    for (int i = 0; i < 50; i++)
        iMid_raw += ads.readADC_Differential_0_1();
    float iMid = (float)iMid_raw / 50.0;

    // --- PHASE 2: High Speed Sampling with Phase Correction ---
    for (int i = 0; i < SAMPLES; i++)
    {
        // 1. Read Voltage (ZMPT on A2)
        ads.setGain(GAIN_TWO);
        float rawV = (float)ads.readADC_SingleEnded(2) - vMid;
        float instV = rawV * VOLT_STEP_V * V_CALIBRATION;

        // 2. Read Current (SCT on A0-A1)
        ads.setGain(GAIN_SIXTEEN);
        float rawI = (float)ads.readADC_Differential_0_1() - iMid;
        float instI = rawI * VOLT_STEP_I * I_CALIBRATION;

        // 3. PHASE SHIFT CORRECTION
        // This math predicts where the voltage was at the exact moment the current was sampled.
        // Start with PHASE_SHIFT = 0.9 or 1.0. Adjust until PF is maxed (approx 1.0) for a bulb.
        float correctedV = lastV + (PHASE_SHIFT * (instV - lastV));
        lastV = instV; // Save current V for the next iteration's interpolation

        // 4. Accumulate
        sumVsq += instV * instV;
        sumIsq += instI * instI;
        sumVI += (correctedV * instI); // Use the corrected voltage for Real Power
    }

    // --- PHASE 3: Final Calculations ---
    float Vrms = sqrt(sumVsq / SAMPLES);
    float Irms = sqrt(sumIsq / SAMPLES);
    float realPower = (sumVI / SAMPLES);
    float apparentPower = Vrms * Irms;

    // Power Factor
    float pf = 0;
    if (apparentPower > 0.05)
    {
        pf = realPower / apparentPower;
        pf = constrain(pf, -1.0, 1.0);
    }

    if (Irms > 0.60 || realPower > 120)
    {
        abnormal_count++;

        abnormal_count = min(abnormal_count, 10);
    }
    else
    {
        if (abnormal_count > 0)
            abnormal_count--;
    }

    if (abnormal_count >= 3)
    {
        digitalWrite(RELAY_PIN, HIGH);
    }

    // --- PHASE 4: Serial Output --
    Serial.print("Vrms: ");
    Serial.print(Vrms, 1);
    Serial.print("V | Irms: ");
    Serial.print(Irms, 4);
    Serial.print("A | Watt: ");
    Serial.print(realPower, 2);
    Serial.print("W | PF: ");
    Serial.println(pf, 3);

    delay(1500);
}
