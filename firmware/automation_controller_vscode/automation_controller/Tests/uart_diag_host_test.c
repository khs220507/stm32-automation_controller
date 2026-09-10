#include <assert.h>
#include <stdio.h>
#include "../Src/uart_diag.c"
host_usart_t host_usart;
uint32_t host_primask;
static uint32_t now;
uint32_t timebase_now_us(void) { return now; }
uint32_t timebase_elapsed_us(uint32_t start) { return now-start; }
void uart2_init(void) { memset(&host_usart,0,sizeof(host_usart)); }
static void receive(const char *text)
{
    while (*text) { host_usart.SR=1U<<5; host_usart.DR=(uint8_t)*text++; USART2_IRQHandler(); }
}
static void expect(const char *expected)
{
    char actual[64]; unsigned count=0;
    while ((host_usart.CR1 & (1U<<7)) != 0)
    {
        assert(count<sizeof(actual)-1);
        host_usart.SR=1U<<7; USART2_IRQHandler(); actual[count++]=(char)host_usart.DR;
    }
    actual[count]=0; assert(strcmp(actual,expected)==0);
}
int main(void)
{
    uart_diag_init(); assert(host_usart.CR1==(1U<<5));
    receive("PING\r\n"); expect("OK,PING,PONG\r\n");
    receive("CHECK_W5500\r\n"); expect("ERR,UART,INVALID_COMMAND\r\n");
    receive("xxxxxxxxxxxxxxxxxxxxxxxx\r\n"); expect("ERR,UART,INVALID_COMMAND\r\n");
    receive("PING\n"); expect("ERR,UART,INVALID_COMMAND\r\n");
    receive("PI"); now+=100001; uart_diag_poll(); assert(length==0 && host_primask==0);
    receive("PING\r\n"); expect("OK,PING,PONG\r\n");
    receive("PI"); host_usart.SR=(1U<<5)|(1U<<3); host_usart.DR='N'; USART2_IRQHandler();
    receive("G\r\n"); expect("ERR,UART,INVALID_COMMAND\r\n");
    receive("PING\r\n"); receive("PING\r\n"); expect("OK,PING,PONG\r\n");
    now=UINT32_MAX-10; receive("P"); now+=100001; host_primask=1; uart_diag_poll(); assert(length==0 && host_primask==1);
    puts("UART diagnostic model passed: PING, invalid/long frames, partial timeout/wrap, overrun discard, busy response, PRIMASK restoration.");
}
