#include "gsm.h"
#include "gsm_services.h"
#include "flash.h"
#include "timing.h"
#include <string.h>
#include <stdlib.h>

enum command { SYNC, ECHO, FLOW, PIN, TEXT, MICROPHONE, MEMORY, NOTIFY, REG, SIGNAL, BATTERY,
               OPERATOR, CLOCK, TEMP, COMMAND_COUNT };
enum phase { BOOT_WAIT, KEY_DOWN, IDLE, WAIT_REPLY };
static enum command command;
static enum phase phase;
static uint8_t attempts, power_attempted, payload, discarding;
static uint16_t length;
static uint32_t deadline, last_rx;
static char line[GSM_LINE_SIZE];
static int values[3];
static char operator_name[21];
gsm_status_t gsm_status;

static void invalidate(enum command cmd)
{
    switch (cmd) {
    case PIN: gsm_status.sim_ready = -1; break;
    case REG: gsm_status.registration = -1; break;
    case SIGNAL: gsm_status.rssi = gsm_status.ber = 99; break;
    case BATTERY:
        gsm_status.battery_percent = gsm_status.battery_mv = -1; break;
    case OPERATOR: gsm_status.carrier[0] = '\0'; break;
    case CLOCK: gsm_status.clock[0] = '\0'; break;
    case TEMP: gsm_status.temperature = -1000; break;
    default: break;
    }
}

static void clear_readings(void)
{
    for (enum command c = PIN; c < COMMAND_COUNT; ++c) invalidate(c);
}

void gsm_init(uint32_t now)
{
    memset(&gsm_status, 0, sizeof gsm_status);
    gsm_status.sms_index = -1;
    clear_readings();
    command = SYNC;
    phase = BOOT_WAIT;
    deadline = now + 5000;
    last_rx = now;
    length = attempts = payload = discarding = power_attempted = 0;
    services_init(line, now);
    gsm_power_key(0);
}

static void send_command(void)
{
    switch (command) {
    case SYNC: gsm_send_p(PSTR("AT\r")); break;
    case ECHO: gsm_send_p(PSTR("ATE0\r")); break;
    case FLOW: gsm_send_p(PSTR("AT+IFC=0,0\r")); break;
    case PIN: gsm_send_p(PSTR("AT+CPIN?\r")); break;
    case TEXT: gsm_send_p(PSTR("AT+CMGF=0\r")); break;
    /* Store SMS and report index only: never confuse an SMS body with AT replies. */
    case MICROPHONE: gsm_send_p(PSTR("AT+CMIC=0,6\r")); break;
    case MEMORY: gsm_send_p(PSTR("AT+CPMS=\"SM\",\"SM\",\"SM\"\r")); break;
    case NOTIFY: gsm_send_p(PSTR("AT+CNMI=2,1,0,0,0\r")); break;
    case REG: gsm_send_p(PSTR("AT+CREG?\r")); break;
    case SIGNAL: gsm_send_p(PSTR("AT+CSQ\r")); break;
    case BATTERY: gsm_send_p(PSTR("AT+CBC\r")); break;
    case OPERATOR: gsm_send_p(PSTR("AT+COPS?\r")); break;
    case CLOCK: gsm_send_p(PSTR("AT+CCLK?\r")); break;
    case TEMP: gsm_send_p(PSTR("AT+CMTE?\r")); break;
    default: break;
    }
}

static void restart(uint32_t now)
{
    services_reset(now);
    gsm_status.modem_ready = 0;
    gsm_status.ready = 0;
    clear_readings();
    command = SYNC;
    attempts = 0;
    phase = BOOT_WAIT;
    deadline = now + 5000;
}

void gsm_reconnect(uint32_t now) { restart(now); }

/* SIM failures invalidate network/SMS state, never local modem readings. */
static void sim_unavailable(int8_t state, uint32_t now)
{
    if (gsm_status.ready || gsm_status.sim_ready == 1) services_reset(now);
    gsm_status.ready = 0;
    gsm_status.sim_ready = state;
    invalidate(REG);
    invalidate(SIGNAL);
    invalidate(OPERATOR);
    attempts = 0;
    phase = IDLE;
    command = BATTERY;
    deadline = now + 1000;
}

