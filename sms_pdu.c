#include "sms_pdu.h"
#include <string.h>

uint8_t sms_phone_valid(const char *number)
{
    if (*number == '+') ++number;
    size_t n = strlen(number);
    if (n < 3 || n > 15) return 0;
    while (*number) if (*number < '0' || *number++ > '9') return 0;
    return 1;
}
static int to_gsm(uint8_t c)
{
    if (c == '@') return 0;
    if (c == '$') return 2;
    if (c == '_') return 17;
    if (c == '\n' || c == '\r') return c;
    if ((c >= 32 && c <= 63) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return c;
    return -1;
}
static int from_gsm(uint8_t c)
{
    if (c == 0) return '@';
    if (c == 2) return '$';
    if (c == 17) return '_';
    if (c == 36) return -1; /* currency sign, not ASCII dollar */
    if (c == '\n' || c == '\r' || (c >= 32 && c <= 63) ||
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return c;
    return -1;
}
uint16_t sms_encode(uint8_t out[SMS_PDU_SIZE], const char *number, const char *text)
{
    if (!sms_phone_valid(number)) return 0;
    size_t n = strlen(text);
    if (!n || n > SMS_MAX_TEXT) return 0;
    for (size_t i = 0; i < n; ++i) if (to_gsm(text[i]) < 0) return 0;
    memset(out, 0, SMS_PDU_SIZE);
    uint8_t international = *number == '+';
    if (international) ++number;
    uint8_t digits = strlen(number), pos = 0;
    out[pos++] = 0; /* use SIM's SMSC */
    out[pos++] = 1; /* SMS-SUBMIT, no validity field */
    out[pos++] = 0; /* modem assigns reference */
    out[pos++] = digits;
    out[pos++] = international ? 0x91 : 0x81;
    for (uint8_t i = 0; i < digits; i += 2)
        out[pos++] = (number[i] - '0') | ((i + 1 < digits ? number[i + 1] - '0' : 15) << 4);
    out[pos++] = 0; /* PID */
    out[pos++] = 0; /* GSM 7-bit */
    out[pos++] = n;
    for (uint16_t i = 0; i < n; ++i) {
        uint16_t bit = i * 7;
        uint8_t c = to_gsm(text[i]);
        out[pos + bit / 8] |= c << (bit % 8);
        if (bit % 8 > 1) out[pos + bit / 8 + 1] |= c >> (8 - bit % 8);
    }
    return pos + (n * 7 + 7) / 8;
}
uint8_t sms_decode(const uint8_t *p, uint16_t size, char number[SMS_PHONE_SIZE], char text[SMS_MAX_TEXT + 1])
{
    if (size < 2) return 0;
    uint16_t pos = 1 + p[0];
    if (pos + 3 > size || (p[pos] & 0x43)) return 0; /* DELIVER, no UDH */
    ++pos;
    uint8_t digits = p[pos++], toa = p[pos++];
    if (digits < 3 || digits > 15 || (toa != 0x91 && toa != 0x81)) return 0;
    uint8_t bytes = (digits + 1) / 2, out = 0;
    if (pos + bytes + 10 > size) return 0;
    if (toa == 0x91) number[out++] = '+';
    for (uint8_t i = 0; i < digits; ++i) {
        uint8_t d = (p[pos + i / 2] >> ((i % 2) * 4)) & 15;
        if (d > 9) return 0;
        number[out++] = '0' + d;
    }
    if ((digits & 1) && (p[pos + bytes - 1] >> 4) != 15) return 0;
    number[out] = 0;
    pos += bytes;
    if (p[pos++] != 0) return 0; /* ordinary short message PID only */
    uint8_t dcs = p[pos++];
    if (dcs != 0 && (dcs & 0xfc) != 0x10) return 0;
    pos += 7; /* timestamp */
    uint8_t n = p[pos++];
    if (!n || n > SMS_MAX_TEXT || pos + (n * 7 + 7) / 8 != size) return 0;
    for (uint16_t i = 0; i < n; ++i) {
        uint16_t bit = i * 7;
        uint16_t word = p[pos + bit / 8];
        if (bit % 8 > 1) word |= (uint16_t)p[pos + bit / 8 + 1] << 8;
        int c = from_gsm((word >> (bit % 8)) & 127);
        if (c < 0) return 0;
        text[i] = c;
    }
    text[n] = 0;
    return 1;
}
