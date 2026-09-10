#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stm32f401xe.h"
#include "w5500.h"

host_gpio_t host_gpiob;
static uint32_t now, tick, byte_cost, calls, fail_at, ready_at;
static uint32_t budgets[4];
static uint8_t reply;
static spi2_result_t injected;

uint32_t timebase_now_us(void) { return now; }
uint32_t timebase_elapsed_us(uint32_t start)
{
    now += tick;
    if (ready_at != 0U && now >= ready_at) host_gpiob.IDR = 2U;
    return now - start;
}

spi2_result_t spi2_transfer_byte(uint8_t tx, uint8_t *rx, uint32_t timeout_us)
{
    const uint8_t expected[] = {0x00U, 0x39U, 0x00U, 0x00U};
    assert(calls < 4U);
    /* BSRR 쓰기만 추적하며 실제 핀 전압은 모의하지 않는다. */
    assert(host_gpiob.BSRR == (1U << 28));
    assert(tx == expected[calls]);
    assert(timeout_us > 0U);
    budgets[calls++] = timeout_us;
    now += byte_cost;
    if (calls == fail_at) return injected;
    *rx = calls == 4U ? reply : 0xEEU;
    return SPI2_RESULT_OK;
}

#include "../Src/w5500.c"

static void reset(void)
{
    memset(&host_gpiob, 0, sizeof(host_gpiob));
    host_gpiob.IDR = 2U;
    now = 0U; tick = 1U; byte_cost = 5U; calls = 0U;
    fail_at = 0U; ready_at = 0U; reply = 4U;
    injected = SPI2_RESULT_HARDWARE_ERROR;
    w5500_frame_failed = false;
}

int main(void)
{
    uint8_t value;
    reset(); value = 0xA5U;
    assert(w5500_read_version(&value, 100U) == SPI2_RESULT_OK);
    assert(value == 4U && calls == 4U && host_gpiob.BSRR == (1U << 12));
    for (unsigned i = 1; i < 4; ++i) assert(budgets[i] < budgets[i - 1]);
    calls = 0U; reply = 0xFFU;
    assert(w5500_read_version(&value, 100U) == SPI2_RESULT_OK);
    assert(value == 0xFFU); /* 전송 성공을 식별값 일치로 바꾸지 않는다. */

    reset(); now = UINT32_MAX - 10U;
    assert(w5500_read_version(&value, 100U) == SPI2_RESULT_OK);
    assert(value == 4U && now < 100U);

    reset(); host_gpiob.IDR = 0U; ready_at = 10U;
    assert(w5500_read_version(&value, 100U) == SPI2_RESULT_OK);
    assert(budgets[0] < 90U);

    reset(); host_gpiob.IDR = 0U; value = 0xA5U;
    assert(w5500_read_version(&value, 10U) == SPI2_RESULT_TIMEOUT);
    assert(calls == 0U && value == 0xA5U && host_gpiob.BSRR == (1U << 12));
    host_gpiob.IDR = 2U;
    assert(w5500_read_version(&value, 100U) == SPI2_RESULT_OK);

    for (unsigned failure = 1U; failure <= 4U; ++failure)
    {
        reset(); fail_at = failure; value = 0xA5U;
        injected = failure == 2U ? SPI2_RESULT_TIMEOUT : SPI2_RESULT_HARDWARE_ERROR;
        assert(w5500_read_version(&value, 100U) == injected);
        assert(calls == failure && value == 0xA5U);
        assert(host_gpiob.BSRR == (1U << 12));
        assert(w5500_read_version(&value, 100U) == SPI2_RESULT_NOT_READY);
        assert(calls == failure);
    }

    reset(); value = 0xA5U;
    assert(w5500_read_version(&value, 8U) == SPI2_RESULT_TIMEOUT);
    assert(calls == 1U && value == 0xA5U && w5500_frame_failed);
    assert(host_gpiob.BSRR == (1U << 12));

    reset(); value = 0xA5U;
    assert(w5500_read_version(&value, 25U) == SPI2_RESULT_TIMEOUT);
    assert(calls == 4U && value == 0xA5U && w5500_frame_failed);
    assert(host_gpiob.BSRR == (1U << 12));

    reset(); value = 0xA5U;
    assert(w5500_read_version(NULL, 100U) == SPI2_RESULT_INVALID_ARGUMENT);
    assert(w5500_read_version(&value, 0U) == SPI2_RESULT_INVALID_ARGUMENT);
    assert(w5500_read_version(&value, 0x80000000U) == SPI2_RESULT_INVALID_ARGUMENT);
    assert(value == 0xA5U && calls == 0U && host_gpiob.BSRR == 0U);
    puts("W5500 host tests passed (mock SPI/time/GPIO; no hardware validation).");
    return 0;
}
