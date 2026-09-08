#include "persistence.h"
#include <avr/eeprom.h>
#include <string.h>
#include "flash.h"

persistent_state_t settings;
/* Two journal slots, outside the legacy counters at EEPROM addresses 0..7. */
#define RECORD_SIZE 31
static uint8_t record[RECORD_SIZE], active_slot, dirty, writing, position;
static uint16_t sequence;
static uint16_t crc(const uint8_t *p)
{
    uint16_t c = 0xffff;
    for (uint8_t i = 1; i < 29; ++i) {
        c ^= (uint16_t)p[i] << 8;
        for (uint8_t j = 0; j < 8; ++j) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1;
    }
    return c;
}
static uint16_t word(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint8_t valid(const uint8_t *p)
{
    return p[0] == 0xa5 && p[1] == 1 && word(p + 29) == crc(p) &&
           memchr(p + 12, 0, SMS_PHONE_SIZE) && sms_phone_valid((const char *)p + 12);
}
static uint16_t base(uint8_t slot) { return slot ? 128 : 64; }
void persistence_init(void)
{
    uint8_t other[RECORD_SIZE];
    eeprom_read_block(record, (const void *)64, RECORD_SIZE);
    eeprom_read_block(other, (const void *)128, RECORD_SIZE);
    active_slot = 0;
    if (valid(other) && (!valid(record) || (int16_t)(word(other + 2) - word(record + 2)) > 0)) {
        memcpy(record, other, RECORD_SIZE); active_slot = 1;
    }
    sequence = 0;
    if (valid(record)) {
        sequence = word(record + 2);
        settings.resets = word(record + 4);
        settings.sent = word(record + 6);
        settings.received = word(record + 8);
        settings.calls = word(record + 10);
        memcpy(settings.emergency, record + 12, SMS_PHONE_SIZE);
    } else {
        uint16_t *counters[] = { &settings.resets, &settings.sent, &settings.received, &settings.calls };
        for (uint8_t i = 0; i < 4; ++i) {
            uint16_t n = eeprom_read_word((const uint16_t *)(uintptr_t)(i * 2));
            *counters[i] = n == 0xffff ? 0 : n;
        }
        strcpy_P(settings.emergency, PSTR("+36309791403"));
    }
    if (settings.resets != UINT16_MAX) ++settings.resets;
    dirty = 1; writing = 0;
}
void persistence_dirty(void) { dirty = 1; }
static void putword(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
void persistence_poll(void)
{
    if (!eeprom_is_ready()) return;
    if (!writing) {
        if (!dirty) return;
        dirty = 0;
        memset(record, 0, sizeof record);
        record[1] = 1;
        putword(record + 2, ++sequence);
        putword(record + 4, settings.resets);
        putword(record + 6, settings.sent);
        putword(record + 8, settings.received);
        putword(record + 10, settings.calls);
        memcpy(record + 12, settings.emergency, SMS_PHONE_SIZE);
        putword(record + 29, crc(record));
        active_slot ^= 1;
        position = 0; writing = 1;
    }
    /* Invalidate first, write payload one byte/loop, publish marker last. */
    uint16_t address = base(active_slot);
    uint8_t value;
    if (position == 0) value = 0;
    else if (position < RECORD_SIZE) { address += position; value = record[position]; }
    else value = 0xa5;
    eeprom_update_byte((uint8_t *)(uintptr_t)address, value);
    if (++position > RECORD_SIZE) writing = 0;
}
