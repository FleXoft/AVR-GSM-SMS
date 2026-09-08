#ifndef FAKE_EEPROM_H
#define FAKE_EEPROM_H
#include <stdint.h>
#include <stddef.h>
void eeprom_read_block(void *out, const void *address, size_t size);
uint16_t eeprom_read_word(const uint16_t *address);
uint8_t eeprom_is_ready(void);
void eeprom_update_byte(uint8_t *address, uint8_t value);
#endif
