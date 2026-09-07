#ifndef HCSR04_H
#define HCSR04_H

#include <stdint.h>

typedef enum
{
    HCSR04_STATUS_OK = 0,
    HCSR04_STATUS_OUT_OF_RANGE,
    HCSR04_STATUS_TIMEOUT
} hcsr04_status_t;

typedef struct
{
    hcsr04_status_t status;
    uint32_t distance_cm;
    uint32_t pulse_us;
} hcsr04_measurement_t;

void hcsr04_init(void);
void hcsr04_measure(hcsr04_measurement_t *measurement);

#endif
