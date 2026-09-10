#ifndef W5500_DRIVER_H
#define W5500_DRIVER_H

#include "spi2.h"
#include <stdbool.h>

spi2_result_t w5500_read_version(uint8_t *version, uint32_t timeout_us);
bool w5500_ready(void);
spi2_result_t w5500_read(uint8_t block, uint16_t address, uint8_t *data, uint16_t size, uint32_t timeout_us);
spi2_result_t w5500_write(uint8_t block, uint16_t address, const uint8_t *data, uint16_t size, uint32_t timeout_us);

#endif
