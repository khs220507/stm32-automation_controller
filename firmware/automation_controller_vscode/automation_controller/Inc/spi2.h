#ifndef SPI2_DRIVER_H
#define SPI2_DRIVER_H

/* 학습 1단계: GPIOB·GPIOC·SPI2 클록을 켠다. */
void spi2_enable_clocks(void);

/* 학습 2단계: 클록 공급 후, SPI2가 비활성화된 부팅 시 한 번 호출한다.
 * GPIO와 AF5만 설정한다. SPI2 통신 설정·활성화는 후속 단계다. */
void spi2_pins_init(void);

#endif
