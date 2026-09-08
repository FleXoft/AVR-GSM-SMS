#ifndef FAKE_IO_H
#define FAKE_IO_H
#include <stdint.h>
#define _BV(n) (1U << (n))
extern uint8_t TWSR, TWBR, TWCR, TWDR;
#define TWINT 7
#define TWEA 6
#define TWSTA 5
#define TWSTO 4
#define TWEN 2
#endif
