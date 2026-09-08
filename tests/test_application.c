#include "application.h"
#include "gsm_services.h"
#include "persistence.h"
#include "sensors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
persistent_state_t settings;
int16_t sensor_half_c[2];
static uint8_t full, pins;
static unsigned queued, dirty;
static char output[161], destination[17];
uint8_t gsm_sms_available(void) { return !full; }
uint8_t gsm_queue_sms(const char *n, const char *t)
{
    if (full) return 0;
    full = 1; ++queued;
    assert(strlen(t) <= 160); strcpy(output, t); strcpy(destination, n); return 1;
}
void persistence_dirty(void) { ++dirty; }
void app_set_relay(uint8_t index, uint8_t on)
{
    if (on) pins |= 1 << index; else pins &= ~(1 << index);
}
void app_beep(uint16_t ms) { (void)ms; }
void sensor_format(char out[8], int16_t t) { (void)t; strcpy(out, "+25.5C"); }
static void reset(void)
{
    memset(&settings, 0, sizeof settings); strcpy(settings.emergency, "+36309791403");
    full = pins = queued = dirty = 0; application_init();
}
int main(void)
{
    reset();
    assert(app_sms_received("+36301234567", "R1ON R2ON")); assert(pins == 3 && settings.received == 1);
    assert(app_sms_received("+36301234567", "SETRELAY1OFF;setrelay2off")); assert(pins == 0);
    assert(app_sms_received("+36301234567", "R1ON NOTR2ON")); assert(pins == 0); /* reject entire message */
    assert(strstr(output, "Unknown"));
    assert(!app_sms_received("+36301234567", "R1ON STATUS")); assert(pins == 0);
    full = 0;
    assert(app_sms_received("+36301234567", "R1ON STATUS")); assert(pins == 1 && strstr(output, "1:R1"));
    full = 0;
    assert(app_sms_received("+36301234567", "HELP")); assert(strstr(output, "SETNUMBER"));
    full = 0;
    assert(app_sms_received("+36301234567", "SETNUMBER")); assert(!strcmp(settings.emergency, "+36301234567"));
    assert(app_sms_received("+36301234567", "POWER")); assert(movement_armed && power_armed);
    application_poll(100, 1, 1); application_poll(200, 1, 1);
    assert(full && !movement_armed && strstr(output, "Movement detected"));
    unsigned before = queued; full = 0; app_sms_sent(1);
    application_poll(300, 1, 1); assert(queued == before);
    assert(app_sms_received("+36301234567", "ENABLE DISABLE")); assert(!movement_armed);
    reset();
    application_poll(10, 0, 0); application_poll(50, 0, 1); application_poll(200, 0, 1);
    assert(queued == 0); /* debounce transient power loss */
    application_poll(300, 0, 0); application_poll(400, 0, 0);
    assert(queued == 1 && strstr(output, "PowerCut"));
    full = 0; app_sms_sent(0); /* definitive failure retried after a minute */
    application_poll(1000, 0, 0); assert(queued == 1);
    application_poll(60401, 0, 0); assert(queued == 2);
    full = 0; app_sms_sent(2); /* uncertain: do not duplicate */
    application_poll(130000, 0, 0); assert(queued == 2);
    application_poll(131000, 0, 1); application_poll(131100, 0, 1);
    assert(queued == 3 && strstr(output, "back again"));
    full = 0; app_sms_sent(1); app_power_restored(); application_poll(132000, 0, 1); assert(queued == 3);
    settings.calls = 65535; app_call_answered(); assert(settings.calls == 65535 && call_connected);
    app_call_ended(); assert(!call_connected);
    puts("Application: exact commands, no partial side effects, STATUS, number changes, debounced one-shot alarms and retry: PASS");
}
