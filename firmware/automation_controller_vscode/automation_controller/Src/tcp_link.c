#include "tcp_link.h"
#include "w5500.h"
#include "timebase.h"
#include <string.h>

volatile tcp_status_t tcp_link_status;
static uint8_t rx[256], tx[256];
static uint16_t rx_size, rx_position, tx_size;
static bool configured, connected, sending, abort_pending;
static uint32_t session, last_poll, send_started, queue_started, state_started;
static uint8_t previous_state;

static void clear_session(void)
{
    if (connected) ++session;
    connected = sending = abort_pending = false;
    rx_size = rx_position = tx_size = 0U;
}

static bool io_result(spi2_result_t result)
{
    if (result == SPI2_RESULT_OK) return true;
    clear_session();
    tcp_link_status = TCP_IO_FAULT;
    return false;
}

static bool read_bytes(uint8_t block, uint16_t address, uint8_t *data, uint16_t count)
{
    return io_result(w5500_read(block, address, data, count, 10000U));
}

static bool write_bytes(uint8_t block, uint16_t address, const uint8_t *data, uint16_t count)
{
    return io_result(w5500_write(block, address, data, count, 10000U));
}

static bool write_byte(uint8_t block, uint16_t address, uint8_t value)
{
    return write_bytes(block, address, &value, 1U);
}

static bool read_word(uint16_t address, uint16_t *value)
{
    uint8_t bytes[2];
    if (!read_bytes(1U, address, bytes, 2U)) return false;
    *value = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    return true;
}

static bool write_word(uint16_t address, uint16_t value)
{
    uint8_t bytes[] = {(uint8_t)(value >> 8), (uint8_t)value};
    return write_bytes(1U, address, bytes, 2U);
}

static bool stable_word(uint16_t address, uint16_t *value)
{
    uint16_t previous;
    if (!read_word(address, &previous)) return false;
    for (unsigned i = 0U; i < 3U; ++i)
    {
        if (!read_word(address, value)) return false;
        if (*value == previous) return true;
        previous = *value;
    }
    return false;
}

static bool command(uint8_t value)
{
    uint32_t start = timebase_now_us();
    if (!write_byte(1U, 0x0001U, value)) return false;
    for (;;)
    {
        uint8_t pending;
        if (!read_bytes(1U, 0x0001U, &pending, 1U)) return false;
        if (pending == 0U) return true;
        if (timebase_elapsed_us(start) >= 10000U)
            return io_result(SPI2_RESULT_TIMEOUT);
    }
}

static void close_socket(void)
{
    clear_session();
    if (!command(0x10U)) return;
    tcp_link_status = TCP_LISTENING;
}

