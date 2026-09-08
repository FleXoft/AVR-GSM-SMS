# Olimex AVR-GSM: build only; programming is an explicit target.
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

MCU ?= atmega32
F_CPU ?= 7372800
UART_BAUD_RATE ?= 57600
VERSION ?= 1.2.1
BUILD_DIR ?= build
TARGET ?= AVR-GSM-SMS
CC = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE = avr-size
NM = avr-nm
PYTHON ?= python3
HOST_CC ?= cc
AVRDUDE ?= avrdude
AVRDUDE_PROGRAMMER ?= jtag1
AVRDUDE_PORT ?= /dev/cu.usbserial-A3000SLY
AVRDUDE_BAUD ?= 57600

SRC := AVR-GSM-SMS.c gsm.c gsm_services.c sms_pdu.c application.c sensors.c persistence.c runtime.c ui.c lcd.c uart.c
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)
OUTPUT := $(BUILD_DIR)/$(TARGET)
CPPFLAGS += -I. -I$(BUILD_DIR) -DF_CPU=$(F_CPU)UL -DUART_BAUD_RATE=$(UART_BAUD_RATE)UL
CPPFLAGS += -DUART_RX0_BUFFER_SIZE=256 -DUART_TX0_BUFFER_SIZE=64
CFLAGS ?= -Os -g
CFLAGS += -std=gnu99 -Wall -Wextra -Wstrict-prototypes -Werror -ffunction-sections -fdata-sections -fstack-usage
LDFLAGS += -Wl,--gc-sections,-Map=$(OUTPUT).map

.PHONY: all build elf hex eep lss sym size clean program check test FORCE
all: build size
build: hex eep lss sym
elf: $(OUTPUT).elf
hex: $(OUTPUT).hex
eep: $(OUTPUT).eep
lss: $(OUTPUT).lss
sym: $(OUTPUT).sym

$(BUILD_DIR)/.dir:
	mkdir -p "$(BUILD_DIR)"
	touch "$@"

# Recompile on changed flags/version, but never rewrite sources or increment a
# version number just because make (including make -j) was invoked.
$(BUILD_DIR)/config: FORCE | $(BUILD_DIR)/.dir
	@printf '%s\n' '$(MCU) $(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(VERSION)' > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@
	@rm -f $@.tmp

$(BUILD_DIR)/version.h: $(BUILD_DIR)/config
	@printf '%s\n' '#define FIRMWARE_VERSION "$(VERSION)"' > $@

$(BUILD_DIR)/%.o: %.c $(BUILD_DIR)/config $(BUILD_DIR)/version.h
	$(CC) -mmcu=$(MCU) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OUTPUT).elf: $(OBJ)
	$(CC) -mmcu=$(MCU) $(LDFLAGS) $(OBJ) $(LDLIBS) -o $@

$(OUTPUT).hex: $(OUTPUT).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# Runtime EEPROM journal is not embedded in the firmware image.
# program writes Flash only; chip erase EEPROM behavior depends on EESAVE.
$(OUTPUT).eep: $(OUTPUT).elf
	$(OBJCOPY) -j .eeprom --set-section-flags=.eeprom=alloc,load --change-section-lma .eeprom=0 --no-change-warnings -O ihex $< $@

$(OUTPUT).lss: $(OUTPUT).elf
	$(OBJDUMP) -h -S $< > $@
$(OUTPUT).sym: $(OUTPUT).elf
	$(NM) -n $< > $@

size: $(OUTPUT).elf
	$(SIZE) $<
	$(PYTHON) tools/check_size.py $(SIZE) $<

program: $(OUTPUT).hex size
	$(AVRDUDE) -p $(MCU) -c $(AVRDUDE_PROGRAMMER) -P $(AVRDUDE_PORT) -b $(AVRDUDE_BAUD) -U flash:w:$<:i

$(BUILD_DIR)/test_gsm: tests/test_gsm.c gsm.c gsm_services.c sms_pdu.c gsm.h gsm_services.h sms_pdu.h flash.h timing.h | $(BUILD_DIR)/.dir
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I. tests/test_gsm.c gsm.c gsm_services.c sms_pdu.c -o $@
$(BUILD_DIR)/test_ui: tests/test_ui.c ui.c ui.h lcd_effects.h timing.h flash.h | $(BUILD_DIR)/.dir
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I. tests/test_ui.c ui.c -o $@
test: $(BUILD_DIR)/test_gsm $(BUILD_DIR)/test_ui
	./$(BUILD_DIR)/test_gsm
	./$(BUILD_DIR)/test_ui
check: all test

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) $(OBJ:.o=.su) $(OUTPUT).elf $(OUTPUT).hex $(OUTPUT).eep $(OUTPUT).map $(OUTPUT).lss $(OUTPUT).sym
	rm -f $(BUILD_DIR)/config $(BUILD_DIR)/config.tmp $(BUILD_DIR)/version.h $(BUILD_DIR)/test_gsm $(BUILD_DIR)/test_ui $(BUILD_DIR)/test_services $(BUILD_DIR)/test_application $(BUILD_DIR)/test_sensors $(BUILD_DIR)/test_persistence $(BUILD_DIR)/test_integration

-include $(OBJ:.o=.d)

$(BUILD_DIR)/test_services: tests/test_services.c gsm_services.c sms_pdu.c gsm_services.h sms_pdu.h | $(BUILD_DIR)/.dir
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I. tests/test_services.c gsm_services.c sms_pdu.c -o $@
$(BUILD_DIR)/test_application: tests/test_application.c application.c sms_pdu.c application.h gsm_services.h persistence.h sensors.h | $(BUILD_DIR)/.dir
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I. tests/test_application.c application.c sms_pdu.c -o $@
.PHONY: test_features
test: test_features
test_features: $(BUILD_DIR)/test_services $(BUILD_DIR)/test_application
	./$(BUILD_DIR)/test_services
	./$(BUILD_DIR)/test_application

$(BUILD_DIR)/test_sensors: tests/test_sensors.c sensors.c sensors.h tests/fake/avr/io.h tests/fake/util/twi.h | $(BUILD_DIR)/.dir
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/fake -I. -DF_CPU=7372800UL tests/test_sensors.c sensors.c -o $@
$(BUILD_DIR)/test_persistence: tests/test_persistence.c persistence.c sms_pdu.c persistence.h tests/fake/avr/eeprom.h | $(BUILD_DIR)/.dir
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/fake -I. tests/test_persistence.c persistence.c sms_pdu.c -o $@
.PHONY: test_hardware_models
test: test_hardware_models
test_hardware_models: $(BUILD_DIR)/test_sensors $(BUILD_DIR)/test_persistence
	./$(BUILD_DIR)/test_sensors
	./$(BUILD_DIR)/test_persistence

$(BUILD_DIR)/test_integration: tests/test_integration.c gsm.c gsm_services.c sms_pdu.c application.c gsm.h gsm_services.h application.h | $(BUILD_DIR)/.dir
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I. tests/test_integration.c gsm.c gsm_services.c sms_pdu.c application.c -o $@
.PHONY: test_integration
test: test_integration
test_integration: $(BUILD_DIR)/test_integration
	./$(BUILD_DIR)/test_integration

# Host test executables must also rebuild after project/test register headers change.
HOST_TESTS := test_gsm test_ui test_services test_application test_sensors test_persistence test_integration
$(addprefix $(BUILD_DIR)/,$(HOST_TESTS)): $(wildcard *.h) $(wildcard tests/fake/avr/*.h tests/fake/util/*.h) makefile
