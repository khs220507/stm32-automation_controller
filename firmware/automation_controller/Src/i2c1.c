#include "i2c1.h"

#include "stm32f401xe.h"

void i2c1_pins_init(void)
{
    /* AHB1ENR의 비트 1(GPIOBEN)을 1로 설정하여 GPIOB 클록을 활성화한다.
     * GPIOB 레지스터에 접근하기 전에 AHB1ENR을 한 번 읽고 값을 버린다. */
    RCC->AHB1ENR |= (0x1U << 1);
    (void)RCC->AHB1ENR;

    /* OTYPER는 핀당 1비트다. 비트 8과 9를 1로 설정한다.
     * PB8과 PB9를 오픈 드레인 출력으로 설정하며 다른 비트는 유지한다. */
    GPIOB->OTYPER |= (0x1U << 8) | (0x1U << 9);

    /* OSPEEDR는 핀당 2비트다. PB8은 17:16, PB9는 19:18에 해당한다.
     * 0x3(2진수 11)으로 각 필드를 지운 뒤 0x2(2진수 10)를 넣는다.
     * 두 핀의 출력 속도를 고속으로 설정한다. I2C 통신 주파수 설정은 아니다. */
    GPIOB->OSPEEDR &= ~((0x3U << 16) | (0x3U << 18));
    GPIOB->OSPEEDR |=  (0x2U << 16) | (0x2U << 18);

    /* PUPDR는 핀당 2비트다. PB8의 17:16, PB9의 19:18을 00으로 지운다.
     * 내부 풀업과 풀다운을 사용하지 않는다. 다른 핀의 설정은 유지한다.
     * 모듈에 적절한 외부 SDA/SCL 풀업 저항이 연결되어 있다고 가정한다. */
    GPIOB->PUPDR &= ~((0x3U << 16) | (0x3U << 18));

    GPIOB->AFR[1] &= ~((0xFU << 0) | (0xFU << 4));
    GPIOB->AFR[1] |=  (0x4U << 0) | (0x4U << 4);

    GPIOB->MODER &= ~((0x3U << 16) | (0x3U << 18));
    GPIOB->MODER |=  (0x2U << 16) | (0x2U << 18);
}
