#ifndef GSM_H
#define GSM_H
#include <stdint.h>

#define GSM_LINE_SIZE 192

typedef struct {
    uint8_t modem_ready;       /* basic AT link configured, independent of SIM */
    uint8_t ready;             /* AT initialization complete, not network registration */
    int8_t sim_ready;          /* -1 unknown, 0 PIN/PUK/other state, 1 READY */
    int8_t registration;       /* -1 unknown, 0..5 per CREG */
    int8_t rssi, ber;          /* raw CSQ; 99 means unknown */
    int16_t battery_percent, battery_mv, temperature;
    char carrier[21];
    char clock[21];
    uint16_t timeouts, errors, rx_errors, sms_notices;
    int16_t sms_index;         /* last CMTI index; messages remain on the modem */
} gsm_status_t;

extern gsm_status_t gsm_status;
void gsm_init(uint32_t now);
void gsm_poll(uint32_t now);
void gsm_feed(uint8_t byte, uint32_t now);
void gsm_rx_error(uint32_t now);

/* Platform hooks: commands are NUL-terminated strings in flash on AVR. */
void gsm_send_p(const char *command);
void gsm_power_key(uint8_t pressed);
#endif
