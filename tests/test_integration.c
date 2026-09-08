#include "gsm.h"
#include "gsm_services.h"
#include "application.h"
#include "persistence.h"
#include "sensors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
persistent_state_t settings;
int16_t sensor_half_c[2] = {51,-1};
static char response[600];
static unsigned read_at, written, deletes, readings, ctrl_z;
static uint8_t pins;
static uint32_t now;
void persistence_dirty(void) {}
void app_set_relay(uint8_t index, uint8_t on) { if (on) pins |= 1 << index; else pins &= ~(1 << index); }
void app_beep(uint16_t ms) { (void)ms; }
void sensor_format(char out[8], int16_t t) { strcpy(out, t > 0 ? "+25.5C" : "-0.5C"); }
void gsm_power_key(uint8_t p) { assert(!p); }
static void reply(const char *s)
{
    assert(read_at == written);
    strcpy(response, s); read_at = 0; written = strlen(s);
}
static void incoming(void)
{
    /* Independent DELIVER PDU for R1ON STATUS. */
    const char *text = "R1ON STATUS";
    uint8_t pdu[160] = {0,0,11,0x91,0x63,0x03,0x21,0x43,0x65,0xf7,0,0,0x62,0x90,0x80,0x21,0,0,0,11};
    for (unsigned i = 0; i < 11; ++i)
        for (unsigned b = 0; b < 7; ++b)
            if (text[i] & (1 << b)) pdu[20 + (i * 7 + b) / 8] |= 1 << ((i * 7 + b) % 8);
    char transcript[300]; unsigned length = 20 + (11 * 7 + 7) / 8;
    unsigned pos = snprintf(transcript, sizeof transcript, "+CMGR: 0,,%u\r\n", length - 1);
    for (unsigned i = 0; i < length; ++i) pos += snprintf(transcript + pos, sizeof transcript - pos, "%02X", pdu[i]);
    strcpy(transcript + pos, "\r\nOK\r\n"); reply(transcript);
}
void gsm_send_p(const char *s)
{
    if (!strcmp(s, "AT+CPIN?\r")) reply("+CPIN: READY\r\nOK\r\n");
    else if (!strncmp(s, "AT+CPMS=", 8)) reply("+CPMS: 1,20,1,20,1,20\r\nOK\r\n");
    else if (!strcmp(s, "AT+CREG?\r")) reply("+CREG: 0,1\r\nOK\r\n");
    else if (!strcmp(s, "AT+CSQ\r")) reply("+CSQ: 20,0\r\nOK\r\n");
    else if (!strcmp(s, "AT+CBC\r")) reply("+CBC: 0,90,4200\r\nOK\r\n");
    else if (!strcmp(s, "AT+COPS?\r")) reply("+COPS: 0,0,\"Test Network\"\r\nOK\r\n");
    else if (!strcmp(s, "AT+CCLK?\r")) reply("+CCLK: \"26/09/08,12:00:00+08\"\r\nOK\r\n");
    else if (!strcmp(s, "AT+CMTE?\r")) reply("+CMTE: 25\r\nOK\r\n");
    else if (!strcmp(s, "AT+CMGR=12,1\r")) { ++readings; incoming(); }
    else if (!strcmp(s, "AT+CMGD=12\r")) { ++deletes; reply("OK\r\n"); }
    else if (!strncmp(s, "AT+CMGS=", 8)) reply("\r\n> ");
    else reply("OK\r\n");
}
void gsm_send(const char *s) { gsm_send_p(s); }
uint8_t gsm_try_putc(uint8_t byte)
{
    if (byte == 26) { ++ctrl_z; reply("+CMGS: 1\r\nOK\r\n"); }
    return 1;
}
int main(void)
{
    strcpy(settings.emergency, "+36309791403"); application_init(); gsm_init(0);
    uint8_t notified = 0;
    for (now = 0; now < 50000; now += 5) {
        if (read_at < written) gsm_feed(response[read_at++], now);
        if (gsm_status.ready && !notified && read_at == written) {
            reply("+CMTI: \"SM\",12\r\n"); notified = 1;
        }
        gsm_poll(now);
        application_poll(now, 0, 1);
    }
    assert(gsm_status.ready && gsm_status.registration == 1);
    assert(pins == 1 && settings.received == 1 && settings.sent == 1);
    assert(readings == 1 && deletes == 1 && ctrl_z == 1);
    assert(last_sms_result == 1 && !strcmp(last_sms_number, "+36301234567"));
    puts("End-to-end: init -> CMTI -> PDU read -> R1ON STATUS -> exact delete -> SMS acknowledgement -> status polling: PASS");
}
