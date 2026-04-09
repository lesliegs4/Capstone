#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#
#include "serial.h"
#
#ifndef F_CPU
#error "F_CPU must be defined (e.g. via -DF_CPU=...)"
#endif

// -------- User-tunable settings --------
// ADC channel for the divider node (0..5 => PC0..PC5 on ATmega328P DIP)
#ifndef ETAPE_ADC_CHANNEL
#define ETAPE_ADC_CHANNEL 0
#endif

// Series resistor from ADC node -> VCC (ohms)
// This matches wiring: GND -> Rsense -> ADC node -> Rseries -> VCC
#ifndef ETAPE_RSERIES_OHMS
#define ETAPE_RSERIES_OHMS 560U
#endif

// Nominal resistance endpoints for the 12" eTape (ohms).
// For best accuracy, replace these with your measured empty/full values.
#ifndef ETAPE_R_EMPTY_OHMS
#define ETAPE_R_EMPTY_OHMS 2000U
#endif
#ifndef ETAPE_R_FULL_OHMS
#define ETAPE_R_FULL_OHMS  400U
#endif

// Active length in tenths of an inch. Datasheet: ~12.4" active for the 12" part.
#ifndef ETAPE_ACTIVE_TENTHS_IN
#define ETAPE_ACTIVE_TENTHS_IN 124U
#endif

// How many ADC samples to average each print
#ifndef ETAPE_SAMPLES
#define ETAPE_SAMPLES 16U
#endif

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

static uint16_t adc_read_avg(uint8_t samples)
{
    uint32_t acc = 0;
    for (uint8_t i = 0; i < samples; i++) {
        acc += adc_read();
        _delay_ms(2);
    }
    return (uint16_t)(acc / samples);
}

// Compute Rsense (ohms) from ADC code for divider:
//   GND -> Rsense -> ADC node -> Rseries -> VCC
// Using Vref = AVcc, the ratio is:
//   adc/1023 = Rsense / (Rsense + Rseries)
// => Rsense = Rseries * adc / (1023 - adc)
static uint16_t rsense_from_adc(uint16_t adc_code)
{
    if (adc_code >= 1023U) return 65535U;
    uint32_t num = (uint32_t)ETAPE_RSERIES_OHMS * (uint32_t)adc_code;
    uint32_t den = (uint32_t)(1023U - adc_code);
    uint32_t r = num / den;
    if (r > 65535U) r = 65535U;
    return (uint16_t)r;
}

// Returns percent full in tenths of a percent (0..1000)
static uint16_t percent_full_x10_from_r(uint16_t r_ohms)
{
    int32_t r_empty = (int32_t)ETAPE_R_EMPTY_OHMS;
    int32_t r_full  = (int32_t)ETAPE_R_FULL_OHMS;
    int32_t r       = (int32_t)r_ohms;

    int32_t denom = (r_empty - r_full);
    if (denom <= 0) return 0;

    // %full = (Rempty - R) / (Rempty - Rfull)
    int32_t num = (r_empty - r) * 1000; // x10 percent
    int32_t p_x10 = num / denom;

    if (p_x10 < 0) p_x10 = 0;
    if (p_x10 > 1000) p_x10 = 1000;
    return (uint16_t)p_x10;
}

static uint16_t level_tenths_in_from_percent_x10(uint16_t percent_x10)
{
    // level = active_len * (percent/100)
    // Here percent_x10 is 0..1000 (tenths of a percent)
    // level_tenths_in = active_tenths_in * percent / 100
    // => active_tenths_in * (percent_x10/10) / 100 = active * percent_x10 / 1000
    uint32_t v = (uint32_t)ETAPE_ACTIVE_TENTHS_IN * (uint32_t)percent_x10;
    return (uint16_t)(v / 1000U);
}

int main(void)
{
    serial_init();
    serial_out_str("ETAPE TEST\r\n");
    serial_out_str("ADC_CH=");
    serial_out_u16(ETAPE_ADC_CHANNEL);
    serial_out_str(" RSER=");
    serial_out_u16(ETAPE_RSERIES_OHMS);
    serial_out_str("ohm\r\n");

    adc_init(ETAPE_ADC_CHANNEL);

    while (1) {
        uint16_t adc_code = adc_read_avg((uint8_t)ETAPE_SAMPLES);
        uint16_t r_ohms = rsense_from_adc(adc_code);
        uint16_t pct_x10 = percent_full_x10_from_r(r_ohms);
        uint16_t lvl_tenths_in = level_tenths_in_from_percent_x10(pct_x10);

        serial_out_str("adc=");
        serial_out_u16(adc_code);
        serial_out_str(" r=");
        serial_out_u16(r_ohms);
        serial_out_str("ohm pct=");
        serial_out_fixed_1(pct_x10); // tenths of a percent
        serial_out_str("% lvl=");
        serial_out_fixed_1(lvl_tenths_in); // tenths of an inch
        serial_out_str("in\r\n");

        _delay_ms(250);
    }
}

