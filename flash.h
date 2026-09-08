#ifndef FLASH_H
#define FLASH_H
#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#include <string.h>
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(p) (*(const unsigned char *)(p))
#define strcpy_P strcpy
#define snprintf_P snprintf
#define strcmp_P strcmp
#define strncmp_P strncmp
#endif
#endif
