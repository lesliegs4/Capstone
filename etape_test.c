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

// If 1, print a scan of ADC0..ADC5 each line (helps find the correct pin)
#ifndef ETAPE_SCAN_CHANNELS
#define ETAPE_SCAN_CHANNELS 0
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

// Print interval (ms)
#ifndef ETAPE_PRINT_MS
#define ETAPE_PRINT_MS 1000U
#endif

// Simple EMA smoothing on ADC (higher = smoother, slower response)
#ifndef ETAPE_EMA_SHIFT
#define ETAPE_EMA_SHIFT 3U   // 1/8 new, 7/8 old
#endif

// --- Calibration ---
// Your "dry/out of water" screenshot shows adc_filt stabilizing around ~1022.
// Your "12 inches submerged" screenshot shows adc_filt stabilizing around ~666.
//
// Calibrating in ADC-code space is much more stable than calibrating in ohms
// when the dry reading is near 1023 (which makes computed resistance blow up).
#ifndef ETAPE_CAL_EMPTY_ADC
#define ETAPE_CAL_EMPTY_ADC 1022U
#endif

#ifndef ETAPE_CAL_FULL_ADC
#define ETAPE_CAL_FULL_ADC 666U
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

static inline void adc_select_channel(uint8_t channel)
{
    // Keep REFS/ADLAR, change only MUX bits
    ADMUX = (ADMUX & 0xF0) | (channel & 0x0F);

    // Disable digital input buffer on the ADC pin (reduces power/noise)
    if (channel <= 5) {
        DIDR0 |= (1 << channel);
    }

    // Throw away first reading after mux change
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {}
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
        _delay_ms(1);
    }
    return (uint16_t)(acc / samples);
}

static uint16_t adc_read_avg_channel(uint8_t channel, uint8_t samples)
{
    adc_select_channel(channel);
    return adc_read_avg(samples);
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

// Returns percent full in tenths of a percent (0..1000) from filtered ADC code.
// Handles either orientation (empty > full or empty < full).
static uint16_t percent_full_x10_from_adc(uint16_t adc_code)
{
    int32_t empty = (int32_t)ETAPE_CAL_EMPTY_ADC;
    int32_t full  = (int32_t)ETAPE_CAL_FULL_ADC;
    int32_t a     = (int32_t)adc_code;

    if (empty == full) return 0;

    // If empty > full (your case): % = (empty - a)/(empty - full)
    // If empty < full:            % = (a - empty)/(full - empty)
    int32_t num, den;
    if (empty > full) {
        if (a >= empty) return 0;
        if (a <= full)  return 1000;
        num = (empty - a) * 1000;
        den = (empty - full);
    } else {
        if (a <= empty) return 0;
        if (a >= full)  return 1000;
        num = (a - empty) * 1000;
        den = (full - empty);
    }

    int32_t p_x10 = num / den;
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
    serial_out_str("ohm SAMPLES=");
    serial_out_u16(ETAPE_SAMPLES);
    serial_out_str(" PRINT_MS=");
    serial_out_u16(ETAPE_PRINT_MS);
    serial_out_str(" SCAN=");
    serial_out_u16(ETAPE_SCAN_CHANNELS);
    serial_out_str(" CAL_EMPTY=");
    serial_out_u16(ETAPE_CAL_EMPTY_ADC);
    serial_out_str(" CAL_FULL=");
    serial_out_u16(ETAPE_CAL_FULL_ADC);
    serial_out_str("\r\n");

    adc_init(ETAPE_ADC_CHANNEL);

    // Filter state in Q8 fixed-point (adc * 256)
    uint32_t adc_filt_q8 = 0;
    uint8_t filt_inited = 0;

    while (1) {
        uint16_t adc_raw = adc_read_avg_channel((uint8_t)ETAPE_ADC_CHANNEL, (uint8_t)ETAPE_SAMPLES);

        if (!filt_inited) {
            adc_filt_q8 = ((uint32_t)adc_raw) << 8;
            filt_inited = 1;
        } else {
            int32_t target_q8 = (int32_t)(((uint32_t)adc_raw) << 8);
            int32_t err = target_q8 - (int32_t)adc_filt_q8;
            adc_filt_q8 = (uint32_t)((int32_t)adc_filt_q8 + (err >> ETAPE_EMA_SHIFT));
        }

        uint16_t adc_code = (uint16_t)(adc_filt_q8 >> 8);

        uint16_t r_ohms = rsense_from_adc(adc_code);
        uint16_t pct_x10 = percent_full_x10_from_adc(adc_code);
        uint16_t sub_tenths_in = level_tenths_in_from_percent_x10(pct_x10);
        uint16_t from_top_tenths_in = 0;
        if (sub_tenths_in <= ETAPE_ACTIVE_TENTHS_IN) {
            from_top_tenths_in = (uint16_t)(ETAPE_ACTIVE_TENTHS_IN - sub_tenths_in);
        }

#if ETAPE_SCAN_CHANNELS
        serial_out_str("scan ");
        for (uint8_t ch = 0; ch <= 5; ch++) {
            uint16_t v = adc_read_avg_channel(ch, 8);
            serial_out('A');
            serial_out((char)('0' + ch));
            serial_out('=');
            serial_out_u16(v);
            if (ch != 5) serial_out(' ');
        }
        serial_out_str(" | ");
#endif

        serial_out_str("adc_raw=");
        serial_out_u16(adc_raw);
        serial_out_str(" adc_filt=");
        serial_out_u16(adc_code);
        serial_out_str(" r=");
        serial_out_u16(r_ohms);
        serial_out_str("ohm pct=");
        serial_out_fixed_1(pct_x10); // tenths of a percent
        serial_out_str("% sub=");
        serial_out_fixed_1(sub_tenths_in); // tenths of an inch submerged (from bottom)
        serial_out_str("in top=");
        serial_out_fixed_1(from_top_tenths_in); // tenths of an inch from top to surface
        serial_out_str("in\r\n");

        for (uint16_t i = 0; i < ETAPE_PRINT_MS; i++) {
            _delay_ms(1);
        }
    }
}

