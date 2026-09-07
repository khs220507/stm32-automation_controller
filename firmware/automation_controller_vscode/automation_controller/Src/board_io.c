#include "board_io.h"

#include "stm32f401xe.h"

void board_io_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER &= ~GPIO_MODER_MODER5;
    GPIOA->MODER |= GPIO_MODER_MODER5_0;

    board_io_set_safe_outputs();
}

void board_io_set_safe_outputs(void)
{
    GPIOA->BSRR = GPIO_BSRR_BR5;
}
