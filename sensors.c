#include "sensors.h"
#include "timing.h"
#include <avr/io.h>
#include <util/twi.h>
#include <stdio.h>
#include <string.h>
#include "flash.h"

int16_t sensor_half_c[2];
static uint8_t state, sensor, msb;
static uint32_t deadline, next_sample;
void sensors_init(void)
{
    state = sensor = 0;
    next_sample = 0;
    TWSR = 0;
    TWBR = (F_CPU / 100000UL - 16) / 2;
    TWCR = _BV(TWEN);
    sensor_half_c[0] = sensor_half_c[1] = TEMP_UNKNOWN;
}
static void advance(uint8_t next, uint8_t control, uint32_t now)
{
    state = next;
    deadline = now + 20;
    TWCR = control | _BV(TWINT) | _BV(TWEN);
}
static void finish(uint32_t now, uint8_t success)
{
    if (!success) sensor_half_c[sensor] = TEMP_UNKNOWN;
    TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
    state = 8;
    deadline = now + 20;
}
void sensors_poll(uint32_t now)
{
    if (!state) {
        if (time_due(now, next_sample)) advance(1, _BV(TWSTA), now);
        return;
    }
    if (state == 8) {
        if (!(TWCR & _BV(TWSTO)) || time_due(now, deadline)) {
            TWCR = 0; TWCR = _BV(TWEN);
            sensor ^= 1;
            state = 0; next_sample = now + 1000;
        }
        return;
    }
    if (time_due(now, deadline)) { finish(now, 0); return; }
    if (!(TWCR & _BV(TWINT))) return;
    uint8_t status = TW_STATUS;
    switch (state) {
    case 1:
        if (status != TW_START) break;
        TWDR = (0x48 + sensor) << 1; advance(2, 0, now); return;
    case 2:
        if (status != TW_MT_SLA_ACK) break;
        TWDR = 0; advance(3, 0, now); return;
    case 3:
        if (status != TW_MT_DATA_ACK) break;
        advance(4, _BV(TWSTA), now); return;
    case 4:
        if (status != TW_REP_START) break;
        TWDR = ((0x48 + sensor) << 1) | 1; advance(5, 0, now); return;
    case 5:
        if (status != TW_MR_SLA_ACK) break;
        advance(6, _BV(TWEA), now); return;
    case 6:
        if (status != TW_MR_DATA_ACK) break;
        msb = TWDR; advance(7, 0, now); return;
    case 7:
        if (status != TW_MR_DATA_NACK) break;
        /* Signed 9-bit LM75 value, one unit = 0.5 C. */
        sensor_half_c[sensor] = (int16_t)((uint16_t)msb << 8 | TWDR) / 128;
        finish(now, 1); return;
    default: break;
    }
    finish(now, 0);
}
void sensor_format(char out[8], int16_t half_c)
{
    if (half_c < -110 || half_c > 250) { strcpy_P(out, PSTR("n/a")); return; }
    uint16_t magnitude = half_c < 0 ? -half_c : half_c;
    snprintf_P(out, 8, PSTR("%c%u.%uC"), half_c < 0 ? '-' : '+', magnitude / 2, (magnitude % 2) * 5);
}
