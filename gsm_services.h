#ifndef GSM_SERVICES_H
#define GSM_SERVICES_H
#include <stdint.h>
#include "sms_pdu.h"
void services_init(char *scratch, uint32_t now);
uint8_t services_poll(uint32_t now, uint8_t allow_start);
uint8_t services_feed(uint8_t byte, uint32_t now);
void services_error(uint32_t now);
void services_reset(uint32_t now);
void services_yield(void);
void services_notice(const char *line);
uint8_t services_storage(const char *line);
uint8_t gsm_sms_available(void);
uint8_t gsm_queue_sms(const char *number, const char *text);
/* Application hooks. Return false to defer a received SMS without deleting it. */
uint8_t app_sms_received(const char *number, const char *text);
void app_sms_sent(uint8_t result); /* 0 failure, 1 accepted by SMSC, 2 uncertain */
void app_call_answered(void);
void app_call_ended(void);
void app_ring(void);
void app_power_restored(void);
/* Nonblocking UART byte enqueue; zero if TX ring is full. */
uint8_t gsm_try_putc(uint8_t byte);
void gsm_send(const char *text);
void gsm_reconnect(uint32_t now);
extern uint16_t sms_rejected, sms_failed, sms_uncertain;
#endif