static bool configure(void)
{
    const uint8_t ip[] = {169U, 254U, 100U, 2U};
    const uint8_t mask[] = {255U, 255U, 0U, 0U};
    const uint8_t gateway[] = {0U, 0U, 0U, 0U};
    const uint8_t retry[] = {0x07U, 0xD0U, 3U};
    uint8_t version, mac[6], readback[4];
    if (!read_bytes(0U, 0x0039U, &version, 1U)) return false;
    if (version != 4U) return false;
    if (!read_bytes(0U, 0x0009U, mac, 6U)) return false;
    if ((mac[0] & 1U) != 0U || (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0U)
        return false;
    if (!command(0x10U) || !write_bytes(0U, 0x0001U, gateway, 4U) ||
        !write_bytes(0U, 0x0005U, mask, 4U) || !write_bytes(0U, 0x000FU, ip, 4U) ||
        !write_bytes(0U, 0x0019U, retry, 3U)) return false;
    if (!read_bytes(0U, 0x000FU, readback, 4U)) return false;
    if (memcmp(ip, readback, 4U) != 0) return io_result(SPI2_RESULT_HARDWARE_ERROR);
    if (!read_bytes(0U, 0x0005U, readback, 4U)) return false;
    if (memcmp(mask, readback, 4U) != 0) return io_result(SPI2_RESULT_HARDWARE_ERROR);
    if (!write_byte(1U, 0x001EU, 2U) || !write_byte(1U, 0x001FU, 2U) ||
        !write_byte(1U, 0x0000U, 1U) || !write_word(0x0004U, 5000U) ||
        !write_byte(1U, 0x002FU, 1U)) return false;
    configured = true;
    tcp_link_status = TCP_LISTENING;
    return true;
}

void tcp_link_init(void)
{
    configured = connected = sending = abort_pending = false;
    rx_size = rx_position = tx_size = 0U;
    session = 0U;
    last_poll = timebase_now_us();
    previous_state = 0xFFU;
    tcp_link_status = TCP_WAIT_MODULE;
}

bool tcp_link_connected(void) { return connected && !abort_pending; }
uint32_t tcp_link_session(void) { return session; }
void tcp_link_abort(void) { abort_pending = true; }

void tcp_link_poll(void)
{
    if (tcp_link_status == TCP_IO_FAULT) return;
    if (timebase_elapsed_us(last_poll) < (connected ? 1000U : 10000U)) return;
    last_poll = timebase_now_us();
    if (!configured)
    {
        if (!w5500_ready()) return;
        if (!configure()) return;
    }
    uint8_t phy, state, flags;
    if (!read_bytes(0U, 0x002EU, &phy, 1U)) return;
    if ((phy & (0x1U << 0)) == 0U)
    {
        if (tcp_link_status != TCP_LINK_DOWN) close_socket();
        if (tcp_link_status != TCP_IO_FAULT) tcp_link_status = TCP_LINK_DOWN;
        return;
    }
    if (abort_pending) { close_socket(); return; }
    if (!read_bytes(1U, 0x0003U, &state, 1U)) return;
    if (state != previous_state)
    {
        previous_state = state;
        state_started = timebase_now_us();
    }
    if (state != 0x17U && connected) clear_session();
    if (state == 0x00U)
    {
        clear_session();
        if (!write_byte(1U, 0x0002U, 0x1FU) || !command(0x01U)) return;
        tcp_link_status = TCP_LISTENING;
        return;
    }
    if (state == 0x13U) { if (command(0x02U)) tcp_link_status = TCP_LISTENING; return; }
    if (state == 0x14U) return;
    if (state == 0x16U)
    {
        if (timebase_elapsed_us(state_started) >= 2000000U) close_socket();
        return;
    }
    if (state != 0x17U) { close_socket(); return; }
    if (!connected)
    {
        clear_session(); connected = true; ++session;
        tcp_link_status = TCP_CONNECTED;
    }
    if (!read_bytes(1U, 0x0002U, &flags, 1U)) return;
    if ((flags & (0x1U << 3)) != 0U) { close_socket(); return; }
    if (sending)
    {
        if ((flags & (0x1U << 4)) != 0U)
        {
            if (!write_byte(1U, 0x0002U, (0x1U << 4))) return;
            sending = false;
        }
        else if (timebase_elapsed_us(send_started) >= 2000000U) { close_socket(); return; }
    }
    if (tx_size != 0U && timebase_elapsed_us(queue_started) >= 2000000U) { close_socket(); return; }
    if (!sending && tx_size != 0U)
    {
        uint16_t free_size, pointer;
        if (!stable_word(0x0020U, &free_size)) return;
        if (free_size >= tx_size)
        {
            if (!read_word(0x0024U, &pointer) || !write_bytes(2U, pointer, tx, tx_size) ||
                !write_word(0x0024U, (uint16_t)(pointer + tx_size)) ||
                !write_byte(1U, 0x0002U, (0x1U << 4)) || !command(0x20U)) return;
            sending = true;
            send_started = timebase_now_us();
            tx_size = 0U;
        }
    }
    if (rx_position == rx_size)
    {
        uint16_t available, pointer;
        if (!stable_word(0x0026U, &available) || available == 0U) return;
        if (available > sizeof(rx)) available = sizeof(rx);
        if (!read_word(0x0028U, &pointer) || !read_bytes(3U, pointer, rx, available) ||
            !write_word(0x0028U, (uint16_t)(pointer + available)) || !command(0x40U)) return;
        rx_size = available;
        rx_position = 0U;
    }
}

uint8_t tcp_link_try_read_byte(uint8_t *value)
{
    if (!tcp_link_connected() || rx_position == rx_size) return 0U;
    *value = rx[rx_position++];
    return 1U;
}

void tcp_link_write_text(const char *text)
{
    if (!tcp_link_connected()) return;
    size_t length = strlen(text);
    if (length > sizeof(tx) - tx_size) { abort_pending = true; return; }
    if (tx_size == 0U) queue_started = timebase_now_us();
    memcpy(tx + tx_size, text, length);
    tx_size += (uint16_t)length;
}

void tcp_link_write_u32(uint32_t value)
{
    char digits[11];
    unsigned i = sizeof(digits) - 1U;
    digits[i] = '\0';
    do { digits[--i] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    tcp_link_write_text(&digits[i]);
}
