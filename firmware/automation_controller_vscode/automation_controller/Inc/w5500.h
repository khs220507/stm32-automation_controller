#ifndef W5500_DRIVER_H
#define W5500_DRIVER_H

#include "spi2.h"

spi2_result_t w5500_read_version(uint8_t *version, uint32_t timeout_us);

#endif
