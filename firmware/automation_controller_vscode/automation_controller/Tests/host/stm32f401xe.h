#ifndef HOST_STM32F401XE_H
#define HOST_STM32F401XE_H

#include <stdint.h>

/* 호스트 시험 전용. 실제 레지스터 주소/동작을 대체하므로 실기 증거가 아니다. */
typedef struct { volatile uint32_t CR1, CR2, OAR1, DR, SR1, SR2, CCR, TRISE; } host_i2c_t;
typedef struct { volatile uint32_t APB1RSTR, CFGR, AHB1ENR, APB1ENR; } host_rcc_t;
typedef struct { volatile uint32_t OTYPER, OSPEEDR, PUPDR, AFR[2], MODER; } host_gpio_t;
typedef struct { volatile uint32_t CR1; } host_timer_t;
extern host_i2c_t host_i2c;
extern host_rcc_t host_rcc;
extern host_gpio_t host_gpio;
extern host_timer_t host_timer;
#define I2C1 (&host_i2c)
#define RCC (&host_rcc)
#define GPIOB (&host_gpio)
#define TIM5 (&host_timer)
extern uint32_t SystemCoreClock;
extern const uint8_t APBPrescTable[8];
void SystemCoreClockUpdate(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
#endif
