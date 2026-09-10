#include <stdint.h>
typedef struct { volatile uint32_t SR, DR, CR1; } host_usart_t;
extern host_usart_t host_usart;
#define USART2 (&host_usart)
#define USART2_IRQn 38
extern uint32_t host_primask;
static inline uint32_t __get_PRIMASK(void) { return host_primask; }
static inline void __disable_irq(void) { host_primask=1; }
static inline void __set_PRIMASK(uint32_t value) { host_primask=value; }
static inline void NVIC_ClearPendingIRQ(int irq) { (void)irq; }
static inline void NVIC_EnableIRQ(int irq) { (void)irq; }
