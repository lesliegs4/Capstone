DEVICE     = atmega328p
CLOCK      = 7372800
PROGRAMMER = -c usbtiny -P usb
MAIN_OBJS  = at328.o
TEMP_TEST_OBJS = temp_test.o ds18b20.o serial.o
ETAPE_TEST_OBJS = etape_test.o serial.o
SERIAL_A_TEST_OBJS = serial_a_test.o serial.o
TDS_TEST_OBJS = tds_test.o serial.o
TDS_TEMP_TEST_OBJS = tds_temp_test.o ds18b20.o serial.o
RELAY_TEST_OBJS = relay_test.o serial.o
FUSES      = -U hfuse:w:0xd9:m -U lfuse:w:0xe0:m
#
# Optional: slow down ISP clock if initialization fails.
# Usage examples:
#   make flash BITCLOCK=10    (≈100 kHz SCK period 10 us)
#   make flash BITCLOCK=20    (≈50 kHz)
BITCLOCK   ?=

# Fuse Low Byte = 0xe0   Fuse High Byte = 0xd9   Fuse Extended Byte = 0xff
# Bit 7: CKDIV8  = 1     Bit 7: RSTDISBL  = 1    Bit 7:
#     6: CKOUT   = 1         6: DWEN      = 1        6:
#     5: SUT1    = 1         5: SPIEN     = 0        5:
#     4: SUT0    = 0         4: WDTON     = 1        4:
#     3: CKSEL3  = 0         3: EESAVE    = 1        3:
#     2: CKSEL2  = 0         2: BOOTSIZ1  = 0        2: BODLEVEL2 = 1
#     1: CKSEL1  = 0         1: BOOTSIZ0  = 0        1: BODLEVEL1 = 1
#     0: CKSEL0  = 0         0: BOOTRST   = 1        0: BODLEVEL0 = 1
# External clock source, start-up time = 14 clks + 65ms
# Don't output clock on PORTB0, don't divide clock by 8,
# Boot reset vector disabled, boot flash size 2048 bytes,
# Preserve EEPROM disabled, watch-dog timer off
# Serial program downloading enabled, debug wire disabled,
# Reset enabled, brown-out detection disabled

# Tune the lines below only if you know what you are doing:

AVRDUDE = avrdude $(PROGRAMMER) -p $(DEVICE) $(if $(BITCLOCK),-B $(BITCLOCK),)
# Extra C flags can be passed on command line, e.g.
#   make temp_test.hex CFLAGS='-DTEST_RAW_C_X16=0x0191'
CFLAGS ?=
# Also define SERIAL_BAUD for firmware builds (serial.c defaults to 9600 if unset).
# Keeping this in sync with `make monitor SERIAL_BAUD=...` avoids "silent" output.
COMPILE = avr-gcc -Wall -Os -DF_CPU=$(CLOCK) -DSERIAL_BAUD=$(SERIAL_BAUD) -mmcu=$(DEVICE) $(CFLAGS)

# symbolic targets:
all:	main.hex

.PHONY: all flash fuse install load clean disasm cpp temp_test flash_temp_test etape_test flash_etape_test serial_a_test flash_serial_a_test tds_test flash_tds_test tds_temp_test flash_tds_temp_test relay_test flash_relay_test test test_ds18b20 main build_main build_temp_test build_etape_test build_serial_a_test build_tds_test build_tds_temp_test build_relay_test serial ds18b20

# ---- Serial monitor (macOS) ----
# Usage:
#   make monitor SERIAL_PORT=/dev/cu.usbserial-XXXX SERIAL_BAUD=9600
SERIAL_PORT ?= $(firstword $(wildcard /dev/cu.usbserial* /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usb*))
SERIAL_BAUD ?= 9600

monitor:
	@if [ -z "$(SERIAL_PORT)" ]; then \
		echo "No serial port auto-detected."; \
		echo "Run: make monitor SERIAL_PORT=/dev/cu.usbserial-XXXX SERIAL_BAUD=$(SERIAL_BAUD)"; \
		exit 1; \
	fi
	screen $(SERIAL_PORT) $(SERIAL_BAUD)

#
# Minimal compile/syntax check (does not build main/temp_test)
#
test: test_ds18b20

test_ds18b20:
	$(COMPILE) -fsyntax-only ds18b20.c

.c.o:
	$(COMPILE) -c $< -o $@

.S.o:
	$(COMPILE) -x assembler-with-cpp -c $< -o $@
# "-x assembler-with-cpp" should not be necessary since this is the default
# file type for the .S (with capital S) extension. However, upper case
# characters are not always preserved on Windows. To ensure WinAVR
# compatibility define the file type manually.