static void finish(uint8_t success, uint32_t now)
{
    services_yield();
    if (!success) {
        ++gsm_status.errors;
        invalidate(command);
        if (++attempts < 3) {
            phase = IDLE;
            deadline = now + 1000;
            return;
        }
        if (command == SYNC && !power_attempted) {
            /* One short startup pulse only; never endlessly power-cycle a live modem. */
            power_attempted = 1;
            gsm_power_key(1);
            phase = KEY_DOWN;
            deadline = now + 1200;
            return;
        }
        if (command <= FLOW) {
            restart(now);
            return;
        }
        if (command == PIN || command == TEXT || command == MEMORY || command == NOTIFY) {
            sim_unavailable(gsm_status.sim_ready, now);
            return;
        }
        /* An unsupported optional query must not prevent the remaining queries. */
    } else {
        switch (command) {
        case FLOW: gsm_status.modem_ready = 1; break;
        case PIN: gsm_status.sim_ready = values[0]; break;
        case REG: gsm_status.registration = values[0]; break;
        case SIGNAL: gsm_status.rssi = values[0]; gsm_status.ber = values[1]; break;
        case BATTERY:
            gsm_status.battery_percent = values[1];
            gsm_status.battery_mv = values[2]; break;
        case OPERATOR: memcpy(gsm_status.carrier, operator_name, sizeof operator_name); break;
        case CLOCK: memcpy(gsm_status.clock, operator_name, sizeof operator_name); break;
        case TEMP: gsm_status.temperature = values[0]; break;
        case NOTIFY: gsm_status.ready = 1; break;
        default: break;
        }
    }
    attempts = 0;
    phase = IDLE;
    deadline = now + (gsm_status.modem_ready ? 1000 : 100);
    if (command == PIN) {
        if (gsm_status.sim_ready != 1) {
            sim_unavailable(0, now);
            return;
        }
        command = gsm_status.ready ? REG : TEXT;
    } else if (command == BATTERY && !gsm_status.ready) {
        command = CLOCK;
    } else if (command == TEMP) {
        command = PIN;
    } else {
        ++command;
    }
}

/* Strict bounded integer parser: no scanf overflow on a 16-bit AVR int. */
static uint8_t number(const char **cursor, int *result)
{
    const char *p = *cursor;
    int32_t n = 0;
    uint8_t negative = 0, digits = 0;
    while (*p == ' ') ++p;
    if (*p == '-') { negative = 1; ++p; }
    while (*p >= '0' && *p <= '9') {
        if (++digits > 5) return 0;
        n = n * 10 + (*p++ - '0');
    }
    if (!digits || n > 32767) return 0;
    while (*p == ' ') ++p;
    *result = negative ? -n : n;
    *cursor = p;
    return 1;
}

static uint8_t numbers(const char *p, int *v, uint8_t count)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (!number(&p, &v[i])) return 0;
        if (i + 1 < count && *p++ != ',') return 0;
    }
    return *p == '\0';
}

