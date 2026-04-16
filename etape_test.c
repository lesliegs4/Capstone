#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#include "serial.h"

#ifndef F_CPU
#error "F_CPU must be defined (e.g. via -DF_CPU=...)"
#endif

// Minimal "website style" test (Arduino sketch equivalent)
// Uses ADC1 (A1 / PC1) and prints:
//   Analog reading <0..1023>
//   Sensor resistance <ohms>
//   Sensor output <kOhms>   (ohms / 1000, matches datasheet y-axis)
//   Level <in> (<cm>)      (looked up from datasheet curve)
//
// Wiring it assumes:
//   sensor -> GND, and a known resistor -> VCC, with the ADC at the junction.

// the value of the 'other' resistor
#define SERIESRESISTOR 560U

// What pin to connect the sensor to: A1 = ADC1 = PC1
#define SENSOR_ADC_CHANNEL 1U

static void serial_out_str(const char *s)
{
    while (*s) serial_out(*s++);
}

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

static void serial_out_fixed_1(uint16_t v_x10)
{
    serial_out_u16((uint16_t)(v_x10 / 10U));
    serial_out('.');
    serial_out((char)('0' + (uint8_t)(v_x10 % 10U)));
}

static void serial_out_kohm_x1000(uint16_t r_ohms)
{
    // Print kΩ with three decimals, e.g. 1.234
    uint16_t whole = (uint16_t)(r_ohms / 1000U);
    uint16_t frac  = (uint16_t)(r_ohms % 1000U);
    serial_out_u16(whole);
    serial_out('.');
    serial_out((char)('0' + (uint8_t)((frac / 100U) % 10U)));
    serial_out((char)('0' + (uint8_t)((frac / 10U)  % 10U)));
    serial_out((char)('0' + (uint8_t)( frac        % 10U)));
}

static void adc_init(uint8_t channel)
{
    // AVcc reference, right adjust, select ADC channel (0..7)
    ADMUX = (1 << REFS0) | (channel & 0x07);

    // Enable ADC, prescaler 128 (ADC clk = F_CPU/128)
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

    // Disable digital input buffer on the ADC pin (reduces power/noise)
    if (channel <= 5) {
        DIDR0 |= (1 << channel);
    }

    // Let reference settle
    _delay_ms(2);
}

static uint16_t adc_read(void)
{
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {}
    return (uint16_t)ADC;
}

static uint16_t rsense_from_adc(uint16_t adc_code)
{
    // Equivalent of Arduino sketch:
    //   x = (1023 / adc) - 1
    //   Rsense = SERIESRESISTOR / x
    // => Rsense = SERIESRESISTOR * adc / (1023 - adc)
    if (adc_code == 0) return 65535U;
    if (adc_code >= 1023U) return 65535U;
    uint32_t num = (uint32_t)SERIESRESISTOR * (uint32_t)adc_code;
    uint32_t den = (uint32_t)(1023U - adc_code);
    uint32_t r = num / den;
    if (r > 65535U) r = 65535U;
    return (uint16_t)r;
}

typedef struct {
    uint16_t r_ohms;
    uint16_t in_x10; // inches * 10
} etape_point_t;

// Approximation from the datasheet "Typical eTape Sensor Output - PN 12110215TC-12" graph.
// Output resistance decreases as liquid level increases.
// NOTE: This is "typical" and eTape parts are ±20%; for best accuracy, calibrate.
static const etape_point_t ETAPE_TABLE[] = {
    {2250,   0},   // 0.0 in
    {2250,  10},   // 1.0 in  (actuation depth / dead zone)
    {2100,  20},   // 2.0 in
    {1950,  30},   // 3.0 in
    {1800,  40},   // 4.0 in
    {1650,  50},   // 5.0 in
    {1450,  60},   // 6.0 in
    {1300,  70},   // 7.0 in
    {1120,  80},   // 8.0 in
    { 950,  90},   // 9.0 in
    { 760, 100},   // 10.0 in
    { 620, 110},   // 11.0 in
    { 460, 120},   // 12.0 in
    { 400, 124},   // 12.4 in
};

static uint16_t in_x10_from_r_table(uint16_t r_ohms)
{
    const uint16_t n = (uint16_t)(sizeof(ETAPE_TABLE) / sizeof(ETAPE_TABLE[0]));

    // Above the top point => 0 in
    if (r_ohms >= ETAPE_TABLE[0].r_ohms) return ETAPE_TABLE[0].in_x10;
    // Below the last point => max in
    if (r_ohms <= ETAPE_TABLE[n - 1].r_ohms) return ETAPE_TABLE[n - 1].in_x10;

    for (uint16_t i = 0; i + 1 < n; i++) {
        uint16_t r0 = ETAPE_TABLE[i].r_ohms;
        uint16_t r1 = ETAPE_TABLE[i + 1].r_ohms;
        uint16_t x0 = ETAPE_TABLE[i].in_x10;
        uint16_t x1 = ETAPE_TABLE[i + 1].in_x10;

        // Table is non-increasing in r_ohms. Find segment where r0 >= r >= r1.
        if (r0 >= r_ohms && r_ohms >= r1) {
            uint16_t dr = (uint16_t)(r0 - r1);
            if (dr == 0) return x1; // flat segment
            uint16_t dx = (uint16_t)(r0 - r_ohms);
            uint16_t dd = (uint16_t)((x1 >= x0) ? (x1 - x0) : 0);

            // Linear interpolation: x = x0 + dd * dx / dr
            uint32_t num = (uint32_t)dd * (uint32_t)dx;
            uint16_t add = (uint16_t)(num / dr);
            return (uint16_t)(x0 + add);
        }
    }

    // Fallback (shouldn't happen)
    return ETAPE_TABLE[n - 1].in_x10;
}

static uint16_t cm_x10_from_in_x10(uint16_t in_x10)
{
    // cm_x10 = round(inches * 2.54 * 10) = round(in_x10 * 2.54)
    // = round(in_x10 * 254 / 100)
    uint32_t num = (uint32_t)in_x10 * 254U + 50U; // +0.5 for rounding
    return (uint16_t)(num / 100U);
}

int main(void)
{
    serial_init();
    serial_out_str("ETAPE SIMPLE TEST (A1)\r\n");
    serial_out_str("SERIESRESISTOR=");
    serial_out_u16(SERIESRESISTOR);
    serial_out_str("\r\n");

    adc_init(SENSOR_ADC_CHANNEL);

    while (1) {
        uint16_t adc_code = adc_read();
        uint16_t r_ohms = rsense_from_adc(adc_code);
        uint16_t in_x10 = in_x10_from_r_table(r_ohms);
        uint16_t cm_x10 = cm_x10_from_in_x10(in_x10);

        serial_out_str("Analog reading ");
        serial_out_u16(adc_code);
        serial_out_str("\r\n");

        serial_out_str("Sensor resistance ");
        serial_out_u16(r_ohms);
        serial_out_str("\r\n");

        serial_out_str("Sensor output ");
        serial_out_kohm_x1000(r_ohms);
        serial_out_str(" kOhms\r\n");

        serial_out_str("Level ");
        serial_out_fixed_1(in_x10);
        serial_out_str(" in (");
        serial_out_fixed_1(cm_x10);
        serial_out_str(" cm)\r\n");

        _delay_ms(1000);
    }
}

