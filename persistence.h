#ifndef PERSISTENCE_H
#define PERSISTENCE_H
#include <stdint.h>
#include "sms_pdu.h"
typedef struct {
    uint16_t resets, sent, received, calls;
    char emergency[SMS_PHONE_SIZE];
} persistent_state_t;
extern persistent_state_t settings;
void persistence_init(void);
void persistence_dirty(void);
void persistence_poll(void);
#endif
