#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#include "ds18b20.h"

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

static void led_show_f_x10(int16_t f_x10)
{
    // Frame marker
    led_pulse_ms(80, 120);
    led_pulse_ms(80, 300);

    if (f_x10 < 0) {
        // Negative marker
        led_pulse_ms(400, 400);
        f_x10 = (int16_t)-f_x10;
    }

    uint16_t v = (uint16_t)f_x10;
    uint8_t tens  = (uint8_t)((v / 100) % 10); // 10s place of integer F
    uint8_t ones  = (uint8_t)((v / 10)  % 10); // 1s place
    uint8_t tenth = (uint8_t)(v % 10);         // .1 place

    led_blink_digit(tens);
    _delay_ms(700);
    led_blink_digit(ones);
    _delay_ms(700);
    led_blink_digit(tenth);
    _delay_ms(1200);
}

// ---- UART (9600 8N1) on PD1 (TXD) ----
#define BAUD 9600UL
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

static void uart_init(void)
{
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE & 0xFF);
    UCSR0A = 0;
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8N1
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) {}
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_put_u16(uint16_t v)
{
    char buf[6];
    uint8_t i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i--) uart_putc(buf[i]);
}

// Convert DS18B20 raw temp (Celsius * 16) to Fahrenheit * 10 (one decimal place).
// raw format: signed 16-bit two's complement, LSB = 1/16 °C.
static int16_t c_x16_to_f_x10(int16_t c_x16)
{
    // F = C * 9/5 + 32
    // (F * 10) = (C * 10) * 9/5 + 320
    // C = c_x16 / 16  =>  (F * 10) = c_x16 * 90 / 80 + 320
    int32_t num = (int32_t)c_x16 * 90;
    if (num >= 0) num += 40;   // round to nearest tenth
    else          num -= 40;
    int32_t f_x10 = (num / 80) + 320;
    if (f_x10 > INT16_MAX) return INT16_MAX;
    if (f_x10 < INT16_MIN) return INT16_MIN;
    return (int16_t)f_x10;
}

static void uart_put_f_x10(int16_t f_x10)
{
    int32_t v = (int32_t)f_x10;
    if (v < 0) { uart_putc('-'); v = -v; }
    uart_put_u16((uint16_t)(v / 10));
    uart_putc('.');
    uart_putc((char)('0' + (uint8_t)(v % 10)));
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

    uart_init();

    if (!ds_init()) {
        uart_puts("ERR\r\n");
    }

    while (1) {
        int16_t c_x16;
        uint8_t ok;

        // For conversion testing without hardware, define TEST_RAW_C_X16.
        // Example from datasheet: 0x0191 = 25.0625C => 77.1F
        // #define TEST_RAW_C_X16 0x0191
#ifdef TEST_RAW_C_X16
        c_x16 = (int16_t)TEST_RAW_C_X16;
        ok = 1;
#else
        ok = read_raw_c_x16_from_sensor(&c_x16);
#endif

        if (ok) {
            int16_t f_x10 = c_x16_to_f_x10(c_x16);
            uart_put_f_x10(f_x10);
            uart_puts("\r\n");
            led_show_f_x10(f_x10);
        } else {
            uart_puts("ERR\r\n");
            // error pattern: three long pulses
            led_pulse_ms(500, 200);
            led_pulse_ms(500, 200);
            led_pulse_ms(500, 800);
        }

        _delay_ms(1000);
    }
}

