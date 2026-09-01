#include "hcsr04.h"

#include "stm32f401xe.h"
#include "timebase.h"

#define HCSR04_EDGE_TIMEOUT_US 30000U
#define HCSR04_MIN_PULSE_US 116U
#define HCSR04_MAX_PULSE_US 23200U

static void hcsr04_trigger(void);
static uint8_t hcsr04_wait_capture(uint32_t *capture, uint32_t timeout_us);
static uint8_t hcsr04_capture_rising_edge(uint32_t *rising_time);
static uint8_t hcsr04_capture_falling_edge(uint32_t *falling_time);
static uint8_t hcsr04_measure_pulse(uint32_t *pulse_us);

void hcsr04_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->BSRR = GPIO_BSRR_BR10;
    GPIOA->MODER &= ~GPIO_MODER_MODER10;
    GPIOA->MODER |= GPIO_MODER_MODER10_0;

    GPIOA->MODER &= ~GPIO_MODER_MODER1;
    GPIOA->MODER |= GPIO_MODER_MODER1_1;
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD1;
    GPIOA->AFR[0] &= ~GPIO_AFRL_AFSEL1;
    GPIOA->AFR[0] |= (1U << GPIO_AFRL_AFSEL1_Pos);

    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->CR1 = 0U;
    TIM2->PSC = 15U;
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->CNT = 0U;
    TIM2->CCMR1 &= ~TIM_CCMR1_CC2S;
    TIM2->CCMR1 |= TIM_CCMR1_CC2S_0;
    TIM2->CCER &= ~(TIM_CCER_CC2E | TIM_CCER_CC2P | TIM_CCER_CC2NP);
    TIM2->CCER |= TIM_CCER_CC2E;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;
    TIM2->CR1 |= TIM_CR1_CEN;
}

void hcsr04_measure(hcsr04_measurement_t *measurement)
{
    uint32_t measured_pulse_us = 0U;

    measurement->status = HCSR04_STATUS_TIMEOUT;
    measurement->distance_cm = 0U;
    measurement->pulse_us = 0U;

    if (hcsr04_measure_pulse(&measured_pulse_us) == 0U)
    {
        return;
    }

    measurement->pulse_us = measured_pulse_us;

    if ((measured_pulse_us < HCSR04_MIN_PULSE_US) ||
        (measured_pulse_us > HCSR04_MAX_PULSE_US))
    {
        measurement->status = HCSR04_STATUS_OUT_OF_RANGE;
        return;
    }

    measurement->status = HCSR04_STATUS_OK;
    measurement->distance_cm = (measured_pulse_us + 29U) / 58U;
}

static void hcsr04_trigger(void)
{
    GPIOA->BSRR = GPIO_BSRR_BS10;
    timebase_delay_us(10U);
    GPIOA->BSRR = GPIO_BSRR_BR10;
}

static uint8_t hcsr04_wait_capture(uint32_t *capture, uint32_t timeout_us)
{
    uint32_t start_time_us = timebase_now_us();

    while ((TIM2->SR & TIM_SR_CC2IF) == 0U)
    {
        if (timebase_elapsed_us(start_time_us) >= timeout_us)
        {
            return 0U;
        }
    }

    *capture = TIM2->CCR2;
    TIM2->SR &= ~(TIM_SR_CC2IF | TIM_SR_CC2OF);
    return 1U;
}

static uint8_t hcsr04_capture_rising_edge(uint32_t *rising_time)
{
    TIM2->CCER &= ~(TIM_CCER_CC2P | TIM_CCER_CC2NP);
    TIM2->SR &= ~(TIM_SR_CC2IF | TIM_SR_CC2OF);
    hcsr04_trigger();
    return hcsr04_wait_capture(rising_time, HCSR04_EDGE_TIMEOUT_US);
}

static uint8_t hcsr04_capture_falling_edge(uint32_t *falling_time)
{
    uint8_t capture_result;

    TIM2->CCER &= ~TIM_CCER_CC2NP;
    TIM2->CCER |= TIM_CCER_CC2P;
    TIM2->SR &= ~(TIM_SR_CC2IF | TIM_SR_CC2OF);
    capture_result = hcsr04_wait_capture(falling_time, HCSR04_EDGE_TIMEOUT_US);
    TIM2->CCER &= ~(TIM_CCER_CC2P | TIM_CCER_CC2NP);

    return capture_result;
}

static uint8_t hcsr04_measure_pulse(uint32_t *pulse_us)
{
    uint32_t rising_time;
    uint32_t falling_time;

    *pulse_us = 0U;
    if (hcsr04_capture_rising_edge(&rising_time) == 0U)
    {
        return 0U;
    }

    if (hcsr04_capture_falling_edge(&falling_time) == 0U)
    {
        return 0U;
    }

    *pulse_us = falling_time - rising_time;
    return 1U;
}
