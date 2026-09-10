#include "uart_diag.h"
#include "uart2.h"
#include "timebase.h"
#include "stm32f401xe.h"
#include <string.h>

static char input[16];
static volatile unsigned length;
static volatile uint32_t last_rx;
static volatile unsigned discard;
static const char *response;
static unsigned position;

void uart_diag_init(void)
{
    uart2_init();
    length = discard = position = 0U;
    response = 0;
    /* SR 다음 DR 읽기: 남은 수신값과 수신 오류를 지운다. */
    (void)USART2->SR;
    (void)USART2->DR;
    /* CR1 RXNEIE[5]: 수신 완료·오버런 인터럽트를 허용한다. */
    USART2->CR1 |= (0x1U << 5);
    NVIC_ClearPendingIRQ(USART2_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);
}

void uart_diag_poll(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    if ((length != 0U || discard != 0U) && timebase_elapsed_us(last_rx) >= 100000U)
        length = discard = 0U;
    __set_PRIMASK(saved);
}

void USART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;
    /* SR RXNE[5], ORE/NF/FE/PE[3:0]: 데이터와 오류를 함께 확인한다. */
    if ((status & ((0x1U << 5) | 0xFU)) != 0U)
    {
        char value = (char)USART2->DR;
        last_rx = timebase_now_us();
        if ((status & 0xFU) != 0U) { discard = 1U; length = 0U; }
        else if (value == '\n')
        {
            if (response == 0 && (length != 0U || discard != 0U))
            {
                input[length] = '\0';
                response = discard == 0U && strcmp(input, "PING\r") == 0
                    ? "OK,PING,PONG\r\n" : "ERR,UART,INVALID_COMMAND\r\n";
                position = 0U;
                /* CR1 TXEIE[7]: 송신 버퍼가 빌 때 다음 바이트를 보낸다. */
                USART2->CR1 |= (0x1U << 7);
            }
            length = discard = 0U;
        }
        else if (discard == 0U)
        {
            if (length < sizeof(input) - 1U && value != '\0') input[length++] = value;
            else discard = 1U;
        }
    }
    if ((status & (0x1U << 7)) != 0U && (USART2->CR1 & (0x1U << 7)) != 0U)
    {
        /* SR TXE[7]=1일 때 DR[7:0]에 쓰고, 마지막 바이트 뒤 TXEIE를 끈다. */
        USART2->DR = (uint8_t)response[position++];
        if (response[position] == '\0')
        {
            USART2->CR1 &= ~(0x1U << 7);
            response = 0;
        }
    }
}
