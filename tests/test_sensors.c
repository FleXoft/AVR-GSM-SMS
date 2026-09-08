#include "sensors.h"
#include <avr/io.h>
#include <util/twi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint8_t TWSR, TWBR, TWCR, TWDR;
static uint32_t now;
static void ack(uint8_t status, uint8_t data)
{
    TWSR = status; TWDR = data; TWCR = _BV(TWINT); sensors_poll(++now);
    TWCR &= ~_BV(TWINT); /* hardware consumes W1C interrupt flag */
}
static void temperature(uint8_t high, uint8_t low, uint8_t address)
{
    sensors_poll(now); TWCR &= ~_BV(TWINT);
    assert(TWCR & _BV(TWSTA));
    ack(TW_START, 0); assert(TWDR == address * 2);
    ack(TW_MT_SLA_ACK, 0); assert(TWDR == 0);
    ack(TW_MT_DATA_ACK, 0); assert(TWCR & _BV(TWSTA));
    ack(TW_REP_START, 0); assert(TWDR == address * 2 + 1);
    ack(TW_MR_SLA_ACK, 0); assert(TWCR & _BV(TWEA));
    ack(TW_MR_DATA_ACK, high); assert(!(TWCR & _BV(TWEA)));
    ack(TW_MR_DATA_NACK, low); assert(TWCR & _BV(TWSTO));
    TWCR = 0; sensors_poll(++now); now += 1000;
}
int main(void)
{
    sensors_init();
    assert(sensor_half_c[0] == TEMP_UNKNOWN);
    temperature(0x19, 0x80, 0x48); assert(sensor_half_c[0] == 51);
    temperature(0xff, 0x80, 0x49); assert(sensor_half_c[1] == -1);
    char text[8]; sensor_format(text, -1); assert(!strcmp(text, "-0.5C"));
    sensor_format(text, -110); assert(!strcmp(text, "-55.0C"));
    sensor_format(text, 250); assert(!strcmp(text, "+125.0C"));
    sensor_format(text, TEMP_UNKNOWN); assert(!strcmp(text, "n/a"));
    /* Stuck TWINT and STOP must both time out without a polling loop. */
    sensors_poll(now); TWCR = 0;
    now += 21; sensors_poll(now); assert(sensor_half_c[0] == TEMP_UNKNOWN);
    now += 21; sensors_poll(now); now += 1000;
    sensors_poll(now); ack(TW_START, 0); ack(0x20, 0); /* missing sensor NACK */
    assert(sensor_half_c[1] == TEMP_UNKNOWN);
    puts("LM75: register sequence, two addresses, signed half degrees, NACK and stuck bus timeout: PASS");
}
