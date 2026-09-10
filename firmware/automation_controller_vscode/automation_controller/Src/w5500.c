#include "w5500.h"
#include "timebase.h"
#include "stm32f401xe.h"

#include <stdbool.h>
#include <stddef.h>

static bool w5500_frame_failed;

spi2_result_t w5500_read_version(uint8_t *version, uint32_t timeout_us)
{
    const uint8_t frame[] = {0x00U, 0x39U, 0x00U, 0x00U};
    uint8_t received = 0U;
    spi2_result_t result = SPI2_RESULT_OK;

    if ((version == NULL) || (timeout_us == 0U) || (timeout_us > 0x7FFFFFFFU))
        return SPI2_RESULT_INVALID_ARGUMENT;
    if (w5500_frame_failed)
        return SPI2_RESULT_NOT_READY;

    uint32_t start = timebase_now_us();
    GPIOB->BSRR = (0x1U << 12);
    while ((GPIOB->IDR & (0x1U << 1)) == 0U)
    {
        if (timebase_elapsed_us(start) >= timeout_us)
            return SPI2_RESULT_TIMEOUT;
    }
    if (timebase_elapsed_us(start) >= timeout_us)
        return SPI2_RESULT_TIMEOUT;

    GPIOB->BSRR = (0x1U << 28);
    for (uint32_t i = 0U; i < sizeof(frame); ++i)
    {
        uint32_t elapsed = timebase_elapsed_us(start);
        if (elapsed >= timeout_us)
        {
            result = SPI2_RESULT_TIMEOUT;
            goto finished;
        }
        result = spi2_transfer_byte(frame[i], &received, timeout_us - elapsed);
        if (result != SPI2_RESULT_OK)
            goto finished;
    }
    if (timebase_elapsed_us(start) >= timeout_us)
        result = SPI2_RESULT_TIMEOUT;

finished:
    GPIOB->BSRR = (0x1U << 12);
    if (result == SPI2_RESULT_OK)
        *version = received;
    else
        w5500_frame_failed = true;
    return result;
}
