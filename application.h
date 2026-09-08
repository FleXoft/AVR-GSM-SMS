#ifndef APPLICATION_H
#define APPLICATION_H
#include <stdint.h>
void application_init(void);
void application_poll(uint32_t now, uint8_t movement, uint8_t power);
void app_set_relay(uint8_t index, uint8_t on);
void app_beep(uint16_t ms);
extern uint8_t movement_armed, power_armed, relays, input_movement, input_power;
extern uint8_t last_sms_result, call_connected;
extern char last_sms_number[17];
#endif
