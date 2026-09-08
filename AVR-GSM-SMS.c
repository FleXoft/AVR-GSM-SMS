/* Cooperative home automation firmware for Olimex AVR-GSM / SIM300D.
 * Original application, including alarm/SMS actions: legacy/AVR-GSM-SMS.c.
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <string.h>
#include "lcd.h"
#include "uart.h"
#include "gsm.h"
#include "gsm_services.h"
#include "application.h"
#include "persistence.h"
#include "sensors.h"
#include "runtime.h"
#include "ui.h"
#include "timing.h"
#include "version.h"

#ifndef UART_BAUD_RATE
#define UART_BAUD_RATE 57600UL
#endif

void gsm_send_p(const char *command)
{
    uart_puts_p(command);
}

void gsm_send(const char *text) { uart_puts(text); }
uint8_t gsm_try_putc(uint8_t byte) { return uart0_try_putc(byte); }
void app_beep(uint16_t ms) { buzzer_beep(ms); }
void app_set_relay(uint8_t index, uint8_t on)
{
    uint8_t mask = index ? _BV(PC6) : _BV(PC7);
    if (on) PORTC |= mask; else PORTC &= ~mask;
}

void gsm_power_key(uint8_t pressed)
{
    PORTA &= ~_BV(PA2);       /* Open drain: never drive the modem pin high. */
    if (pressed) DDRA |= _BV(PA2);
    else DDRA &= ~_BV(PA2);
}

void ui_write(uint8_t x, uint8_t y, uint8_t character)
{
    lcd_gotoxy(x, y);
    lcd_putc(character);
}

static void show_page(uint8_t page, uint32_t now)
{
    char top[21] = "", bottom[21] = "", frame[40];
    switch (page) {
    case 0:
        snprintf_P(top, sizeof top, PSTR("GSM: %s"), gsm_status.ready ? "Ready" : gsm_status.modem_ready ? "SIM/SMS waiting" : "Initializing");
        snprintf_P(bottom, sizeof bottom, PSTR("SIM:%d Network:%d"),
                   (int)gsm_status.sim_ready, (int)gsm_status.registration);
        break;
    case 1:
        snprintf_P(top, sizeof top, PSTR("Mobile carrier:"));
        snprintf_P(bottom, sizeof bottom, PSTR("%s"),
                   gsm_status.carrier[0] ? gsm_status.carrier : "n/a");
        break;
    case 2:
        snprintf_P(top, sizeof top, PSTR("Signal strength:"));
        if (gsm_status.rssi == 99)
            snprintf_P(bottom, sizeof bottom, PSTR("RSSI: n/a BER:%d"), (int)gsm_status.ber);
        else
            snprintf_P(bottom, sizeof bottom, PSTR("%d dBm BER:%d"),
                       -113 + 2 * gsm_status.rssi, (int)gsm_status.ber);
        break;
    case 3:
        snprintf_P(top, sizeof top, PSTR("Battery status:"));
        if (gsm_status.battery_percent < 0)
            snprintf_P(bottom, sizeof bottom, PSTR("n/a"));
        else
            snprintf_P(bottom, sizeof bottom, PSTR("%d%%  %d mV"),
                       gsm_status.battery_percent, gsm_status.battery_mv);
        break;
    case 4:
        snprintf_P(top, sizeof top, PSTR("GSM temperature:"));
        if (gsm_status.temperature == -1000)
            snprintf_P(bottom, sizeof bottom, PSTR("n/a"));
        else
            snprintf_P(bottom, sizeof bottom, PSTR("%d%cC"), gsm_status.temperature, 223);
        break;
    case 5:
        snprintf_P(top, sizeof top, PSTR("Inputs: 1=%d 2=%d"),
                   !(PINB & _BV(PB0)), !(PIND & _BV(PD2)));
        snprintf_P(bottom, sizeof bottom, PSTR("Relays: 1=%d 2=%d"),
                   !!(PORTC & _BV(PC7)), !!(PORTC & _BV(PC6)));
        break;
    case 6:
        snprintf_P(top, sizeof top, PSTR("SMS notices: %u"), gsm_status.sms_notices);
        snprintf_P(bottom, sizeof bottom, PSTR("SIM index:%d"), gsm_status.sms_index);
        break;
    case 7:
        snprintf_P(top, sizeof top, PSTR("AT err:%u t/o:%u"), gsm_status.errors, gsm_status.timeouts);
        snprintf_P(bottom, sizeof bottom, PSTR("RX errors: %u"), gsm_status.rx_errors);
        break;
    case 8: {
        char t1[8], t2[8];
        sensor_format(t1, sensor_half_c[0]); sensor_format(t2, sensor_half_c[1]);
        snprintf_P(top, sizeof top, PSTR("1:R%u %s"), !!(relays & 1), t1);
        snprintf_P(bottom, sizeof bottom, PSTR("2:R%u %s"), !!(relays & 2), t2);
        break;
    }
    case 9:
        snprintf_P(top, sizeof top, PSTR("SMS s:%u r:%u"), settings.sent, settings.received);
        snprintf_P(bottom, sizeof bottom, PSTR("Call:%u Boot:%u"), settings.calls, settings.resets);
        break;
    case 10:
        snprintf_P(top, sizeof top, PSTR("Alert destination:"));
        snprintf_P(bottom, sizeof bottom, PSTR("%s"), settings.emergency);
        break;
    case 11:
        snprintf_P(top, sizeof top, PSTR("Movement armed:%u"), movement_armed);
        snprintf_P(bottom, sizeof bottom, PSTR("Power watch:%u"), power_armed);
        break;
    case 12:
        snprintf_P(top, sizeof top, PSTR("SMS result:%u"), last_sms_result);
        snprintf_P(bottom, sizeof bottom, PSTR("%s"), last_sms_number);
        break;
    case 13:
        snprintf_P(top, sizeof top, PSTR("Modem clock:"));
        snprintf_P(bottom, sizeof bottom, PSTR("%s"), gsm_status.clock[0] ? gsm_status.clock : "n/a");
        break;
    case 14:
        snprintf_P(top, sizeof top, PSTR("Home Automation by"));
        snprintf_P(bottom, sizeof bottom, PSTR("_flex from FleXoft."));
        break;
    default:
        snprintf_P(top, sizeof top, PSTR("AVR-GSM " FIRMWARE_VERSION));
        snprintf_P(bottom, sizeof bottom, PSTR("B1: next screen"));
        break;
    }
    memset(frame, ' ', sizeof frame);
    memcpy(frame, top, strlen(top));
    memcpy(frame + 20, bottom, strlen(bottom));
    ui_start(frame, page % 5, now);
}

