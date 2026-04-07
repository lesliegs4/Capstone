// Minimal serial API (EE459 Lab 4 style)
// TXD = PD1 (ATmega328P DIP pin 3), RXD = PD0 (DIP pin 2)
#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);
void serial_out(char c);
char serial_in(void);

#endif

