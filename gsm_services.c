#include "gsm_services.h"
#include "gsm.h"
#include "flash.h"
#include "timing.h"
#include <string.h>
#include <stdio.h>
#ifndef __AVR__
#define snprintf_P snprintf
#endif

enum job { NONE, READ, DELETE, SEND, ANSWER, TONE, CANCEL };
enum step { REPLY, PDU_LINE, PROMPT, TRANSMIT, SENT_REPLY };
static enum job job;
static enum step step;
static char *line;
static uint16_t used, expected, active_index, delete_index, scan_index, capacity;
static uint16_t queue[4], tx_length, tx_pos;
static uint8_t queued, attempts, served, got_payload, bad_line, nibble;
static uint8_t ring_pending, call_active, tone_pending;
static uint32_t deadline, delete_after, scan_after, cancel_after;
static uint8_t tx_pdu[SMS_PDU_SIZE];
static char sender[SMS_PHONE_SIZE], body[SMS_MAX_TEXT + 1];
uint16_t sms_rejected, sms_failed, sms_uncertain;

static int integer(const char *s)
{
    while (*s == ' ') ++s;
    uint32_t n = 0;
    uint8_t digits = 0;
    while (*s >= '0' && *s <= '9') {
        if (++digits > 5) return -1;
        n = n * 10 + (*s++ - '0');
        if (n > 32767) return -1;
    }
    while (*s == ' ') ++s;
    return digits && (!*s || *s == ',') ? (int)n : -1;
}
uint8_t services_storage(const char *s)
{
    const char *p = strchr(s, ',');
    int n = p ? integer(p + 1) : -1;
    if (n < 1 || n > 255) return 0;
    capacity = n;
    return 1;
}
void services_init(char *scratch, uint32_t now)
{
    line = scratch;
    job = NONE;
    queued = attempts = served = ring_pending = call_active = tone_pending = 0;
    tx_length = delete_index = 0;
    scan_index = capacity = 1;
    scan_after = now + 60000;
    sms_rejected = sms_failed = sms_uncertain = 0;
}
void services_yield(void) { served = 0; }
static void done(void)
{
    job = NONE;
    served = 1;
    used = got_payload = bad_line = 0;
}
void services_reset(uint32_t now)
{
    (void)now;
    if (job == SEND) {
        uint8_t result = step == SENT_REPLY ? 2 : 0;
        if (result == 2) ++sms_uncertain; else ++sms_failed;
        tx_length = 0;
        app_sms_sent(result);
    }
    ring_pending = call_active = tone_pending = 0;
    app_call_ended();
    done();
}
uint8_t gsm_sms_available(void) { return tx_length == 0; }
uint8_t gsm_queue_sms(const char *number, const char *text)
{
    if (tx_length) return 0;
    tx_length = sms_encode(tx_pdu, number, text);
    return tx_length != 0;
}
void services_notice(const char *s)
{
    if (!strncmp_P(s, PSTR("+CMTI:"), 6)) {
        const char *comma = strchr(s, ',');
        int notice = comma ? integer(comma + 1) : -1;
        if (notice > 0) {
            ++gsm_status.sms_notices;
            gsm_status.sms_index = notice;
        }
        const char *p = strchr(s, '"');
        if (!p || strncmp_P(p, PSTR("\"SM\","), 5)) return;
        int index = integer(p + 5);
        if (index < 1 || index > 255 || (uint16_t)index == delete_index) return;
        if (job == READ && (uint16_t)index == active_index) return;
        for (uint8_t i = 0; i < queued; ++i) if (queue[i] == (uint16_t)index) return;
        if (queued < 4) queue[queued++] = index;
        /* A full queue loses no SIM records: the background scan recovers them. */
    } else if (!strcmp_P(s, PSTR("RING"))) {
        if (!call_active && !ring_pending) { ring_pending = 1; app_ring(); }
    } else if (!strcmp_P(s, PSTR("NO CARRIER"))) {
        ring_pending = call_active = tone_pending = 0;
        app_call_ended();
    } else if (!strcmp_P(s, PSTR("Charging in NORMAL MODE"))) {
        app_power_restored();
    }
}
static void begin(enum job next, uint32_t now)
{
    char command[32];
    job = next;
    step = next == SEND ? PROMPT : REPLY;
    used = got_payload = bad_line = nibble = 0;
    deadline = now + (next == ANSWER ? 30000 : 5000);
    switch (next) {
    case READ: snprintf_P(command, sizeof command, PSTR("AT+CMGR=%u,1\r"), active_index); gsm_send(command); break;
    case DELETE: snprintf_P(command, sizeof command, PSTR("AT+CMGD=%u\r"), delete_index); gsm_send(command); break;
    case SEND: snprintf_P(command, sizeof command, PSTR("AT+CMGS=%u\r"), tx_length - 1); gsm_send(command); break;
    case ANSWER: gsm_send_p(PSTR("ATA\r")); break;
    case TONE: gsm_send_p(PSTR("AT+CLDTMF=3,\"1,2,3,4,5\"\r")); break;
    default: break;
    }
}
static void failure(uint32_t now, uint8_t uncertain)
{
    uint8_t cancel = job == SEND && step != SENT_REPLY;
    ++sms_failed;
    if (job == SEND) {
        /* Never automatically retransmit an SMS after Ctrl-Z: delivery may have happened. */
        if (step != SENT_REPLY) gsm_try_putc(27); /* cancel any pending CMGS prompt */
        tx_length = 0;
        if (uncertain) ++sms_uncertain;
        app_sms_sent(uncertain ? 2 : 0);
    } else if (job == DELETE) {
        delete_after = now + 60000; /* preserve delete intent, never execute twice */
    } else if (job == ANSWER) {
        ring_pending = 0;
    } else if (job == TONE) {
        tone_pending = 0;
    }
    done();
    if (cancel) {
        job = CANCEL; step = REPLY;
        deadline = now + 5000; cancel_after = now;
    }
}
void services_error(uint32_t now)
{
    if (job != NONE) failure(now, job == SEND && step == SENT_REPLY);
}
static void received_line(uint32_t now)
{
    if (!strcmp_P(line, PSTR("RDY")) || !strcmp_P(line, PSTR("NORMAL POWER DOWN"))) {
        gsm_reconnect(now); return;
    }
    services_notice(line);
    if (job == CANCEL) {
        if (!strcmp_P(line, PSTR("OK")) || !strcmp_P(line, PSTR("ERROR"))) done();
        return;
    }
    if (!strcmp_P(line, PSTR("ERROR")) || !strncmp_P(line, PSTR("+CMS ERROR:"), 11) ||
        !strncmp_P(line, PSTR("+CME ERROR:"), 11)) {
        if (job == DELETE && !strcmp_P(line, PSTR("+CMS ERROR: 321"))) {
            delete_index = 0; done(); return;
        }
        /* Empty SIM slot is a normal background scan outcome. */
        if (job == READ && !strcmp_P(line, PSTR("+CMS ERROR: 321"))) { done(); return; }
        if (job != SEND && ++attempts < 3) { begin(job, now); return; }
        failure(now, 0); return;
    }
    if (job == READ && !strncmp_P(line, PSTR("+CMGR:"), 6)) {
        int status = integer(line + 6);
        const char *p = strrchr(line, ',');
        int n = p ? integer(p + 1) : -1;
        if ((status != 0 && status != 1) || n < 1 || n > SMS_PDU_SIZE - 1) {
            ++sms_rejected; done(); return;
        }
        expected = n;
        step = PDU_LINE;
        nibble = 0;
        return;
    }
    if (job == SEND && step == SENT_REPLY && !strncmp_P(line, PSTR("+CMGS:"), 6)) {
        got_payload = integer(line + 6) >= 0;
        return;
    }
    if (strcmp_P(line, PSTR("OK"))) return;
    switch (job) {
    case READ:
        if (got_payload && app_sms_received(sender, body)) {
            delete_index = active_index;
            delete_after = now;
        }
        break;
    case DELETE: delete_index = 0; break;
    case SEND:
        if (!got_payload || step != SENT_REPLY) { failure(now, step == SENT_REPLY); return; }
        tx_length = 0;
        app_sms_sent(1);
        break;
    case ANSWER:
        if (ring_pending) {
            ring_pending = 0; call_active = 1; tone_pending = 1;
            app_call_answered();
        }
        break;
    case TONE: tone_pending = 0; break;
    default: break;
    }
    done();
}
uint8_t services_feed(uint8_t byte, uint32_t now)
{
    if (job == NONE) return 0;
    if (step == PROMPT && byte == '>') {
        step = TRANSMIT;
        tx_pos = 0;
        used = 0;
        deadline = now + 5000;
        return 1;
    }
    if (step == TRANSMIT) return 1; /* optional prompt space; no commands while streaming */
    if (step == PDU_LINE) {
        if (byte == '\r') return 1;
        if (byte == '\n') {
            if (!bad_line && !nibble && used > 0 && used == 1u + (uint8_t)line[0] + expected &&
                sms_decode((uint8_t *)line, used, sender, body)) got_payload = 1;
            else ++sms_rejected;
            step = REPLY;
            used = bad_line = 0;
            return 1;
        }
        int h = byte >= '0' && byte <= '9' ? byte - '0' :
            byte >= 'A' && byte <= 'F' ? byte - 'A' + 10 :
            byte >= 'a' && byte <= 'f' ? byte - 'a' + 10 : -1;
        if (h < 0 || used >= SMS_PDU_SIZE) { bad_line = 1; return 1; }
        if (!nibble) line[used] = h << 4;
        else line[used++] |= h;
        nibble = !nibble;
        return 1;
    }
    if (byte == '\n') {
        if (!bad_line && used) {
            line[used] = 0;
            received_line(now);
        }
        used = bad_line = 0;
    } else if (byte != '\r') {
        if (used >= GSM_LINE_SIZE - 1 || !byte) { gsm_rx_error(now); return 1; }
        else if (!bad_line) line[used++] = byte;
    }
    return 1;
}
uint8_t services_poll(uint32_t now, uint8_t allow_start)
{
    if (job == CANCEL) {
        if (time_due(now, deadline)) { gsm_reconnect(now); return 0; }
        if (time_due(now, cancel_after) && gsm_try_putc(27)) cancel_after = now + 1000;
        return 1;
    }
    if (job != NONE) {
        if (time_due(now, deadline)) {
            failure(now, job == SEND && step == SENT_REPLY);
            return 0;
        }
        if (step == TRANSMIT) {
            /* At most 16 bytes each loop; no uart_puts() of a whole SMS. */
            for (uint8_t n = 0; n < 16; ++n) {
                if (tx_pos == tx_length * 2) {
                    if (gsm_try_putc(26)) { step = SENT_REPLY; deadline = now + 60000; }
                    break;
                }
                uint8_t h = (tx_pdu[tx_pos / 2] >> ((tx_pos & 1) ? 0 : 4)) & 15;
                if (!gsm_try_putc(h < 10 ? '0' + h : 'A' + h - 10)) break;
                ++tx_pos;
            }
        }
        return 1;
    }
    if (served || !allow_start) return 0;
    attempts = 0;
    if (delete_index && time_due(now, delete_after)) begin(DELETE, now);
    else if (ring_pending) begin(ANSWER, now);
    else if (tone_pending) begin(TONE, now);
    else if (tx_length && (gsm_status.registration == 1 || gsm_status.registration == 5)) begin(SEND, now);
    else if (!delete_index && queued) {
        active_index = queue[0];
        memmove(queue, queue + 1, (--queued) * sizeof queue[0]);
        begin(READ, now);
    } else if (!delete_index && time_due(now, scan_after)) {
        active_index = scan_index++;
        if (scan_index > capacity) { scan_index = 1; scan_after = now + 60000; }
        else scan_after = now + 1000;
        begin(READ, now);
    }
    return job != NONE;
}
