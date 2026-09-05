#include "i2c1.h"

#include "stm32f401xe.h"

void i2c1_pins_init(void)
{
    /* GPIOBEN: bit 1 = 1 enables GPIOB; read back before register access. */
    RCC->AHB1ENR |= (0x1U << 1);
    (void)RCC->AHB1ENR;

    /* OT8/OT9: bits 8/9 = 1 selects open-drain. */
    GPIOB->OTYPER |= (0x1U << 8) | (0x1U << 9);

    /* OSPEED8/9: bits 17:16 / 19:18 = binary 10 (high speed). */
    GPIOB->OSPEEDR &= ~((0x3U << 16) | (0x3U << 18));
    GPIOB->OSPEEDR |=  (0x2U << 16) | (0x2U << 18);

    /* PUPD8/9: bits 17:16 / 19:18 = binary 00 (no internal pull).
     * Assumes suitable external SDA/SCL pull-ups on the module. */
    GPIOB->PUPDR &= ~((0x3U << 16) | (0x3U << 18));

    /* AFR[1] is AFRH. AFSEL8/9: bits 3:0 / 7:4 = binary 0100 (AF4). */
    GPIOB->AFR[1] &= ~((0xFU << 0) | (0xFU << 4));
    GPIOB->AFR[1] |=  (0x4U << 0) | (0x4U << 4);

    /* MODER8/9: bits 17:16 / 19:18 = binary 10 (alternate function).
     * Switch modes after configuring the electrical properties and AF. */
    GPIOB->MODER &= ~((0x3U << 16) | (0x3U << 18));
    GPIOB->MODER |=  (0x2U << 16) | (0x2U << 18);
}
