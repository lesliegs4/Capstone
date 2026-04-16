#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#
#include "serial.h"
#
#ifndef F_CPU
#error "F_CPU must be defined (e.g. via -DF_CPU=...)"
#endif
#
// Gravity TDS (SEN0244) quick test
// - Board pins:  + -> VCC, - -> GND, A -> ADC input (default ADC1 / PC1 / A1)
// - Prints median ADC code, computed mV, and estimated TDS (ppm) at assumed temperature.
#
#ifndef TDS_ADC_CHANNEL
#define TDS_ADC_CHANNEL 1U  // ADC1 = PC1 = Arduino A1
#endif
#
#ifndef VREF_MV
#define VREF_MV 5000U       // set to 3300 if using 3.3V AVcc reference
#endif
#
#ifndef TEMP_C_X10
#define TEMP_C_X10 250      // 25.0C (sensor has no temperature probe)
#endif

// --- Saltwater "recalibration" (scaled estimate) ---
// The SEN0244 kit is specified for ~0..1000 ppm TDS. In real seawater it will
// typically rail/saturate near the top of that range. If you still want a
// "saltwater equivalent" number for your project, you can scale the computed
// TDS ppm so that your measured seawater sample maps to a target ppm.
//
// Example (your report):
//   measured TDS=921 ppm in real seawater
//   target seawater ~35000 ppm
// => scale ~ 35000/921 ≈ 38.0
#ifndef SEAWATER_EQ_PPM
#define SEAWATER_EQ_PPM 35000UL
#endif

#ifndef SEAWATER_SENSOR_TDS_PPM
#define SEAWATER_SENSOR_TDS_PPM 921UL
#endif
#
#define SCOUNT 30U
#
static void serial_out_str(const char *s)
{
    while (*s) serial_out(*s++);
}
#
static void serial_out_u16(uint16_t v)
{
    char buf[6];
    uint8_t i = 0;
    if (v == 0) { serial_out('0'); return; }
    while (v && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i--) serial_out(buf[i]);
}
#
static void adc_init(uint8_t channel)
{
    // AVcc reference, right adjust, select ADC channel (0..7)
    ADMUX = (1 << REFS0) | (channel & 0x07);
#
    // Enable ADC, prescaler 128 (ADC clk = F_CPU/128)
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
#
    // Disable digital input buffer on the ADC pin (reduces noise)
    if (channel <= 5) DIDR0 |= (1 << channel);
#
    _delay_ms(2);
}
#
static uint16_t adc_read(void)
{
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {}
    return (uint16_t)ADC;
}
#
static uint16_t median_u16(uint16_t *a, uint8_t n)
{
    // In-place bubble sort (n is small)
    for (uint8_t j = 0; j + 1 < n; j++) {
        for (uint8_t i = 0; i + 1 < n - j; i++) {
            if (a[i] > a[i + 1]) {
                uint16_t t = a[i];
                a[i] = a[i + 1];
                a[i + 1] = t;
            }
        }
    }
    if (n & 1) return a[n / 2];
    return (uint16_t)((a[n / 2] + a[(n / 2) - 1]) / 2U);
}
#
int main(void)
{
    serial_init();
    serial_out_str("TDS TEST (SEN0244)\r\n");
    serial_out_str("ADC=");
    serial_out_u16((uint16_t)TDS_ADC_CHANNEL);
    serial_out_str(" VREF=");
    serial_out_u16((uint16_t)VREF_MV);
    serial_out_str("mV TEMP=");
    serial_out_u16((uint16_t)TEMP_C_X10);
    serial_out_str("/10C\r\n");
#
    adc_init((uint8_t)TDS_ADC_CHANNEL);
#
    while (1) {
        uint16_t samples[SCOUNT];
        for (uint8_t i = 0; i < (uint8_t)SCOUNT; i++) {
            samples[i] = adc_read();
            _delay_ms(40);
        }
#
        uint16_t adc_med = median_u16(samples, (uint8_t)SCOUNT);
        uint16_t mv = (uint16_t)(((uint32_t)adc_med * (uint32_t)VREF_MV) / 1023U);
#
        float v = (float)mv / 1000.0f;
        float temp_c = (float)TEMP_C_X10 / 10.0f;
        float comp_coeff = 1.0f + 0.02f * (temp_c - 25.0f);
        float comp_v = v / comp_coeff;
#
        // From DFRobot sample code / datasheet tutorial.
        float tds = (133.42f * comp_v * comp_v * comp_v
                   - 255.86f * comp_v * comp_v
                   + 857.39f * comp_v) * 0.5f;
        if (tds < 0.0f) tds = 0.0f;
        uint16_t tds_ppm = (uint16_t)(tds + 0.5f);
#
        serial_out_str("ADC_med=");
        serial_out_u16(adc_med);
        serial_out_str(" mV=");
        serial_out_u16(mv);
        serial_out_str(" TDS=");
        serial_out_u16(tds_ppm);
        serial_out_str("ppm");

        // Saltwater-equivalent ppm (scaled)
        uint32_t sw_eq_ppm = 0;
        if (SEAWATER_SENSOR_TDS_PPM != 0UL) {
            sw_eq_ppm = ((uint32_t)tds_ppm * (uint32_t)SEAWATER_EQ_PPM) / (uint32_t)SEAWATER_SENSOR_TDS_PPM;
        }
        serial_out_str(" SW_eq=");
        serial_out_u16((uint16_t)((sw_eq_ppm > 65535UL) ? 65535UL : sw_eq_ppm));
        serial_out_str("ppm\r\n");
#
        _delay_ms(400);
    }
}