int main(void)
{
    /* Relay outputs start off; valid SMS commands may change them. */
    PORTC &= ~(_BV(PC7) | _BV(PC6));
    DDRC |= _BV(PC7) | _BV(PC6);
    DDRD &= ~(_BV(PD3) | _BV(PD2));
    PORTD |= _BV(PD3) | _BV(PD2) | _BV(PD7);
    DDRD |= _BV(PD7);
    DDRB &= ~_BV(PB0);
    PORTB |= _BV(PB0);
    PORTB &= ~_BV(PB2);       /* Hold FT232 in reset: AVR owns modem UART. */
    DDRB |= _BV(PB2);
    PORTA &= ~(_BV(PA5) | _BV(PA7));
    DDRA |= _BV(PA5) | _BV(PA7);  /* RTS and DTR low. */

    lcd_init(LCD_DISP_ON);
    uart_init(UART_BAUD_SELECT(UART_BAUD_RATE, F_CPU));
    runtime_init();
    gsm_init(0);
    persistence_init();
    sensors_init();
    application_init();
    sei();
    buzzer_beep(80);

    uint8_t page = 0;
    uint32_t next_page = 5000;
    show_page(page, 0);
    for (;;) {
        uint32_t now = runtime_millis();
        /* Bound work even under continuous noise; normally drains the entire ring. */
        for (uint16_t n = 0; n < UART_RX0_BUFFER_SIZE; ++n) {
            uint16_t c = uart_getc();
            if (c & (UART_FRAME_ERROR | UART_OVERRUN_ERROR | UART_BUFFER_OVERFLOW))
                gsm_rx_error(now);
            if (c & UART_NO_DATA) break;
            gsm_feed((uint8_t)c, now);
        }
        gsm_poll(now);
        sensors_poll(now);
        application_poll(now, !(PINB & _BV(PB0)), !(PIND & _BV(PD2)));
        persistence_poll();
        uint8_t pressed = button_update(!(PIND & _BV(PD3)), now);
        if (pressed || time_due(now, next_page)) {
            page = (page + 1) % 16;
            show_page(page, now);
            next_page = now + 5000;
            if (pressed) buzzer_beep(50);
        }
        ui_poll(now);
    }
}
