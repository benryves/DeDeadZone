###############################################################################
# Makefile for the project DeDeadZone
###############################################################################

## General Flags
PROJECT = DeDeadZone
MCU = atmega8
F_CPU = 8000000
TARGET = DeDeadZone.elf
CC = avr-gcc
AVRDUDE = avrdude -p $(MCU)

## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99 -DF_CPU=$(F_CPU)UL -Os -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums
CFLAGS += -MD -MP -MT $(*F).o -MF dep/$(@F).d 

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS +=  -Wl,-Map=DeDeadZone.map

## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings

COMPILE = $(CC) $(INCLUDES) $(CFLAGS)

OBJECTS = main.o

## Build
all: $(TARGET) DeDeadZone.elf DeDeadZone.hex size

## Compile
.c.o:
	$(COMPILE) -c $< -o $@

.S.o:
	$(COMPILE) -x assembler-with-cpp -c $< -o $@

##Link
$(TARGET): $(OBJECTS)
	 $(CC) $(LDFLAGS) $(OBJECTS) $(LINKONLYOBJECTS) $(LIBDIRS) $(LIBS) -o $(TARGET)

%.hex: $(TARGET)
	avr-objcopy -O ihex $(HEX_FLASH_FLAGS)  $< $@

%.eep: $(TARGET)
	-avr-objcopy $(HEX_EEPROM_FLAGS) -O ihex $< $@ || exit 0

%.lss: $(TARGET)
	avr-objdump -h -S $< > $@

size: ${TARGET}
	@echo
	@avr-size -C --mcu=${MCU} ${TARGET}

## Clean target
.PHONY: clean
clean:
	-rm -rf $(OBJECTS) DeDeadZone.elf dep DeDeadZone.hex DeDeadZone.eep DeDeadZone.lss DeDeadZone.map

flash: all
	$(AVRDUDE) -U flash:w:DeDeadZone.hex:i


fuses:
ifeq ($(MCU),atmega168)
	$(AVRDUDE) -U lfuse:w:0xc2:m -U hfuse:w:0xd4:m -U efuse:w:0xf9:m 
endif	
ifeq ($(MCU),atmega8)
	$(AVRDUDE) -U lfuse:w:0x04:m -U hfuse:w:0xd1:m
endif
	
## Other dependencies
-include $(shell mkdir dep 2>/dev/null) $(wildcard dep/*)

