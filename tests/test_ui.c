#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned writes;
static char display[40];
static unsigned seen[40];
void ui_write(uint8_t x, uint8_t y, uint8_t c)
{
    assert(x < 20 && y < 2);
    ++writes;
    if (c != 255) ++seen[y * 20 + x];
    display[y * 20 + x] = (char)c;
}
int main(void)
{
    char frame[40];
    for (unsigned i = 0; i < 40; ++i) frame[i] = 'A' + i % 26;
    for (unsigned effect = 0; effect < 5; ++effect) {
        writes = 0;
        memset(seen, 0, sizeof seen);
        uint32_t start = UINT32_MAX - 500;
        ui_start(frame, effect, start);
        for (unsigned elapsed = 0; elapsed <= 1500; elapsed += 5)
            ui_poll(start + elapsed);
        assert(writes == 80 && !memcmp(frame, display, 40));
        for (unsigned i = 0; i < 40; ++i) assert(seen[i] == 1);
    }
    /* Starting another page mid-effect replaces the old frame immediately. */
    ui_start(frame, 0, 0); ui_poll(0);
    memset(frame, 'X', sizeof frame);
    ui_start(frame, 3, 10);
    for (unsigned ms = 10; ms < 1600; ms += 5) ui_poll(ms);
    assert(!memcmp(frame, display, 40));
    assert(!button_update(1, 10));
    assert(!button_update(0, 20));
    assert(!button_update(1, 25));
    assert(!button_update(1, 50));
    assert(button_update(1, 55));
    assert(!button_update(1, 1000));
    assert(!button_update(0, 1010));
    assert(!button_update(0, 1040));
    assert(!button_update(1, UINT32_MAX - 10));
    assert(button_update(1, 20));
    puts("UI: five complete original effects, mid-animation page change, debounce, wrap: PASS");
}
