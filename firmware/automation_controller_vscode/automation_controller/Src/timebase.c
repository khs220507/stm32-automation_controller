#include "timebase.h"

#include "stm32f401xe.h"

void timebase_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;

    TIM5->CR1 = 0U;
    TIM5->PSC = 15U;
    TIM5->ARR = 0xFFFFFFFFU;
    TIM5->CNT = 0U;
    TIM5->EGR = TIM_EGR_UG;
    TIM5->SR = 0U;
    TIM5->CR1 |= TIM_CR1_CEN;
}

uint32_t timebase_now_us(void)
{
    return TIM5->CNT;
}

uint32_t timebase_elapsed_us(uint32_t start_time_us)
{
    return (uint32_t)(timebase_now_us() - start_time_us);
}

void timebase_delay_us(uint32_t delay_us)
{
    uint32_t start_time_us = timebase_now_us();

    while (timebase_elapsed_us(start_time_us) < delay_us)
    {
    }
}