.c.s:
	$(COMPILE) -S $< -o $@

flash:	all
	$(AVRDUDE) -U flash:w:main.hex:i

temp_test: temp_test.hex

main: main.hex
build_main: main.hex
build_temp_test: temp_test.hex
etape_test: etape_test.hex
build_etape_test: etape_test.hex
serial_a_test: serial_a_test.hex
build_serial_a_test: serial_a_test.hex
tds_test: tds_test.hex
build_tds_test: tds_test.hex
tds_temp_test: tds_temp_test.hex
build_tds_temp_test: tds_temp_test.hex
relay_test: relay_test.hex
build_relay_test: relay_test.hex
serial: serial.o
ds18b20: ds18b20.o

flash_temp_test: temp_test.hex
	$(AVRDUDE) -U flash:w:temp_test.hex:i

flash_etape_test: etape_test.hex
	$(AVRDUDE) -U flash:w:etape_test.hex:i

flash_serial_a_test: serial_a_test.hex
	$(AVRDUDE) -U flash:w:serial_a_test.hex:i

flash_tds_test: tds_test.hex
	$(AVRDUDE) -U flash:w:tds_test.hex:i

flash_tds_temp_test: tds_temp_test.hex
	$(AVRDUDE) -U flash:w:tds_temp_test.hex:i

flash_relay_test: relay_test.hex
	$(AVRDUDE) -U flash:w:relay_test.hex:i

fuse:
	$(AVRDUDE) $(FUSES)

# Xcode uses the Makefile targets "", "clean" and "install"
install: flash fuse

# if you use a bootloader, change the command below appropriately:
load: all
	bootloadHID main.hex

clean:
	rm -f *.o *.elf *.hex

# file targets:
main.elf: $(MAIN_OBJS)
	$(COMPILE) -o main.elf $(MAIN_OBJS)

main.hex: main.elf
	rm -f main.hex
	avr-objcopy -j .text -j .data -O ihex main.elf main.hex
	avr-size --format=avr --mcu=$(DEVICE) main.elf

temp_test.elf: temp_test.o ds18b20.o serial.o
	$(COMPILE) -o temp_test.elf temp_test.o ds18b20.o serial.o

temp_test.hex: temp_test.elf
	rm -f temp_test.hex
	avr-objcopy -j .text -j .data -O ihex temp_test.elf temp_test.hex
	avr-size --format=avr --mcu=$(DEVICE) temp_test.elf

etape_test.elf: etape_test.o serial.o
	$(COMPILE) -o etape_test.elf etape_test.o serial.o

etape_test.hex: etape_test.elf
	rm -f etape_test.hex
	avr-objcopy -j .text -j .data -O ihex etape_test.elf etape_test.hex
	avr-size --format=avr --mcu=$(DEVICE) etape_test.elf

serial_a_test.elf: serial_a_test.o serial.o
	$(COMPILE) -o serial_a_test.elf serial_a_test.o serial.o

serial_a_test.hex: serial_a_test.elf
	rm -f serial_a_test.hex
	avr-objcopy -j .text -j .data -O ihex serial_a_test.elf serial_a_test.hex
	avr-size --format=avr --mcu=$(DEVICE) serial_a_test.elf

tds_test.elf: $(TDS_TEST_OBJS)
	$(COMPILE) -o tds_test.elf $(TDS_TEST_OBJS)

tds_test.hex: tds_test.elf
	rm -f tds_test.hex
	avr-objcopy -j .text -j .data -O ihex tds_test.elf tds_test.hex
	avr-size --format=avr --mcu=$(DEVICE) tds_test.elf

tds_temp_test.elf: $(TDS_TEMP_TEST_OBJS)
	$(COMPILE) -o tds_temp_test.elf $(TDS_TEMP_TEST_OBJS)

tds_temp_test.hex: tds_temp_test.elf
	rm -f tds_temp_test.hex
	avr-objcopy -j .text -j .data -O ihex tds_temp_test.elf tds_temp_test.hex
	avr-size --format=avr --mcu=$(DEVICE) tds_temp_test.elf

relay_test.elf: $(RELAY_TEST_OBJS)
	$(COMPILE) -o relay_test.elf $(RELAY_TEST_OBJS)

relay_test.hex: relay_test.elf
	rm -f relay_test.hex
	avr-objcopy -j .text -j .data -O ihex relay_test.elf relay_test.hex
	avr-size --format=avr --mcu=$(DEVICE) relay_test.elf
# If you have an EEPROM section, you must also create a hex file for the
# EEPROM and add it to the "flash" target.

# Targets for code debugging and analysis:
disasm:	main.elf
	avr-objdump -d main.elf

cpp:
	$(COMPILE) -E at328.c