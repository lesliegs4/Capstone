#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#include "serial.h"

#ifndef F_CPU
#error "F_CPU must be defined (e.g. via -DF_CPU=...)"
#endif

// IoT Relay II control input wiring (low voltage side):
// - Connect ATmega GND to one IoT Relay control terminal.
// - Connect ATmega GPIO (defined below) to the other control terminal.
//
// Default GPIO: PD7 (ATmega328P DIP pin 13)
#ifndef RELAY_DDR
#define RELAY_DDR  DDRD
#endif
#ifndef RELAY_PORT
#define RELAY_PORT PORTD
#endif
#ifndef RELAY_BIT
#define RELAY_BIT  PD7
#endif

// If your relay turns ON when the pin is LOW, set to 1 via CFLAGS:
//   make relay_test.hex CFLAGS='-DRELAY_ACTIVE_LOW=1'
#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 0
#endif

#ifndef RELAY_ON_MS
#define RELAY_ON_MS  1000U
#endif
#ifndef RELAY_OFF_MS
#define RELAY_OFF_MS 1000U
#endif

static void serial_out_str(const char *s)
{
    while (*s) serial_out(*s++);
}

static inline void relay_init(void)
{
    RELAY_DDR |= (1 << RELAY_BIT);
}

static inline void relay_set(uint8_t on)
{
#if RELAY_ACTIVE_LOW
    if (on) RELAY_PORT &= ~(1 << RELAY_BIT);
    else    RELAY_PORT |=  (1 << RELAY_BIT);
#else
    if (on) RELAY_PORT |=  (1 << RELAY_BIT);
    else    RELAY_PORT &= ~(1 << RELAY_BIT);
#endif
}

static void delay_ms(uint16_t ms)
{
    while (ms--) _delay_ms(1);
}

int main(void)
{
    serial_init();
    serial_out_str("RELAY TEST (IoT Relay II)\r\n");

    relay_init();
    relay_set(0);

    while (1) {
        relay_set(1);
        serial_out_str("ON\r\n");
        delay_ms((uint16_t)RELAY_ON_MS);

        relay_set(0);
        serial_out_str("OFF\r\n");
        delay_ms((uint16_t)RELAY_OFF_MS);
    }
}

