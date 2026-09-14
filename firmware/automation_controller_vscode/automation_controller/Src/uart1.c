#include "uart1.h"

#include "stm32f401xe.h"

void uart1_enable_clocks(void)
{
    /* RM0368 Rev 6, 6.3.9절 p.118~119: GPIOBEN 비트 1, GPIOB 클록 활성화. */
    RCC->AHB1ENR |= (0x1U << 1);
    (void)RCC->AHB1ENR;

    /* RM0368 Rev 6, 6.3.12절 p.122~123: USART1EN 비트 4, USART1 클록 활성화. */
    RCC->APB2ENR |= (0x1U << 4);
    (void)RCC->APB2ENR;
}
