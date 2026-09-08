#include "gsm.h"
#include "gsm_services.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t now;
static unsigned sends, presses, releases;
static char sent[64];
void gsm_send_p(const char *command)
{
    assert(strlen(command) < sizeof sent);
    strcpy(sent, command);
    ++sends;
}
void gsm_power_key(uint8_t down)
{
    if (down) ++presses;
    else ++releases;
}
static void feed(const char *s)
{
    /* Deliver fragmented replies, polling between every byte. */
    while (*s) {
        now += 5;
        gsm_feed((uint8_t)*s++, now);
        gsm_poll(now);
    }
}
static void next(const char *expected)
{
    unsigned before = sends;
    now += 1000;
    gsm_poll(now);
    assert(sends == before + 1);
    assert(!strcmp(sent, expected));
}
static void start(uint32_t initial)
{
    sends = presses = releases = 0;
    now = initial;
    gsm_init(now);
    now += 4995;
    gsm_poll(now);
    assert(sends == 0);
    now += 5;
    gsm_poll(now);
    assert(sends == 1 && !strcmp(sent, "AT\r"));
}
static void initialize(void)
{
    start(0);
    feed("AT\r\n\r\nOK\r\n");
    next("ATE0\r"); feed("\r\nOK\r\n");
    next("AT+IFC=0,0\r"); feed("\r\nOK\r\n");
    next("AT+CPIN?\r"); feed("\r\n+CPIN: READY\r\nOK\r\n");
    next("AT+CMGF=0\r"); feed("\r\nOK\r\n");
    next("AT+CMIC=0,6\r"); feed("OK\r\n");
    next("AT+CPMS=\"SM\",\"SM\",\"SM\"\r"); feed("+CPMS: 0,20,0,20,0,20\r\nOK\r\n");
    next("AT+CNMI=2,1,0,0,0\r"); feed("\r\nOK\r\n");
    assert(gsm_status.ready && gsm_status.sim_ready == 1);
    next("AT+CREG?\r");
    feed("\r\n+CREG: 1\r\n+CREG: 0,5\r\nOK\r\n");
    assert(gsm_status.registration == 5);
    next("AT+CSQ\r");
}
static void test_status(void)
{
    initialize();
    char message[153];
    memset(message, 'x', 150);
    message[150] = '\r'; message[151] = '\n'; message[152] = 0;
    feed(message);
    feed("IGNORED\r\n+CSQ: 17,2\r\n+CMTI: \"ME\",12\r\n");
    assert(gsm_status.rssi == 99); /* don't publish before terminal OK */
    feed("OK\r\n");
    assert(gsm_status.rssi == 17 && gsm_status.ber == 2);
    assert(gsm_status.sms_index == 12 && gsm_status.sms_notices == 1);
    assert(gsm_status.rx_errors == 0);
    next("AT+CBC\r"); feed("+CBC: 1,84,4100\r\nOK\r\n");
    assert(gsm_status.battery_percent == 84 && gsm_status.battery_mv == 4100);
    next("AT+COPS?\r"); feed("+COPS: 0,0,\"Example Mobile Network With Long Name\"\r\nOK\r\n");
    assert(strlen(gsm_status.carrier) == 20);
    next("AT+CCLK?\r"); feed("+CCLK: \"26/09/08,12:00:00+08\"\r\nOK\r\n");
    next("AT+CMTE?\r"); feed("+CMTE: -12\r\nOK\r\n");
    assert(gsm_status.temperature == -12);
    next("AT+CPIN?\r"); feed("+CPIN: READY\r\nOK\r\n");
    next("AT+CREG?\r"); feed("+CREG: 2,1,\"1234\",\"abcd\"\r\nOK\r\n");
    next("AT+CSQ\r"); feed("+CSQ: 99,99\r\nOK\r\n");
    assert(gsm_status.rssi == 99);
    next("AT+CBC\r"); feed("+CBC: 0,100,4200\r\nOK\r\n");
    next("AT+COPS?\r"); feed("+COPS: 0\r\nOK\r\n");
    assert(gsm_status.carrier[0] == 0);
    next("AT+CCLK?\r"); feed("+CCLK: \"26/09/08,12:00:00+08\"\r\nOK\r\n");
    next("AT+CMTE?\r"); feed("+CMTE: 0,26\r\nOK\r\n");
    assert(gsm_status.temperature == 26);
}
static void test_bad_input(void)
{
    initialize();
    feed("+CSQ: 9999999999999999,2\r\nOK\r\n");
    assert(gsm_status.rssi == 99 && gsm_status.errors == 1);
    next("AT+CSQ\r");
    char overlong[GSM_LINE_SIZE + 40];
    memset(overlong, 'a', sizeof overlong - 1);
    overlong[sizeof overlong - 1] = 0;
    feed(overlong); feed("OK\r\n+CSQ: 20,1\r\nOK\r\n");
    assert(gsm_status.rx_errors == 1 && gsm_status.rssi == 99);
    next("AT+CSQ\r"); feed("+CSQ: 20,1\r\nOK\r\n");
    assert(gsm_status.rssi == 20);
    next("AT+CBC\r");
    feed("+CBC: 0,99,4200\r\n");
    gsm_rx_error(now);  /* hardware overrun invalidates the entire transaction */
    feed("OK\r\n");
    assert(gsm_status.battery_mv == -1);
    next("AT+CBC\r"); feed("+CBC: 0,99,4200\r\nOK\r\n");
    assert(gsm_status.battery_mv == 4200);
    next("AT+COPS?\r");
    for (unsigned n = 0; n < 3; ++n) {
        feed("+CME ERROR: 3\r\n");
        if (n < 2) next("AT+COPS?\r");
    }
    next("AT+CCLK?\r"); feed("+CCLK: \"26/09/08,12:00:00+08\"\r\nOK\r\n");
    next("AT+CMTE?\r"); feed("+CMTE: 25\r\nOK\r\n");
    assert(gsm_status.ready && gsm_status.temperature == 25);
    feed("RDY\r\n");
    assert(!gsm_status.ready && gsm_status.rssi == 99 && gsm_status.battery_mv == -1);
}
static void test_timeouts_and_wrap(void)
{
    start(UINT32_MAX - 1000);
    for (unsigned i = 0; i < 60; ++i) {
        now += 5001;
        gsm_poll(now);
    }
    assert(presses == 1 && releases == 2);
    assert(gsm_status.timeouts > 3 && !gsm_status.ready);
    /* Late OK is discarded during retry delay, it cannot send the next command. */
    initialize();
    unsigned before = sends;
    now += 5001;
    gsm_poll(now);
    feed("+CSQ: 20,0\r\nOK\r\n");
    assert(sends == before && gsm_status.rssi == 99);
    next("AT+CSQ\r"); feed("+CSQ: 21,0\r\nOK\r\n");
    assert(gsm_status.rssi == 21);
}
static void test_locked_sim(void)
{
    start(0); feed("OK\r\n");
    next("ATE0\r"); feed("OK\r\n");
    next("AT+IFC=0,0\r"); feed("OK\r\n");
    next("AT+CPIN?\r"); feed("+CPIN: SIM PIN\r\nOK\r\n");
    assert(!gsm_status.ready && gsm_status.sim_ready == 0);
    assert(gsm_status.modem_ready);
    next("AT+CBC\r"); feed("+CBC: 1,99,4187\r\nOK\r\n");
    next("AT+CCLK?\r"); feed("+CCLK: \"26/09/08,12:00:00+08\"\r\nOK\r\n");
    next("AT+CMTE?\r"); feed("+CMTE: 1,32\r\nOK\r\n");
    next("AT+CPIN?\r");
    feed("+CPIN: READY\r\nOK\r\n");
    next("AT+CMGF=0\r");
}
static void local_readings(void)
{
    next("AT+CBC\r"); feed("+CBC: 1,99,4187\r\nOK\r\n");
    assert(gsm_status.battery_percent == 99 && gsm_status.battery_mv == 4187);
    next("AT+CCLK?\r"); feed("+CCLK: \"26/09/08,12:00:00+08\"\r\nOK\r\n");
    next("AT+CMTE?\r"); feed("+CMTE: 1,32\r\nOK\r\n");
    assert(gsm_status.temperature == 32);
    next("AT+CPIN?\r");
}
static void test_no_sim(void)
{
    start(0); feed("OK\r\n");
    next("ATE0\r"); feed("OK\r\n");
    next("AT+IFC=0,0\r"); feed("OK\r\n");
    next("AT+CPIN?\r"); feed("+CME ERROR: SIM not inserted\r\n");
    assert(gsm_status.modem_ready && !gsm_status.ready && gsm_status.sim_ready == 0);
    assert(gsm_status.errors == 0 && presses == 0);
    local_readings();
    feed("+CME ERROR: 10\r\n");
    assert(gsm_status.battery_mv == 4187 && gsm_status.temperature == 32);
    local_readings();
    assert(gsm_status.errors == 0 && presses == 0);
    /* A usable SIM later enables the original SMS initialization path. */
    feed("+CPIN: READY\r\nOK\r\n");
    next("AT+CMGF=0\r"); feed("OK\r\n");
    next("AT+CMIC=0,6\r"); feed("OK\r\n");
    next("AT+CPMS=\"SM\",\"SM\",\"SM\"\r"); feed("+CPMS: 0,20,0,20,0,20\r\nOK\r\n");
    next("AT+CNMI=2,1,0,0,0\r"); feed("OK\r\n");
    assert(gsm_status.ready && gsm_status.sim_ready == 1 && gsm_status.battery_mv == 4187);
}
static void test_sim_removed(void)
{
    initialize(); feed("+CSQ: 20,0\r\nOK\r\n");
    next("AT+CBC\r"); feed("+CBC: 1,99,4187\r\nOK\r\n");
    next("AT+COPS?\r"); feed("+COPS: 0,0,\"Example\"\r\nOK\r\n");
    next("AT+CCLK?\r"); feed("+CCLK: \"26/09/08,12:00:00+08\"\r\nOK\r\n");
    next("AT+CMTE?\r"); feed("+CMTE: 1,32\r\nOK\r\n");
    next("AT+CPIN?\r"); feed("+CME ERROR: SIM not inserted\r\n");
    assert(!gsm_status.ready && gsm_status.modem_ready && gsm_status.sim_ready == 0);
    assert(gsm_status.registration == -1 && gsm_status.carrier[0] == 0);
    assert(gsm_status.battery_mv == 4187 && gsm_status.temperature == 32);
    /* Queued jobs must not run against a missing SIM. */
    assert(gsm_queue_sms("+36301234567", "STATUS"));
    feed("+CMTI: \"SM\",7\r\n");
    local_readings(); feed("+CME ERROR: 10\r\n");
    assert(!gsm_sms_available());
}
static void test_sim_init_error(void)
{
    start(0); feed("OK\r\n");
    next("ATE0\r"); feed("OK\r\n");
    next("AT+IFC=0,0\r"); feed("OK\r\n");
    next("AT+CPIN?\r"); feed("+CPIN: READY\r\nOK\r\n");
    next("AT+CMGF=0\r"); feed("OK\r\n");
    next("AT+CMIC=0,6\r"); feed("OK\r\n");
    for (unsigned i = 0; i < 3; ++i) {
        next("AT+CPMS=\"SM\",\"SM\",\"SM\"\r"); feed("ERROR\r\n");
    }
    assert(!gsm_status.ready && gsm_status.modem_ready);
    local_readings();
    /* Generic CPIN errors also leave the local polling loop running. */
    for (unsigned i = 0; i < 3; ++i) {
        feed("ERROR\r\n");
        if (i < 2) next("AT+CPIN?\r");
    }
    local_readings();
}
int main(void)
{
    test_status();
    test_bad_input();
    test_timeouts_and_wrap();
    test_locked_sim();
    test_no_sim();
    test_sim_removed();
    test_sim_init_error();
    puts("GSM: fragmented replies, 150-byte lines, URCs, overflow, retry, SIM-independent readings, insertion/removal, wrap: PASS");
}

void gsm_send(const char *s) { gsm_send_p(s); }
uint8_t gsm_try_putc(uint8_t byte) { (void)byte; return 1; }
uint8_t app_sms_received(const char *n, const char *t) { (void)n; (void)t; return 1; }
void app_sms_sent(uint8_t result) { (void)result; }
void app_call_answered(void) {}
void app_call_ended(void) {}
void app_ring(void) {}
void app_power_restored(void) {}
