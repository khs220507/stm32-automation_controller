#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stm32f401xe.h"

/* 내부 실패 래치는 시험마다 MCU 재부팅을 모사할 때만 초기화한다. */
#include "../Src/spi2.c"

host_spi_t host_spi;
host_rcc_t host_rcc;
host_gpio_t host_gpiob, host_gpioc;
static uint32_t now, ticks, error_tick, error_mask;
static unsigned scenario;
static uint8_t sent;

uint32_t timebase_now_us(void) { return now; }
uint32_t timebase_elapsed_us(uint32_t start)
{
    ++now;
    ++ticks;
    if (ticks == 2U && scenario != 1U)
    {
        sent = (uint8_t)SPI2->DR;
        if (scenario != 2U)
        {
            SPI2->DR = 0xA6U;
            SPI2->SR = (0x1U << 7) | (0x1U << 1) | (0x1U << 0);
        }
    }
    if (ticks == 4U && scenario != 2U)
        SPI2->SR &= ~(0x1U << 0);
    if (ticks == 5U && scenario != 3U)
        SPI2->SR &= ~(0x1U << 7);
    if (ticks == error_tick) SPI2->SR |= error_mask;
    return now - start;
}

static void reset(unsigned mode)
{
    memset(&host_spi, 0, sizeof host_spi);
    spi2_transfer_failed = false;
    spi2_configure();
    SPI2->SR = mode == 1U ? 0U : (0x1U << 1);
    now = ticks = error_tick = error_mask = 0U;
    scenario = mode;
    sent = 0U;
}

int main(void)
{
    uint8_t rx;
    for (unsigned wrap = 0; wrap < 2; ++wrap)
    {
        reset(0);
        if (wrap) now = 0xFFFFFFFCU;
        rx = 0x55U;
        assert(spi2_transfer_byte(0x3CU, &rx, 20U) == SPI2_RESULT_OK);
        assert(sent == 0x3CU && rx == 0xA6U && ticks >= 6U);
        assert((SPI2->SR & (0x1U << 7)) == 0U);
        /* 연속 호출: 실제 DR 읽기가 지우는 RXNE는 위 모형에서 지웠다. */
        ticks = 0;
        assert(spi2_transfer_byte(0x81U, &rx, 20U) == SPI2_RESULT_OK);
        assert(sent == 0x81U && rx == 0xA6U);
    }
    for (unsigned mode = 1; mode <= 3; ++mode)
    {
        reset(mode);
        rx = 0x55U;
        assert(spi2_transfer_byte(0x3CU, &rx, 10U) == SPI2_RESULT_TIMEOUT);
        assert(rx == 0x55U && ticks == 10U);
        SPI2->SR = (0x1U << 1) | (0x1U << 0);
        SPI2->DR = 0xEEU;
        assert(spi2_transfer_byte(0x3CU, &rx, 10U) == SPI2_RESULT_NOT_READY);
        assert(SPI2->DR == 0xEEU && rx == 0x55U);
    }
    reset(0);
    rx = 0x55U;
    /* 각 대기마다 제한시간을 새로 시작하지 않는지 확인한다. */
    assert(spi2_transfer_byte(0x3CU, &rx, 5U) == SPI2_RESULT_TIMEOUT);
    assert(rx == 0x55U);
    for (unsigned bit = 4; bit <= 8; ++bit)
    {
        if (bit == 7) continue;
        for (unsigned phase = 0; phase < 3; ++phase)
        {
            reset(0);
            rx = 0x55U;
            error_mask = 0x1U << bit;
            if (phase == 0) SPI2->SR |= error_mask;
            else error_tick = phase == 1 ? 2U : 4U;
            assert(spi2_transfer_byte(0x3CU, &rx, 20U) == SPI2_RESULT_HARDWARE_ERROR);
            assert(rx == 0x55U);
        }
    }
    for (unsigned bit = 0; bit <= 7; bit += 7)
    {
        reset(0);
        rx = 0x55U;
        SPI2->SR |= 0x1U << bit;
        assert(spi2_transfer_byte(0x3CU, &rx, 20U) == SPI2_RESULT_DIRTY_STATE);
        assert(SPI2->DR == 0U && rx == 0x55U);
    }
    reset(0);
    assert(spi2_transfer_byte(0, NULL, 20) == SPI2_RESULT_INVALID_ARGUMENT);
    assert(spi2_transfer_byte(0, &rx, 0) == SPI2_RESULT_INVALID_ARGUMENT);
    assert(spi2_transfer_byte(0, &rx, 0x80000000U) == SPI2_RESULT_INVALID_ARGUMENT);
    assert(ticks == 0U && !spi2_transfer_failed);
    for (unsigned bit = 2; bit <= 6; bit += 4)
    {
        reset(0);
        rx = 0x55U;
        SPI2->CR1 &= ~(0x1U << bit);
        assert(spi2_transfer_byte(0, &rx, 20) == SPI2_RESULT_NOT_READY);
        assert(rx == 0x55U && SPI2->DR == 0U);
    }
    puts("SPI2 host tests passed (register model, not hardware evidence)");
    return 0;
}
