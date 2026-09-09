#include "spi2.h"
#include "timebase.h"

#include <stdbool.h>
#include <stddef.h>

#include "stm32f401xe.h"

/* 실패한 프레임의 늦은 수신값을 다음 전송에 섞지 않도록 재부팅까지 차단한다. */
static bool spi2_transfer_failed;

static spi2_result_t spi2_wait_status(uint32_t mask, uint32_t expected,
                                     uint32_t start, uint32_t timeout_us)
{
    for (;;)
    {
        uint32_t status = SPI2->SR;

        /* SR: FRE(8), OVR(6), MODF(5), CRCERR(4)는 전송 오류. */
        if ((status & ((0x1U << 8) | (0x1U << 6) | (0x1U << 5) | (0x1U << 4))) != 0U)
            return SPI2_RESULT_HARDWARE_ERROR;
        /* CR1: SPE(6)·MSTR(2)가 모두 1이어야 한다. */
        if ((SPI2->CR1 & ((0x1U << 6) | (0x1U << 2))) != ((0x1U << 6) | (0x1U << 2)))
            return SPI2_RESULT_NOT_READY;
        if (timebase_elapsed_us(start) >= timeout_us)
            return SPI2_RESULT_TIMEOUT;
        if ((status & mask) == expected)
            return SPI2_RESULT_OK;
    }
}

spi2_result_t spi2_transfer_byte(uint8_t tx, uint8_t *rx, uint32_t timeout_us)
{
    uint32_t start;
    uint8_t received;
    spi2_result_t result;

    if ((rx == NULL) || (timeout_us == 0U) || (timeout_us > 0x7FFFFFFFU))
        return SPI2_RESULT_INVALID_ARGUMENT;
    if (spi2_transfer_failed)
        return SPI2_RESULT_NOT_READY;

    start = timebase_now_us();
    /* SR bit 1(TXE)=1: 송신 버퍼가 비면 쓸 수 있다. */
    result = spi2_wait_status((0x1U << 1), (0x1U << 1), start, timeout_us);
    if (result != SPI2_RESULT_OK) goto failed;

    /* SR bit 7(BSY)·bit 0(RXNE): 이전 전송/수신값이 남아 있으면 거부한다. */
    if ((SPI2->SR & ((0x1U << 7) | (0x1U << 0))) != 0U)
    {
        result = SPI2_RESULT_DIRTY_STATE;
        goto failed;
    }

    /* DR[7:0]: 쓰기는 송신 버퍼, 읽기는 별도의 수신 버퍼에 접근한다. */
    SPI2->DR = tx;
    /* SR bit 0(RXNE)=1: 1바이트 수신 완료. */
    result = spi2_wait_status((0x1U << 0), (0x1U << 0), start, timeout_us);
    if (result != SPI2_RESULT_OK) goto failed;
    received = (uint8_t)SPI2->DR;

    /* TXE(1)=1 다음 BSY(7)=0 확인: CS 해제 전 마지막 클록까지 끝나야 한다. */
    result = spi2_wait_status((0x1U << 1), (0x1U << 1), start, timeout_us);
    if (result != SPI2_RESULT_OK) goto failed;
    result = spi2_wait_status((0x1U << 7), 0U, start, timeout_us);
    if (result != SPI2_RESULT_OK) goto failed;

    *rx = received;
    return SPI2_RESULT_OK;

failed:
    spi2_transfer_failed = true;
    return result;
}

void spi2_enable_clocks(void)
{
    /* RM0368 Rev 6, 6.3.9절 p.118~119:
     * AHB1ENR bit 1(GPIOBEN), bit 2(GPIOCEN)=1: GPIOB·GPIOC 클록 공급.
     * PB12~PB15와 PB1, PC4의 핀 설정·입출력에 필요한 내부 클록이다. */
    RCC->AHB1ENR |= (0x1U << 1) | (0x1U << 2);
    (void)RCC->AHB1ENR;

    /* RM0368 Rev 6, 6.3.11절 p.119~120:
     * APB1ENR bit 14(SPI2EN)=1: SPI2 주변장치에 내부 클록 공급.
     * SPI 설정·활성화나 SCLK 핀의 통신 파형 출력은 아직 하지 않는다. */
    RCC->APB1ENR |= (0x1U << 14);
    (void)RCC->APB1ENR;
}

