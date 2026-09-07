#include "uart2.h"

#include "stm32f401xe.h"

void uart2_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    GPIOA->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);
    GPIOA->MODER |= GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1;

    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2 | GPIO_AFRL_AFSEL3);
    GPIOA->AFR[0] |= (7U << GPIO_AFRL_AFSEL2_Pos);
    GPIOA->AFR[0] |= (7U << GPIO_AFRL_AFSEL3_Pos);

    /* PCLK1=16 MHz, 115200 bit/s, 16배 오버샘플링: BRR=139. */
    USART2->CR1 = 0U;
    USART2->BRR = 139U;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

uint8_t uart2_try_read_byte(uint8_t *received_byte)
{
    if ((USART2->SR & USART_SR_RXNE) == 0U)
    {
        return 0U;
    }

    *received_byte = (uint8_t)USART2->DR;
    return 1U;
}

void uart2_write_byte(uint8_t byte)
{
    while ((USART2->SR & USART_SR_TXE) == 0U)
    {
    }

    USART2->DR = byte;
}

void uart2_write_text(const char *text)
{
    while (*text != '\0')
    {
        uart2_write_byte((uint8_t)*text);
        text++;
    }
}

void uart2_write_u32(uint32_t value)
{
    uint8_t digit_count = 0U;
    char digits[10];

    if (value == 0U)
    {
        uart2_write_byte((uint8_t)'0');
        return;
    }

    while (value != 0U)
    {
        digits[digit_count] = (char)('0' + (value % 10U));
        digit_count++;
        value /= 10U;
    }

    while (digit_count != 0U)
    {
        digit_count--;
        uart2_write_byte((uint8_t)digits[digit_count]);
    }
}
