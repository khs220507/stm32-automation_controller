#include "spi2.h"

#include "stm32f401xe.h"

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
