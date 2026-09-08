#ifndef UI_H
#define UI_H
#include <stdint.h>
void ui_start(const char text[40], uint8_t effect, uint32_t now);
void ui_poll(uint32_t now);
uint8_t button_update(uint8_t pressed, uint32_t now);
/* Platform hook, one LCD cell. */
void ui_write(uint8_t x, uint8_t y, uint8_t character);
#endif
