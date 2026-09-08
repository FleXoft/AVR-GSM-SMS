#ifndef RUNTIME_H
#define RUNTIME_H
#include <stdint.h>
void runtime_init(void);
uint32_t runtime_millis(void);
void buzzer_beep(uint16_t duration_ms);
#endif
