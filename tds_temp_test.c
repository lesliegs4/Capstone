#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#
#include "ds18b20.h"
#include "serial.h"
#
#ifndef F_CPU
#error "F_CPU must be defined (e.g. via -DF_CPU=...)"
#endif
#
// Combined test: DS18B20 temperature + Gravity TDS (SEN0244)
// - DS18B20 1-wire bus: PD2 (see ds18b20.c)
// - TDS analog output: ADC channel below (default ADC1 / PC1 / A1)
#
#ifndef TDS_ADC_CHANNEL
#define TDS_ADC_CHANNEL 1U  // ADC1 = PC1 = Arduino A1
#endif
#
#ifndef VREF_MV
#define VREF_MV 5000U       // set to 3300 if using 3.3V AVcc reference
#endif
#
#ifndef SEAWATER_EQ_PPM
#define SEAWATER_EQ_PPM 35000UL
#endif
#
#ifndef SEAWATER_SENSOR_TDS_PPM
#define SEAWATER_SENSOR_TDS_PPM 921UL
#endif
#
#ifndef SCOUNT
#define SCOUNT 30U
#endif
#
#ifndef SAMPLE_DELAY_MS
#define SAMPLE_DELAY_MS 40U
#endif
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
static void serial_out_u32(uint32_t v)
{
    char buf[11];
    uint8_t i = 0;
    if (v == 0) { serial_out('0'); return; }
    while (v && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (char)(v % 10UL));
        v /= 10UL;
    }
    while (i--) serial_out(buf[i]);
}
#
static void serial_out_c_x10(int16_t c_x10)
{
    int32_t v = (int32_t)c_x10;
    if (v < 0) { serial_out('-'); v = -v; }
    serial_out_u16((uint16_t)(v / 10));
    serial_out('.');
    serial_out((char)('0' + (uint8_t)(v % 10)));
}
#
static int16_t c_x16_to_c_x10(int16_t c_x16)
{
    // (C * 10) = (c_x16 * 10) / 16
    int32_t num = (int32_t)c_x16 * 10;
    if (num >= 0) num += 8;   // round to nearest tenth
    else          num -= 8;
    int32_t c_x10 = num / 16;
    if (c_x10 > INT16_MAX) return INT16_MAX;
    if (c_x10 < INT16_MIN) return INT16_MIN;
    return (int16_t)c_x10;
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
static uint8_t try_read_temp_c_x10(int16_t *out_c_x10)
{
    unsigned char tdata[2];
#
    if (!ds_temp(tdata)) return 0;
#
    int16_t c_x16 = (int16_t)((uint16_t)tdata[1] << 8 | tdata[0]);
    *out_c_x10 = c_x16_to_c_x10(c_x16);
    return 1;
}
#
static uint16_t compute_tds_ppm(uint16_t adc_med, int16_t temp_c_x10, uint16_t *out_mv)
{
    uint16_t mv = (uint16_t)(((uint32_t)adc_med * (uint32_t)VREF_MV) / 1023U);
    if (out_mv) *out_mv = mv;
#
    float v = (float)mv / 1000.0f;
    float temp_c = (float)temp_c_x10 / 10.0f;
    float comp_coeff = 1.0f + 0.02f * (temp_c - 25.0f);
    float comp_v = v / comp_coeff;
#
    // From DFRobot sample code / datasheet tutorial.
    float tds = (133.42f * comp_v * comp_v * comp_v
               - 255.86f * comp_v * comp_v
               + 857.39f * comp_v) * 0.5f;
    if (tds < 0.0f) tds = 0.0f;
    return (uint16_t)(tds + 0.5f);
}
#
int main(void)
{
    serial_init();
    serial_out_str("TDS+TEMP TEST\r\n");
#
    uint8_t temp_sensor_ok = ds_init() ? 1 : 0;
    if (!temp_sensor_ok) {
        serial_out_str("WARN: DS18B20 init failed; using 25.0C compensation\r\n");
    }
#
    adc_init((uint8_t)TDS_ADC_CHANNEL);
#
    // Use last-known temperature for compensation if reads occasionally fail.
    int16_t last_temp_c_x10 = 250; // 25.0C
#
    while (1) {
        uint16_t samples[SCOUNT];
        uint8_t have_temp_this_frame = 0;
        int16_t frame_temp_c_x10 = last_temp_c_x10;
#
        if (temp_sensor_ok) {
            ds_convert(); // start conversion; don't wait
        }
#
        for (uint8_t i = 0; i < (uint8_t)SCOUNT; i++) {
            samples[i] = adc_read();
#
            if (temp_sensor_ok && !have_temp_this_frame) {
                if (try_read_temp_c_x10(&frame_temp_c_x10)) {
                    have_temp_this_frame = 1;
                }
            }
#
            for (uint8_t ms = 0; ms < (uint8_t)SAMPLE_DELAY_MS; ms++) {
                _delay_ms(1);
            }
        }
#
        // If temp wasn't ready during sampling window, give it a little more time.
        if (temp_sensor_ok && !have_temp_this_frame) {
            for (uint16_t i = 0; i < 1000; i++) {
                if (try_read_temp_c_x10(&frame_temp_c_x10)) {
                    have_temp_this_frame = 1;
                    break;
                }
                _delay_ms(1);
            }
        }
#
        if (temp_sensor_ok && have_temp_this_frame) {
            last_temp_c_x10 = frame_temp_c_x10;
        }
#
        uint16_t adc_med = median_u16(samples, (uint8_t)SCOUNT);
        uint16_t mv = 0;
        uint16_t tds_ppm = compute_tds_ppm(adc_med, frame_temp_c_x10, &mv);
#
        uint32_t sw_eq_ppm = 0;
        if (SEAWATER_SENSOR_TDS_PPM != 0UL) {
            sw_eq_ppm = ((uint32_t)tds_ppm * (uint32_t)SEAWATER_EQ_PPM) / (uint32_t)SEAWATER_SENSOR_TDS_PPM;
        }
#
        serial_out_str("T=");
        serial_out_c_x10(frame_temp_c_x10);
        serial_out_str("C");
        if (temp_sensor_ok && !have_temp_this_frame) {
            serial_out_str("(stale)");
        } else if (!temp_sensor_ok) {
            serial_out_str("(default)");
        }
#
        serial_out_str(" ADC_med=");
        serial_out_u16(adc_med);
        serial_out_str(" mV=");
        serial_out_u16(mv);
        serial_out_str(" TDS=");
        serial_out_u16(tds_ppm);
        serial_out_str("ppm SW_eq=");
        serial_out_u32(sw_eq_ppm);
        serial_out_str("ppm\r\n");
#
        _delay_ms(250);
    }
}

