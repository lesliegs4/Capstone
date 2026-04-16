#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#include "ds18b20.h"
#include "serial.h"

// LED output (attach LED+resistor to PC0 / DIP pin 23, or change these defines)
#define LED_DDR  DDRC
#define LED_PORT PORTC
#define LED_BIT  PC0

static inline void led_on(void)  { LED_PORT |=  (1 << LED_BIT); }
static inline void led_off(void) { LED_PORT &= ~(1 << LED_BIT); }

static void led_pulse_ms(uint16_t on_ms, uint16_t off_ms)
{
    led_on();
    while (on_ms--) _delay_ms(1);
    led_off();
    while (off_ms--) _delay_ms(1);
}

static void led_blink_digit(uint8_t d)
{
    // Digit encoding: 1-9 = that many short pulses. 0 = one long pulse.
    if (d == 0) {
        led_pulse_ms(700, 400);
        return;
    }
    for (uint8_t i = 0; i < d; i++) {
        led_pulse_ms(200, 200);
    }
    _delay_ms(400);
}

static void led_show_c_x10(int16_t c_x10)
{
    // Frame marker
    led_pulse_ms(80, 120);
    led_pulse_ms(80, 300);

    if (c_x10 < 0) {
        // Negative marker
        led_pulse_ms(400, 400);
        c_x10 = (int16_t)-c_x10;
    }

    uint16_t v = (uint16_t)c_x10;
    uint8_t tens  = (uint8_t)((v / 100) % 10); // 10s place of integer C
    uint8_t ones  = (uint8_t)((v / 10)  % 10); // 1s place
    uint8_t tenth = (uint8_t)(v % 10);         // .1 place

    led_blink_digit(tens);
    _delay_ms(700);
    led_blink_digit(ones);
    _delay_ms(700);
    led_blink_digit(tenth);
    _delay_ms(1200);
}

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

// Convert DS18B20 raw temp (Celsius * 16) to Celsius * 10 (one decimal place).
// raw format: signed 16-bit two's complement, LSB = 1/16 °C.
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

static void serial_out_c_x10(int16_t c_x10)
{
    int32_t v = (int32_t)c_x10;
    if (v < 0) { serial_out('-'); v = -v; }
    serial_out_u16((uint16_t)(v / 10));
    serial_out('.');
    serial_out((char)('0' + (uint8_t)(v % 10)));
}

static uint8_t read_raw_c_x16_from_sensor(int16_t *out_c_x16)
{
    unsigned char tdata[2];

    ds_convert();

    // Wait for conversion complete (max 750ms at 12-bit)
    for (uint16_t i = 0; i < 800; i++) {
        if (ds_temp(tdata)) {
            *out_c_x16 = (int16_t)((uint16_t)tdata[1] << 8 | tdata[0]);
            return 1;
        }
        _delay_ms(1);
    }

    return 0;
}

int main(void)
{
    LED_DDR |= (1 << LED_BIT);
    led_off();

    serial_init();
    serial_out_str("BOOT\r\n");

    if (!ds_init()) {
        serial_out_str("ERR\r\n");
    }

    while (1) {
        int16_t c_x16;
        uint8_t ok;

        // For conversion testing without hardware, define TEST_RAW_C_X16.
        // Example from datasheet: 0x0191 = 25.0625C
        // #define TEST_RAW_C_X16 0x0191
#ifdef TEST_RAW_C_X16
        c_x16 = (int16_t)TEST_RAW_C_X16;
        ok = 1;
#else
        ok = read_raw_c_x16_from_sensor(&c_x16);
#endif

        if (ok) {
            int16_t c_x10 = c_x16_to_c_x10(c_x16);
            serial_out_c_x10(c_x10);
            serial_out_str("\r\n");
            led_show_c_x10(c_x10);
        } else {
            serial_out_str("ERR\r\n");
            // error pattern: three long pulses
            led_pulse_ms(500, 200);
            led_pulse_ms(500, 200);
            led_pulse_ms(500, 800);
        }

        _delay_ms(1000);
    }
}
