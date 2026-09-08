#ifndef TIMING_H
#define TIMING_H
#include <stdint.h>
/* Valid for deadlines less than 2^31 ms away, including counter wrap. */
static inline uint8_t time_due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}
#endif
