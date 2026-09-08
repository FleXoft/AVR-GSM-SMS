#include "application.h"
#include "gsm_services.h"
#include "persistence.h"
#include "sensors.h"
#include "flash.h"
#include "timing.h"
#include <string.h>
#include <stdio.h>
#ifndef __AVR__
#define snprintf_P snprintf
#define strcpy_P strcpy
#endif
uint8_t movement_armed, power_armed, relays, input_movement, input_power;
uint8_t last_sms_result, call_connected;
char last_sms_number[17];
static uint8_t pending_alerts, alert_inflight, power_lost, raw_inputs, stable_inputs;
static uint32_t changed_at, retry_after, current_time;
static void increment(uint16_t *value)
{
    if (*value != UINT16_MAX) ++*value;
    persistence_dirty();
}
void application_init(void)
{
    movement_armed = relays = pending_alerts = alert_inflight = power_lost = 0;
    power_armed = 1;
    stable_inputs = raw_inputs = 2; /* expected normal state: no movement, power present */
    input_power = 1; input_movement = 0;
    retry_after = changed_at = 0;
    last_sms_result = call_connected = 0;
    last_sms_number[0] = 0;
}
static void status_text(char out[161])
{
    char t1[8], t2[8];
    sensor_format(t1, sensor_half_c[0]); sensor_format(t2, sensor_half_c[1]);
    snprintf_P(out, 161, PSTR("STATUS BEGIN\nMove:%u Armed:%u\nPower:%u Watch:%u\n1:R%u %s\n2:R%u %s\nSMS s:%u r:%u\nCalls:%u Resets:%u\nSTATUS END"),
        input_movement, movement_armed, input_power, power_armed,
        !!(relays & 1), t1, !!(relays & 2), t2,
        settings.sent, settings.received, settings.calls, settings.resets);
}
/* Exact tokens, not substring matches (e.g. NOTR1ON must not switch a relay). */
enum action { HELP, STATUS, R1ON, R1OFF, R2ON, R2OFF, ENABLE, DISABLE, POWER, SETNUMBER, INVALID };
static enum action parse(const char *token)
{
    if (!strcmp_P(token, PSTR("HELP"))) return HELP;
    if (!strcmp_P(token, PSTR("STATUS"))) return STATUS;
    if (!strcmp_P(token, PSTR("R1ON")) || !strcmp_P(token, PSTR("SETRELAY1ON"))) return R1ON;
    if (!strcmp_P(token, PSTR("R1OFF")) || !strcmp_P(token, PSTR("SETRELAY1OFF"))) return R1OFF;
    if (!strcmp_P(token, PSTR("R2ON")) || !strcmp_P(token, PSTR("SETRELAY2ON"))) return R2ON;
    if (!strcmp_P(token, PSTR("R2OFF")) || !strcmp_P(token, PSTR("SETRELAY2OFF"))) return R2OFF;
    if (!strcmp_P(token, PSTR("ENABLE"))) return ENABLE;
    if (!strcmp_P(token, PSTR("DISABLE"))) return DISABLE;
    if (!strcmp_P(token, PSTR("POWER"))) return POWER;
    if (!strcmp_P(token, PSTR("SETNUMBER"))) return SETNUMBER;
    return INVALID;
}
static uint8_t separator(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == ',' || c == ';'; }
static enum action token(const char **cursor)
{
    char word[16];
    uint8_t n = 0;
    const char *p = *cursor;
    while (*p && separator(*p)) ++p;
    while (*p && !separator(*p)) {
        if (n == sizeof word - 1) return INVALID;
        char c = *p++;
        word[n++] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
    }
    while (*p && separator(*p)) ++p;
    word[n] = 0; *cursor = p;
    return parse(word);
}
uint8_t app_sms_received(const char *number, const char *text)
{
    if (!sms_phone_valid(number)) return 0;
    const char *p = text;
    uint8_t reply = 0, invalid = !*p;
    /* Validate the whole message before any physical side effects. */
    while (*p) {
        enum action a = token(&p);
        if (a == INVALID) { invalid = 1; break; }
        if (a == STATUS || a == HELP) reply = 1;
    }
    char response[161];
    if (invalid) {
        strcpy_P(response, PSTR("Unknown command. Send HELP."));
        if (!gsm_queue_sms(number, response)) return 0;
    } else {
        /* Single-threaded scheduler: reserve capacity before physical effects. */
        if (reply && !gsm_sms_available()) return 0;
        p = text;
        while (*p) {
            enum action a = token(&p);
            if (a >= R1ON && a <= R2OFF) {
                uint8_t index = a >= R2ON, on = a == R1ON || a == R2ON;
                if (on) relays |= 1 << index; else relays &= ~(1 << index);
                app_set_relay(index, on);
            } else if (a == ENABLE || a == POWER) movement_armed = 1;
            else if (a == DISABLE) movement_armed = 0;
            else if (a == SETNUMBER) {
                memset(settings.emergency, 0, sizeof settings.emergency);
                strcpy(settings.emergency, number);
                persistence_dirty();
            }
            if (a == POWER) power_armed = 1;
        }
    }
    if (!invalid && reply) {
        status_text(response);
        p = text;
        while (*p) if (token(&p) == HELP) {
            strcpy_P(response, PSTR("HELP STATUS SETNUMBER\nR1ON R1OFF R2ON R2OFF\nSETRELAY1ON/OFF SETRELAY2ON/OFF\nENABLE DISABLE POWER"));
            break;
        }
        gsm_queue_sms(number, response);
        strcpy(last_sms_number, number);
    }
    increment(&settings.received);
    app_beep(100);
    return 1;
}
void app_sms_sent(uint8_t result)
{
    last_sms_result = result;
    if (alert_inflight && result == 0) {
        pending_alerts |= alert_inflight;
        retry_after = current_time + 60000;
    }
    alert_inflight = 0;
    if (result == 1) increment(&settings.sent);
}
void app_call_answered(void)
{
    call_connected = 1;
    increment(&settings.calls);
}
void app_call_ended(void) { call_connected = 0; }
void app_ring(void) { app_beep(250); }
void app_power_restored(void)
{
    /* Physical OPT2 transition is authoritative; repeated charging URCs are deduplicated. */
    if (power_lost && input_power) {
        pending_alerts |= 4; power_lost = 0; power_armed = 1;
    }
}
void application_poll(uint32_t now, uint8_t movement, uint8_t power)
{
    current_time = now;
    uint8_t inputs = (!!movement) | (!!power << 1);
    if (inputs != raw_inputs) { raw_inputs = inputs; changed_at = now; }
    if (time_due(now, changed_at + 100)) stable_inputs = raw_inputs;
    input_movement = stable_inputs & 1;
    input_power = !!(stable_inputs & 2);
    if (movement_armed && input_movement) {
        pending_alerts |= 1; movement_armed = 0;
    }
    if (power_armed && !input_power) {
        pending_alerts |= 2; power_armed = 0; power_lost = 1;
    }
    if (power_lost && input_power) {
        pending_alerts |= 4; power_lost = 0; power_armed = 1;
    }
    if (!pending_alerts || !time_due(now, retry_after)) return;
    char text[100];
    uint8_t bit;
    if (pending_alerts & 1) {
        bit = 1; strcpy_P(text, PSTR("BALATON STATUS BEGIN\n!!!Movement detected!!!\nBALATON STATUS END"));
    } else if (pending_alerts & 2) {
        bit = 2; strcpy_P(text, PSTR("BALATON STATUS BEGIN\n!!!PowerCut!!!\nBALATON STATUS END"));
    } else {
        bit = 4; strcpy_P(text, PSTR("BALATON STATUS BEGIN\nPower is back again in the house!\nBALATON STATUS END"));
    }
    if (gsm_queue_sms(settings.emergency, text)) {
        pending_alerts &= ~bit;
        alert_inflight = bit;
        last_sms_result = 3;
        strcpy(last_sms_number, settings.emergency);
    } else retry_after = now + 1000;
}
