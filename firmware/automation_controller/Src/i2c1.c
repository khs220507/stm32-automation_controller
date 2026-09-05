#include "i2c1.h"

#include "stm32f401xe.h"

void i2c1_pins_init(void)
{
    /* Enable GPIOB and complete a read-back before accessing its registers. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;

    /* I2C devices pull the shared bus low, so both outputs must be open-drain. */
    GPIOB->OTYPER |= GPIO_OTYPER_OT8 | GPIO_OTYPER_OT9;

    /* Select high GPIO output speed for clean I2C signal edges. */
    GPIOB->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED8 | GPIO_OSPEEDR_OSPEED9);
    GPIOB->OSPEEDR |= GPIO_OSPEEDR_OSPEED8_1 | GPIO_OSPEEDR_OSPEED9_1;

    /* The GY-521 module supplies the external SDA/SCL pull-up resistors. */
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPD8 | GPIO_PUPDR_PUPD9);

    /* Route I2C1 SCL/SDA to PB8/PB9 through alternate function AF4. */
    GPIOB->AFR[1] &= ~(GPIO_AFRH_AFSEL8 | GPIO_AFRH_AFSEL9);
    GPIOB->AFR[1] |= GPIO_AFRH_AFSEL8_2 | GPIO_AFRH_AFSEL9_2;

    /* Switch the pins to alternate-function mode after configuring them. */
    GPIOB->MODER &= ~(GPIO_MODER_MODER8 | GPIO_MODER_MODER9);
    GPIOB->MODER |= GPIO_MODER_MODER8_1 | GPIO_MODER_MODER9_1;
}
