#include "persistence.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
static uint8_t memory[1024], busy;
static unsigned writes;
void eeprom_read_block(void *out, const void *address, size_t n)
{
    uintptr_t a = (uintptr_t)address; assert(a + n <= sizeof memory); memcpy(out, memory + a, n);
}
uint16_t eeprom_read_word(const uint16_t *address)
{
    uintptr_t a = (uintptr_t)address; assert(a + 2 <= sizeof memory); return memory[a] | (uint16_t)memory[a + 1] << 8;
}
uint8_t eeprom_is_ready(void) { return !busy; }
void eeprom_update_byte(uint8_t *address, uint8_t value)
{
    assert(!busy); uintptr_t a = (uintptr_t)address; assert(a < sizeof memory);
    memory[a] = value; ++writes;
}
static void flush(void) { for (unsigned i = 0; i < 70; ++i) persistence_poll(); }
int main(void)
{
    memset(memory, 255, sizeof memory); memory[2] = 42; memory[3] = 0;
    persistence_init(); assert(settings.resets == 1 && settings.sent == 42 && settings.received == 0);
    flush(); unsigned before = writes; flush(); assert(writes == before);
    settings.received = 99; strcpy(settings.emergency, "+36301234567"); persistence_dirty();
    busy = 1; flush(); assert(writes == before); busy = 0; flush();
    persistence_init(); assert(settings.received == 99 && settings.sent == 42 && settings.resets == 2);
    assert(!strcmp(settings.emergency, "+36301234567")); flush();
    uint8_t saved[1024]; memcpy(saved, memory, sizeof memory);
    /* Interrupt every byte boundary of a new journal commit. */
    for (unsigned cut = 0; cut < 33; ++cut) {
        memcpy(memory, saved, sizeof memory); persistence_init();
        settings.received = 100; strcpy(settings.emergency, "+36305555555"); persistence_dirty();
        for (unsigned i = 0; i < cut; ++i) persistence_poll();
        persistence_init();
        assert(settings.received == 99 || settings.received == 100);
        assert(!strcmp(settings.emergency, settings.received == 99 ? "+36301234567" : "+36305555555"));
    }
    puts("EEPROM: legacy migration, no idle writes, busy deferral, two-slot recovery at every commit boundary: PASS");
}
