#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tcp_link.h"
#include "w5500.h"

static uint8_t memory[4][65536];
static uint32_t now;
static bool fault, stuck, send_ok = true;
static uint16_t acknowledged;
uint32_t timebase_now_us(void) { return now; }
uint32_t timebase_elapsed_us(uint32_t start) { return ++now - start; }
bool w5500_ready(void) { return true; }
static uint16_t word(unsigned address) { return (uint16_t)((memory[1][address] << 8) | memory[1][address+1]); }
static void set_word(unsigned address, uint16_t value) { memory[1][address] = value >> 8; memory[1][address+1] = (uint8_t)value; }
spi2_result_t w5500_read(uint8_t block, uint16_t address, uint8_t *data, uint16_t size, uint32_t timeout)
{
    assert(timeout > 0);
    if (fault) return SPI2_RESULT_HARDWARE_ERROR;
    for (unsigned i=0; i<size; ++i) data[i] = memory[block][(uint16_t)(address+i)];
    return SPI2_RESULT_OK;
}
spi2_result_t w5500_write(uint8_t block, uint16_t address, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    assert(timeout > 0);
    if (fault) return SPI2_RESULT_HARDWARE_ERROR;
    if (block == 1 && address == 2) { memory[1][2] &= (uint8_t)~data[0]; return SPI2_RESULT_OK; }
    for (unsigned i=0; i<size; ++i) memory[block][(uint16_t)(address+i)] = data[i];
    if (block == 1 && address == 1 && !stuck)
    {
        switch(data[0])
        {
            case 0x10: memory[1][3]=0; break;
            case 1: memory[1][3]=0x13; set_word(0x20,2048); break;
            case 2: memory[1][3]=0x14; break;
            case 0x20: if(send_ok) memory[1][2] |= 0x10; break;
            case 0x40:
                set_word(0x26,(uint16_t)(word(0x26)-(uint16_t)(word(0x28)-acknowledged)));
                acknowledged=word(0x28); break;
        }
        memory[1][1]=0;
    }
    return SPI2_RESULT_OK;
}
static void poll(void) { now+=10001; tcp_link_poll(); }
static void reset(void)
{
    memset(memory,0,sizeof(memory)); now=0; fault=stuck=false; send_ok=true;
    memory[0][0x39]=4; memory[0][9]=2; memory[0][0x2e]=1; acknowledged=0;
    tcp_link_init(); poll(); poll();
    assert(memory[1][3]==0x14 && tcp_link_status==TCP_LISTENING);
    assert(word(4)==5000 && memory[0][0xf]==169 && memory[0][0x12]==2);
    memory[1][3]=0x17; poll(); assert(tcp_link_connected());
}
int main(void)
{
    reset(); uint32_t session=tcp_link_session();
    set_word(0x28,65534); acknowledged=65534;
    memory[3][65534]='P'; memory[3][65535]='I'; memory[3][0]='N'; memory[3][1]='G';
    set_word(0x26,4); poll();
    uint8_t byte; const char *expected="PING";
    for (unsigned i=0;i<4;++i) { assert(tcp_link_try_read_byte(&byte)); assert(byte==expected[i]); }
    assert(!tcp_link_try_read_byte(&byte) && word(0x28)==2 && word(0x26)==0);
    set_word(0x24,65534); tcp_link_write_text("OK,"); tcp_link_write_u32(4294967295U); tcp_link_write_text("\r\n"); poll();
    assert(memory[2][65534]=='O' && memory[2][65535]=='K' && memory[2][0]==',' && word(0x24)==13);
    poll(); memory[1][3]=0x1c; poll(); assert(!tcp_link_connected() && tcp_link_session()!=session);
    poll(); poll(); memory[1][3]=0x17; poll(); assert(tcp_link_connected() && !tcp_link_try_read_byte(&byte));
    memory[0][0x2e]=0; poll(); assert(tcp_link_status==TCP_LINK_DOWN && !tcp_link_connected());
    memory[0][0x2e]=1; poll(); poll(); assert(memory[1][3]==0x14);
    reset(); send_ok=false; tcp_link_write_text("test"); poll(); now+=2000001; poll(); assert(!tcp_link_connected());
    reset(); set_word(0x20,0); tcp_link_write_text("test"); poll(); now+=2000001; poll(); assert(!tcp_link_connected());
    reset(); char large[258]; memset(large,'x',257); large[257]=0; tcp_link_write_text(large); assert(!tcp_link_connected()); poll(); assert(memory[1][3]==0);
    reset(); fault=true; poll(); assert(tcp_link_status==TCP_IO_FAULT && !tcp_link_connected());
    reset(); stuck=true; tcp_link_abort(); poll(); assert(tcp_link_status==TCP_IO_FAULT);
    reset(); memory[1][3]=0x16; poll(); now+=2000001; poll(); assert(!tcp_link_connected());
    reset(); now=UINT32_MAX-5000; poll(); tcp_link_write_text("wrap"); poll(); assert(tcp_link_connected());
    puts("TCP socket model passed: config, RX/TX pointer wrap, disconnect/reconnect, link loss, send/queue/command deadlines, overflow, SPI fault, clock wrap.");
    return 0;
}
