#include <avr/io.h>
#include <stdint.h>

#include "serial.h"

#ifndef F_CPU
#error "F_CPU must be defined (e.g. via -DF_CPU=...)"
#endif

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 9600UL
#endif

// Normal speed UART: UBRR = (F_CPU / (16*BAUD)) - 1
#define UBRR_VALUE ((F_CPU / (16UL * SERIAL_BAUD)) - 1)

void serial_init(void)
{
    // Set baud
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE & 0xFF);

    // 8N1, enable RX/TX
    UCSR0A = 0;
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void serial_out(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) {}
    UDR0 = (uint8_t)c;
}

char serial_in(void)
{
    while (!(UCSR0A & (1 << RXC0))) {}
    return (char)UDR0;
}

