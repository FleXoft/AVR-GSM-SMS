#include "runtime.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

static volatile uint32_t ticks_ms;
static volatile uint16_t beep_ticks;

/* 5 ms exactly at 7.3728 MHz / 64: 576 timer counts. */
#if (F_CPU % 12800UL) != 0 || (F_CPU / 12800UL) > 65536UL
#error "Timer1 requires F_CPU divisible by 12800 and a 16-bit compare value"
#endif

void runtime_init(void)
{
    PORTD &= ~_BV(PD4);
    DDRD |= _BV(PD4);
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = F_CPU / 12800UL - 1;
    TIFR = _BV(OCF1A) | _BV(OCF2);
    TIMSK |= _BV(OCIE1A);
    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);
    /* Timer2 drives the passive buzzer via PD4, approximately 2 kHz. */
    TCCR2 = _BV(WGM21);
    OCR2 = F_CPU / 8UL / 4000UL - 1;
}

uint32_t runtime_millis(void)
{
    uint32_t now;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { now = ticks_ms; }
    return now;
}

void buzzer_beep(uint16_t duration_ms)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        beep_ticks = duration_ms / 5 + (duration_ms % 5 != 0);
        if (beep_ticks) {
            TCNT2 = 0;
            TIFR = _BV(OCF2);
            TIMSK |= _BV(OCIE2);
            TCCR2 = _BV(WGM21) | _BV(CS21);
        } else {
            TIMSK &= ~_BV(OCIE2);
            TCCR2 = _BV(WGM21);
            PORTD &= ~_BV(PD4);
        }
    }
}

ISR(TIMER1_COMPA_vect)
{
    ticks_ms += 5;
    if (beep_ticks && --beep_ticks == 0) {
        TIMSK &= ~_BV(OCIE2);
        TCCR2 = _BV(WGM21);
        PORTD &= ~_BV(PD4);
    }
}

ISR(TIMER2_COMP_vect)
{
    PORTD ^= _BV(PD4);
}
