#include <avr/io.h>
#include <util/delay.h>
#
#include "serial.h"
#
#ifndef F_CPU
#error "F_CPU must be defined (e.g. via -DF_CPU=...)"
#endif
#
int main(void)
{
    serial_init();
    serial_out('B');
    serial_out('O');
    serial_out('O');
    serial_out('T');
    serial_out('\r');
    serial_out('\n');
#
    while (1) {
        serial_out('A');
        serial_out('\r');
        serial_out('\n');
        _delay_ms(250);
    }
}