void spi2_pins_init(void)
{
    /* RM0368 Rev 6, 8.4.7절 p.161: BSRR bit 12/4에 1을 써 출력 래치를 HIGH로 준비.
     * PB12=SCSn 선택 해제, PC4=RSTn 리셋 해제. 출력 모드보다 먼저 설정한다. */
    GPIOB->BSRR = (0x1U << 12);
    GPIOC->BSRR = (0x1U << 4);

    /* 8.4.2절 p.158: OTYPER bit 12~15와 bit 4를 0으로 설정해 push-pull 사용.
     * PB14는 SPI2 MISO 입력이므로 출력 형식 설정은 입력 동작에 영향을 주지 않는다. */
    GPIOB->OTYPER &= ~(0xFU << 12);
    GPIOC->OTYPER &= ~(0x1U << 4);

    /* 8.4.3절 p.159: OSPEEDR는 출력 전환 속도이며 SPI 클록 주파수 설정이 아니다.
     * PB12 field 25:24=00(low), PB13~15 fields 31:26=10(high) 각각 설정.
     * PC4 field 9:8=00(low). 실제 SCLK 속도는 후속 SPI2 분주 설정에서 정한다. */
    GPIOB->OSPEEDR &= ~(0xFFU << 24);
    GPIOB->OSPEEDR |= (0x2U << 26) | (0x2U << 28) | (0x2U << 30);
    GPIOC->OSPEEDR &= ~(0x3U << 8);

    /* 8.4.4절 p.159~160: PUPDR의 PB1 field 3:2, PB12~15 fields 31:24,
     * PC4 field 9:8을 00으로 설정해 내부 pull-up/down을 사용하지 않는다.
     * PB1의 RDY 입력은 전원이 공급된 WIZ550io가 구동하는 조건이다. */
    GPIOB->PUPDR &= ~((0x3U << 2) | (0xFFU << 24));
    GPIOC->PUPDR &= ~(0x3U << 8);

    /* 8.4.10절 p.163: AFRH의 PB13 field 23:20, PB14 27:24, PB15 31:28=0101(AF5).
     * DS10086 Rev 5, Table 9 p.46: 각각 SPI2_SCK, SPI2_MISO, SPI2_MOSI 연결. */
    GPIOB->AFR[1] &= ~(0xFFFU << 20);
    GPIOB->AFR[1] |= (0x5U << 20) | (0x5U << 24) | (0x5U << 28);

    /* 8.4.1절 p.158: MODER는 핀마다 2비트다. 다른 준비를 마친 뒤 모드를 전환한다.
     * PB1 field 3:2=00(RDY 입력), PB12 25:24=01(SCSn 일반 출력),
     * PB13~15 fields 31:26=10(AF) 각각 설정. */
    GPIOB->MODER &= ~((0x3U << 2) | (0xFFU << 24));
    GPIOB->MODER |= (0x1U << 24) | (0x2U << 26) | (0x2U << 28) | (0x2U << 30);

    /* PC4 MODER field 9:8=01: RSTn 일반 GPIO 출력. 리셋 펄스는 아직 발생시키지 않는다. */
    GPIOC->MODER &= ~(0x3U << 8);
    GPIOC->MODER |= (0x1U << 8);
}

void spi2_configure(void)
{
    /* 부팅 초기화 전용: SPE(bit 6)=0으로 SPI를 끄고 설정한다. */
    SPI2->CR1 = 0U;

    /* I2SMOD(bit 11)=0: SPI 선택, I2SE(bit 10)=0: I2S 끔. */
    SPI2->I2SCFGR &= ~((0x1U << 11) | (0x1U << 10));

    /* 인터럽트(7:5)·DMA(1:0)·NSS 출력(2) 끔, Motorola 형식(4=0). */
    SPI2->CR2 = 0U;

    /* SSM(9)·SSI(8)=1: 내부 NSS HIGH, 실제 CS는 GPIO로 제어.
     * BR(5:3)=011: PCLK1/16, MSTR(2)=1: 마스터.
     * DFF(11)·LSBFIRST(7)·CPOL/CPHA(1:0)=0: 8비트·MSB 우선·Mode 0. */
    SPI2->CR1 = (0x1U << 9) | (0x1U << 8) | (0x3U << 3) | (0x1U << 2);

    /* SPE(bit 6)=1: SPI 활성화. */
    SPI2->CR1 |= (0x1U << 6);
}
