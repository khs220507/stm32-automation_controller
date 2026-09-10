#include "w5500.h"
#include <assert.h>

spi2_result_t host_w5500_result = SPI2_RESULT_OK;
uint8_t host_w5500_version = 4U;
unsigned host_w5500_calls;

spi2_result_t w5500_read_version(uint8_t *version, uint32_t timeout_us)
{
    assert(timeout_us == 100000U);
    ++host_w5500_calls;
    if (host_w5500_result == SPI2_RESULT_OK) *version = host_w5500_version;
    return host_w5500_result;
}
