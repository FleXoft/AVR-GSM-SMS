#ifndef SMS_PDU_H
#define SMS_PDU_H
#include <stdint.h>
#define SMS_MAX_TEXT 160
#define SMS_PHONE_SIZE 17
#define SMS_PDU_SIZE 180
uint8_t sms_phone_valid(const char *number);
/* Single-part GSM-7 ASCII command subset. Unsupported encodings are rejected. */
uint16_t sms_encode(uint8_t out[SMS_PDU_SIZE], const char *number, const char *text);
uint8_t sms_decode(const uint8_t *pdu, uint16_t size, char number[SMS_PHONE_SIZE], char text[SMS_MAX_TEXT + 1]);
#endif