static void receive_line(uint32_t now)
{
    const char *p;
    int v[3];
    if (!strcmp_P(line, PSTR("RDY")) || !strcmp_P(line, PSTR("NORMAL POWER DOWN"))) {
        restart(now);
        return;
    }
    services_notice(line);
    if (phase != WAIT_REPLY) return;
    if (command == PIN && (!strcmp_P(line, PSTR("+CME ERROR: SIM not inserted")) ||
                           !strcmp_P(line, PSTR("+CME ERROR: 10")))) {
        /* Expected terminal response without a SIM, not a modem transport fault. */
        sim_unavailable(0, now);
        return;
    }
    if (!strcmp_P(line, PSTR("ERROR")) ||
        !strncmp_P(line, PSTR("+CME ERROR:"), 11) ||
        !strncmp_P(line, PSTR("+CMS ERROR:"), 11)) {
        finish(0, now);
        return;
    }
    if (!strcmp_P(line, PSTR("OK"))) {
        uint8_t needs_payload = command == PIN || command == MEMORY || command >= REG;
        finish(!needs_payload || payload, now);
        return;
    }
    if (command == MEMORY && !strncmp_P(line, PSTR("+CPMS:"), 6)) {
        payload = services_storage(line);
        return;
    }
    /* Only the active command's payload can update its cached result. */
    if (command == PIN && !strncmp_P(line, PSTR("+CPIN:"), 6)) {
        p = line + 6;
        while (*p == ' ') ++p;
        if (*p) { values[0] = !strcmp_P(p, PSTR("READY")); payload = 1; }
    } else if (command == REG && !strncmp_P(line, PSTR("+CREG:"), 6)) {
        p = line + 6;
        /* Query response has n,stat; a one-field registration URC isn't its reply. */
        if (number(&p, &v[0]) && *p++ == ',' && number(&p, &v[1]) &&
            (*p == '\0' || *p == ',') && v[0] >= 0 && v[0] <= 2 &&
            v[1] >= 0 && v[1] <= 5) {
            values[0] = v[1]; payload = 1;
        }
    } else if (command == SIGNAL && !strncmp_P(line, PSTR("+CSQ:"), 5)) {
        if (numbers(line + 5, v, 2) && ((v[0] >= 0 && v[0] <= 31) || v[0] == 99) &&
            ((v[1] >= 0 && v[1] <= 7) || v[1] == 99)) {
            values[0] = v[0]; values[1] = v[1]; payload = 1;
        }
    } else if (command == BATTERY && !strncmp_P(line, PSTR("+CBC:"), 5)) {
        if (numbers(line + 5, v, 3) && v[0] >= 0 && v[0] <= 3 &&
            v[1] >= 0 && v[1] <= 100 && v[2] >= 0 && v[2] <= 6000) {
            memcpy(values, v, sizeof values); payload = 1;
        }
    } else if (command == CLOCK && !strncmp_P(line, PSTR("+CCLK:"), 6)) {
        p = strchr(line, '"');
        if (p && strlen(p) == 22 && p[21] == '"') {
            memcpy(operator_name, p + 1, 20); operator_name[20] = 0; payload = 1;
        }
    } else if (command == TEMP && !strncmp_P(line, PSTR("+CMTE:"), 6)) {
        /* SIM300D: temperature. Some related firmware: status,temperature. */
        if (numbers(line + 6, v, 1)) {
            values[0] = v[0];
            payload = v[0] >= -40 && v[0] <= 90;
        } else if (numbers(line + 6, v, 2) && v[0] >= 0 && v[0] <= 2) {
            values[0] = v[1];
            payload = v[1] >= -40 && v[1] <= 90;
        }
    } else if (command == OPERATOR && !strncmp_P(line, PSTR("+COPS:"), 6)) {
        p = line + 6;
        operator_name[0] = '\0';
        if (!number(&p, &v[0]) || v[0] < 0 || v[0] > 4) return;
        if (*p == '\0') { payload = 1; return; } /* no operator selected */
        if (*p++ != ',' || !number(&p, &v[1]) || v[1] < 0 || v[1] > 2 || *p++ != ',') return;
        while (*p == ' ') ++p;
        if (*p++ != '"') return;
        const char *end = strchr(p, '"');
        if (!end || end[1] != '\0') return;
        size_t n = (size_t)(end - p);
        if (n > 20) n = 20;
        memcpy(operator_name, p, n);
        operator_name[n] = '\0';
        payload = 1;
    }
}

void gsm_rx_error(uint32_t now)
{
    ++gsm_status.rx_errors;
    services_error(now);
    length = 0;
    discarding = 1;
    if (phase == WAIT_REPLY) finish(0, now);
    last_rx = now;
}

void gsm_feed(uint8_t byte, uint32_t now)
{
    last_rx = now;
    if (services_feed(byte, now)) return;
    if (byte == '\n') {
        if (!discarding && length) {
            line[length] = '\0';
            receive_line(now);
        }
        length = 0;
        discarding = 0;
    } else if (byte != '\r') {
        if (discarding) return;
        if (byte == 0 || length >= sizeof line - 1) { gsm_rx_error(now); return; }
        line[length++] = byte;
    }
}

void gsm_poll(uint32_t now)
{
    if (phase == IDLE && gsm_status.ready && !attempts &&
        services_poll(now, time_due(now, deadline) && time_due(now, last_rx + 100))) {
        length = 0;
        return;
    }
    if (phase == WAIT_REPLY) {
        if (time_due(now, deadline)) {
            ++gsm_status.timeouts;
            length = 0;
            discarding = 1;
            finish(0, now);
        }
        return;
    }
    if (!time_due(now, deadline)) return;
    if (phase == KEY_DOWN) {
        gsm_power_key(0);
        restart(now);
        return;
    }
    /* Drain late replies and require a quiet gap before the next transaction. */
    if (!time_due(now, last_rx + 100)) return;
    if (length || discarding) { length = 0; discarding = 0; }
    payload = 0;
    phase = WAIT_REPLY;
    deadline = now + 5000;
    send_command();
}
