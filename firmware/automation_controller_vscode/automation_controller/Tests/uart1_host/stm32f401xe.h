#include <stdint.h>
typedef struct
{
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3;
} host_usart_t;
typedef struct
{
    volatile uint32_t AHB1ENR, APB2ENR;
} host_rcc_t;
typedef struct
{
    volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, AFR[2];
} host_gpio_t;
extern host_usart_t host_usart;
extern host_rcc_t host_rcc;
extern host_gpio_t host_gpio;
#define USART1 (&host_usart)
#define RCC (&host_rcc)
#define GPIOB (&host_gpio)
#define USART1_IRQn 37
static inline void NVIC_DisableIRQ(int irq)
{
    (void)irq;
}
static inline void NVIC_ClearPendingIRQ(int irq)
{
    (void)irq;
}
static inline void NVIC_EnableIRQ(int irq)
{
    (void)irq;
}
static inline void NVIC_SetPriority(int irq, unsigned priority)
{
    (void)irq;
    (void)priority;
}
