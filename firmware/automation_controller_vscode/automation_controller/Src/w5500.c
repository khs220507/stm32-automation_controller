#include "w5500.h"
#include "timebase.h"
#include "stm32f401xe.h"
#include <stddef.h>
#include <string.h>

static bool w5500_frame_failed;

bool w5500_ready(void)
{
    return (GPIOB->IDR & (0x1U << 1)) != 0U;
}

static spi2_result_t transfer(uint8_t block, uint16_t address, const uint8_t *tx,
                              uint8_t *rx, uint16_t size, uint32_t timeout_us)
{
    uint8_t incoming[256];
    uint8_t ignored;
    uint8_t header[] = {(uint8_t)(address >> 8), (uint8_t)address,
        (uint8_t)((block << 3) | (tx != NULL ? (0x1U << 2) : 0U))};
    spi2_result_t result = SPI2_RESULT_OK;
    if (block > 3U || size == 0U || size > sizeof(incoming) ||
        (tx == NULL && rx == NULL) || timeout_us == 0U || timeout_us > 0x7FFFFFFFU)
        return SPI2_RESULT_INVALID_ARGUMENT;
    if (w5500_frame_failed) return SPI2_RESULT_NOT_READY;
    uint32_t start = timebase_now_us();
    GPIOB->BSRR = (0x1U << 12);
    while (!w5500_ready())
        if (timebase_elapsed_us(start) >= timeout_us) return SPI2_RESULT_TIMEOUT;
    if (timebase_elapsed_us(start) >= timeout_us) return SPI2_RESULT_TIMEOUT;
    GPIOB->BSRR = (0x1U << 28);
    for (uint16_t i = 0U; i < size + 3U; ++i)
    {
        uint32_t elapsed = timebase_elapsed_us(start);
        if (elapsed >= timeout_us) { result = SPI2_RESULT_TIMEOUT; goto finished; }
        uint8_t outgoing = i < 3U ? header[i] : (tx != NULL ? tx[i - 3U] : 0U);
        uint8_t *received = i < 3U ? &ignored : &incoming[i - 3U];
        result = spi2_transfer_byte(outgoing, received, timeout_us - elapsed);
        if (result != SPI2_RESULT_OK) goto finished;
    }
    if (timebase_elapsed_us(start) >= timeout_us) result = SPI2_RESULT_TIMEOUT;
finished:
    GPIOB->BSRR = (0x1U << 12);
    if (result == SPI2_RESULT_OK && rx != NULL) memcpy(rx, incoming, size);
    if (result != SPI2_RESULT_OK) w5500_frame_failed = true;
    return result;
}

spi2_result_t w5500_read(uint8_t block, uint16_t address, uint8_t *data, uint16_t size, uint32_t timeout_us)
{
    return transfer(block, address, NULL, data, size, timeout_us);
}

spi2_result_t w5500_write(uint8_t block, uint16_t address, const uint8_t *data, uint16_t size, uint32_t timeout_us)
{
    return transfer(block, address, data, NULL, size, timeout_us);
}

spi2_result_t w5500_read_version(uint8_t *version, uint32_t timeout_us)
{
    return w5500_read(0U, 0x0039U, version, 1U, timeout_us);
}