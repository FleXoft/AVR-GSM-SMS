#include "ui.h"
#include "timing.h"
#include "lcd_effects.h"
#include <string.h>

static char frame[40];
static const unsigned char *matrix;
static uint8_t cell, reveal;
static uint32_t next_step;

void ui_start(const char text[40], uint8_t effect, uint32_t now)
{
    memcpy(frame, text, sizeof frame);
    switch (effect % 5) {
    case 0: matrix = rand_matrix1; break;
    case 1: matrix = rand_matrix2; break;
    case 2: matrix = rand_matrix3; break;
    case 3: matrix = rand_matrix4; break;
    default: matrix = rand_matrix5; break;
    }
    cell = reveal = 0;
    next_step = now;
}

void ui_poll(uint32_t now)
{
    if (!matrix || cell == 40 || !time_due(now, next_step)) return;
    uint8_t y = pgm_read_byte(matrix + cell * 2);
    uint8_t x = pgm_read_byte(matrix + cell * 2 + 1);
    ui_write(x, y, reveal ? (uint8_t)frame[y * 20 + x] : 255);
    next_step = now + (reveal ? 5 : 30);
    if (reveal) ++cell;
    reveal = !reveal;
}

uint8_t button_update(uint8_t pressed, uint32_t now)
{
    static uint8_t candidate, stable;
    static uint32_t changed_at;
    pressed = !!pressed;
    if (pressed != candidate) {
        candidate = pressed;
        changed_at = now;
    }
    if (stable != candidate && time_due(now, changed_at + 30)) {
        stable = candidate;
        return stable;  /* One press event; hold and release do not advance. */
    }
    return 0;
}
