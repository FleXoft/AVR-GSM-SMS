#include "gsm_services.h"
#include "gsm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

gsm_status_t gsm_status;
static char scratch[GSM_LINE_SIZE], sent[64], received[161], recipient[17];
static uint32_t now;
static unsigned rx_count, sent_result, call_count, ring_count, reconnects;
static uint8_t allow_rx, tx_room;
static uint8_t wire[400];
static unsigned wire_length;
void gsm_send_p(const char *s) { assert(strlen(s) < sizeof sent); strcpy(sent, s); }
void gsm_send(const char *s) { gsm_send_p(s); }
uint8_t gsm_try_putc(uint8_t b)
{
    if (!tx_room) return 0;
    assert(wire_length < sizeof wire); wire[wire_length++] = b; return 1;
}
void gsm_rx_error(uint32_t n) { ++gsm_status.rx_errors; services_error(n); }
void gsm_reconnect(uint32_t n) { ++reconnects; services_reset(n); }
uint8_t app_sms_received(const char *n, const char *t)
{
    if (!allow_rx) return 0;
    strcpy(recipient, n); strcpy(received, t); ++rx_count; return 1;
}
void app_sms_sent(uint8_t result) { sent_result = result + 1; }
void app_call_answered(void) { ++call_count; }
void app_call_ended(void) {}
void app_ring(void) { ++ring_count; }
void app_power_restored(void) {}
static void reset(void)
{
    now = 0; sent[0] = 0; rx_count = sent_result = call_count = ring_count = reconnects = wire_length = 0;
    tx_room = allow_rx = 1;
    gsm_status.registration = 1;
    services_init(scratch, now);
    assert(services_storage("+CPMS: 1,20,1,20,1,20"));
}
static void feed(const char *s)
{
    while (*s) { now += 5; assert(services_feed(*s++, now)); }
}
static void run(const char *expected)
{
    services_yield(); now += 1000;
    assert(services_poll(now, 1));
    assert(!strcmp(sent, expected));
}
/* Build DELIVER independently from the sender encoder. GSM-7 letters are ASCII. */
static unsigned deliver(uint8_t *out, const char *text)
{
    const uint8_t header[] = {0,0,11,0x91,0x63,0x03,0x21,0x43,0x65,0xf7,0,0,0x62,0x90,0x80,0x21,0,0,0};
    memcpy(out, header, sizeof header);
    size_t n = strlen(text);
    out[sizeof header] = n;
    unsigned pos = sizeof header + 1;
    memset(out + pos, 0, 140);
    for (unsigned i = 0; i < n; ++i) {
        uint8_t c = text[i];
        for (unsigned bit = 0; bit < 7; ++bit)
            if (c & (1 << bit)) out[pos + (i * 7 + bit) / 8] |= 1 << ((i * 7 + bit) % 8);
    }
    return pos + (n * 7 + 7) / 8;
}
static void feed_pdu(const char *text)
{
    uint8_t pdu[180]; char s[64];
    unsigned n = deliver(pdu, text);
    snprintf(s, sizeof s, "+CMGR: 0,,%u\r\n", n - 1); feed(s);
    for (unsigned i = 0; i < n; ++i) { snprintf(s, sizeof s, "%02X", pdu[i]); feed(s); }
    feed("\r\nOK\r\n");
}
static void test_receive(void)
{
    reset();
    services_notice("+CMTI: \"SM\",12");
    services_notice("+CMTI: \"SM\",12"); /* de-duplicate queued index */
    run("AT+CMGR=12,1\r");
    char message[161]; memset(message, 'A', 150); message[150] = 0;
    memcpy(message + 20, "\r\nOK\r\nRING\r\n", 12);
    feed_pdu(message);
    assert(rx_count == 1 && !strcmp(received, message));
    assert(!strcmp(recipient, "+36301234567"));
    assert(ring_count == 0 && call_count == 0);
    run("AT+CMGD=12\r");
    feed("ERROR\r\nERROR\r\nERROR\r\n");
    assert(rx_count == 1);
    services_yield(); now += 1000; assert(!services_poll(now, 1));
    now += 60000; run("AT+CMGD=12\r"); feed("OK\r\n");
    assert(rx_count == 1);
    /* Full application queue leaves the SMS in its original unread state. */
    reset(); allow_rx = 0;
    services_notice("+CMTI: \"SM\",2"); run("AT+CMGR=2,1\r"); feed_pdu("STATUS");
    services_yield(); now += 1000; assert(!services_poll(now, 1)); assert(rx_count == 0);
    /* Reset/notification loss recovery scans the SIM. */
    now = 61000; run("AT+CMGR=1,1\r"); feed("+CMS ERROR: 321\r\n");
    run("AT+CMGR=2,1\r"); allow_rx = 1; feed_pdu("R1ON");
    assert(rx_count == 1); run("AT+CMGD=2\r"); feed("OK\r\n");
}
static void test_bad_receive(void)
{
    reset(); services_notice("+CMTI: \"SM\",5"); run("AT+CMGR=5,1\r");
    feed("+CMGR: 0,,30\r\n00G1\r\nOK\r\n");
    assert(rx_count == 0 && sms_rejected == 1);
    services_yield(); now += 1000; assert(!services_poll(now, 1));
    reset(); services_notice("+CMTI: \"SM\",5"); run("AT+CMGR=5,1\r");
    feed("+CMGR: 0,,30\r\n0000"); services_error(now);
    assert(rx_count == 0);
}
static void test_send(void)
{
    reset(); assert(gsm_queue_sms("+36301234567", "STATUS\nOK"));
    assert(!gsm_queue_sms("+36301234567", "SECOND"));
    services_yield(); now += 1000; assert(services_poll(now, 1));
    assert(!strncmp(sent, "AT+CMGS=", 8));
    feed("\r\n> ");
    tx_room = 0; assert(services_poll(now, 1)); assert(wire_length == 0);
    tx_room = 1;
    for (unsigned i = 0; i < 30; ++i) { now += 5; services_poll(now, 1); }
    assert(wire_length && wire[wire_length - 1] == 26 && sent_result == 0);
    feed("+CMGS: 42\r\nOK\r\n"); assert(sent_result == 2 && gsm_sms_available());
    /* Ctrl-Z sent, no receipt: uncertain outcome, never re-submit automatically. */
    assert(gsm_queue_sms("+36301234567", "R1ON"));
    services_yield(); now += 1000; services_poll(now, 1); feed("> ");
    for (unsigned i = 0; i < 30; ++i) { now += 5; services_poll(now, 1); }
    now += 60001; services_poll(now, 1);
    assert(sent_result == 3 && sms_uncertain == 1 && gsm_sms_available());
}
static void test_cancel(void)
{
    reset(); assert(gsm_queue_sms("+36301234567", "STATUS"));
    services_yield(); now += 1000; assert(services_poll(now, 1));
    now += 5001; services_poll(now, 1);
    assert(sent_result == 1 && wire_length && wire[wire_length - 1] == 27);
    /* A late prompt is cancelled; no fresh AT command may enter the SMS body. */
    feed("> "); now += 1000; assert(services_poll(now, 1));
    feed("\r\nOK\r\n"); assert(!services_poll(now, 0));
    reset(); assert(gsm_queue_sms("+36301234567", "STATUS"));
    services_yield(); now += 1000; services_poll(now, 1);
    now += 5001; services_poll(now, 1);
    now += 5001; services_poll(now, 1); assert(reconnects == 1);
}
static void test_calls(void)
{
    reset(); services_notice("RING"); services_notice("RING"); assert(ring_count == 1);
    run("ATA\r"); feed("RING\r\nOK\r\n"); assert(call_count == 1);
    run("AT+CLDTMF=3,\"1,2,3,4,5\"\r"); feed("OK\r\n");
    services_notice("RING"); assert(ring_count == 1);
    services_notice("NO CARRIER"); services_notice("RING"); assert(ring_count == 2);
    run("ATA\r"); feed("NO CARRIER\r\nOK\r\n"); assert(call_count == 1);
}
static void test_codec(void)
{
    uint8_t pdu[180]; char n[17], t[161], message[162];
    assert(sms_encode(pdu, "+36301234567", "hello") == 19);
    /* Independent known GSM-7 packing vector for "hello": E8 32 9B FD 06. */
    const uint8_t packed[] = {0xe8,0x32,0x9b,0xfd,0x06};
    assert(!memcmp(pdu + 14, packed, sizeof packed));
    memset(message, 'A', 160); message[160] = 0;
    assert(sms_encode(pdu, "+36301234567", message));
    unsigned size = deliver(pdu, message);
    assert(sms_decode(pdu, size, n, t) && strlen(t) == 160);
    for (unsigned i = 0; i < size; ++i) assert(!sms_decode(pdu, i, n, t));
    pdu[1] |= 0x40; assert(!sms_decode(pdu, size, n, t)); pdu[1] = 0;
    pdu[11] = 8; assert(!sms_decode(pdu, size, n, t));
    message[160] = 'A'; message[161] = 0; assert(!sms_encode(pdu, "+36301234567", message));
    assert(!sms_encode(pdu, "+36\";AT", "R1ON"));
}
int main(void)
{
    test_codec(); test_receive(); test_bad_receive(); test_send(); test_cancel(); test_calls();
    puts("Services/PDU: receive 150/160 bytes, framing, SIM scan, exact delete, TX backpressure, uncertain delivery, calls: PASS");
}
