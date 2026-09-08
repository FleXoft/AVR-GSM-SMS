#ifndef SENSORS_H
#define SENSORS_H
#include <stdint.h>
#define TEMP_UNKNOWN INT16_MIN
extern int16_t sensor_half_c[2];
void sensors_init(void);
void sensors_poll(uint32_t now);
void sensor_format(char out[8], int16_t half_c);
#endif
