#ifndef SPI2_HOST_STM32F401XE_H
#define SPI2_HOST_STM32F401XE_H
#include <stdint.h>
/* SPI 전용 호스트 모형. 실제 주소와 DR 읽기 부수 효과는 재현하지 않는다. */
typedef struct { volatile uint32_t CR1, CR2, SR, DR, I2SCFGR; } host_spi_t;
typedef struct { volatile uint32_t AHB1ENR, APB1ENR; } host_rcc_t;
typedef struct { volatile uint32_t BSRR, OTYPER, OSPEEDR, PUPDR, AFR[2], MODER, IDR; } host_gpio_t;
extern host_spi_t host_spi;
extern host_rcc_t host_rcc;
extern host_gpio_t host_gpiob, host_gpioc;
#define SPI2 (&host_spi)
#define RCC (&host_rcc)
#define GPIOB (&host_gpiob)
#define GPIOC (&host_gpioc)
#endif
