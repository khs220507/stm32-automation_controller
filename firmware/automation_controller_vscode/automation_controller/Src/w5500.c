#include "w5500.h"
#include "timebase.h"
#include "stm32f401xe.h"

#include <stdbool.h>
#include <stddef.h>

static bool w5500_frame_failed;

spi2_result_t w5500_read_version(uint8_t *version, uint32_t timeout_us)
{
    /* W5500 v1.0.6, 2.2절 p.14~17, VERSIONR p.43:
     * 주소 0x0039(상위 바이트 먼저), 제어 0x00, 수신 클록용 더미 0x00.
     * 제어 bit 7:3 BSB=00000(공통 레지스터), bit 2 RWB=0(읽기),
     * bit 1:0 OM=00(VDM). 마지막 수신 바이트만 버전 값이다. */
    const uint8_t frame[] = {0x00U, 0x39U, 0x00U, 0x00U};
    uint8_t received = 0U;
    spi2_result_t result = SPI2_RESULT_OK;

    if ((version == NULL) || (timeout_us == 0U) || (timeout_us > 0x7FFFFFFFU))
        return SPI2_RESULT_INVALID_ARGUMENT;
    if (w5500_frame_failed)
        return SPI2_RESULT_NOT_READY;

    uint32_t start = timebase_now_us();
    /* RM0368 Rev 6, 8.4.7절 p.161: BSRR bit 12(BS12)=1, PB12 CS HIGH. */
    GPIOB->BSRR = (0x1U << 12);
    /* WIZnet WIZ550io J2: RDY HIGH는 자동 초기화 완료.
     * https://docs.wiznet.io/Product/ioModule/wiz550io
     * RM0368 Rev 6, 8.4.5절 p.160: IDR bit 1(IDR1), PB1 입력 확인. */
    while ((GPIOB->IDR & (0x1U << 1)) == 0U)
    {
        if (timebase_elapsed_us(start) >= timeout_us)
            return SPI2_RESULT_TIMEOUT;
    }
    if (timebase_elapsed_us(start) >= timeout_us)
        return SPI2_RESULT_TIMEOUT;

    /* BSRR bit 28(BR12)=1: PB12 CS LOW. 네 바이트 동안 선택을 유지한다. */
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
    /* 성공/실패 모두 CS HIGH. 하위 함수는 성공 시 TXE/BSY 종료까지 확인한다. */
    GPIOB->BSRR = (0x1U << 12);
    if (result == SPI2_RESULT_OK)
        *version = received;
    else
        w5500_frame_failed = true;
    return result;
}
