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
    if (connected)
        ++session;
    connected = sending = abort_pending = false;
    rx_size = rx_position = tx_size = 0U;
}

static bool io_result(spi2_result_t result)
{
    if (result == SPI2_RESULT_OK)
        return true;
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
    bool success;

    uint8_t bytes[2];
    success = read_bytes(1U, address, bytes, 2U);
    if (success == false)
        return false;
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
    bool success;

    uint16_t previous;
    success = read_word(address, &previous);
    if (success == false)
        return false;
    for (unsigned i = 0U; i < 3U; ++i)
    {
        success = read_word(address, value);
        if (success == false)
            return false;

        if (*value == previous)
            return true;
        previous = *value;
    }

    return false;
}

static bool command(uint8_t value)
{
    bool success;

    uint32_t start = timebase_now_us();
    success = write_byte(1U, 0x0001U, value);
    if (success == false)
        return false;
    for (;;)
    {
        uint8_t pending;
        success = read_bytes(1U, 0x0001U, &pending, 1U);
        if (success == false)
            return false;

        if (pending == 0U)
            return true;

        if (timebase_elapsed_us(start) >= 10000U)
            return io_result(SPI2_RESULT_TIMEOUT);
    }
}

static void close_socket(void)
{
    bool success;

    clear_session();
    success = command(0x10U);
    if (success == false)
        return;
    tcp_link_status = TCP_LISTENING;
}

static bool configure(void)
{
    bool success;

    const uint8_t ip[] = {169U, 254U, 100U, 2U};
    const uint8_t mask[] = {255U, 255U, 0U, 0U};
    const uint8_t gateway[] = {0U, 0U, 0U, 0U};
    const uint8_t retry[] = {0x07U, 0xD0U, 3U};
    uint8_t version, mac[6], readback[4];
    success = read_bytes(0U, 0x0039U, &version, 1U);
    if (success == false)
        return false;

    if (version != 4U)
        return false;

    success = read_bytes(0U, 0x0009U, mac, 6U);
    if (success == false)
        return false;

    if ((mac[0] & 1U) != 0U || (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0U)
        return false;

    success = command(0x10U);
    if (success == false)
        return false;

    success = write_bytes(0U, 0x0001U, gateway, 4U);
    if (success == false)
        return false;

    success = write_bytes(0U, 0x0005U, mask, 4U);
    if (success == false)
        return false;

    success = write_bytes(0U, 0x000FU, ip, 4U);
    if (success == false)
        return false;

    success = write_bytes(0U, 0x0019U, retry, 3U);
    if (success == false)
        return false;

    success = read_bytes(0U, 0x000FU, readback, 4U);
    if (success == false)
        return false;

    if (memcmp(ip, readback, 4U) != 0)
        return io_result(SPI2_RESULT_HARDWARE_ERROR);

    success = read_bytes(0U, 0x0005U, readback, 4U);
    if (success == false)
        return false;

    if (memcmp(mask, readback, 4U) != 0)
        return io_result(SPI2_RESULT_HARDWARE_ERROR);

    success = write_byte(1U, 0x001EU, 2U);
    if (success == false)
        return false;

    success = write_byte(1U, 0x001FU, 2U);
    if (success == false)
        return false;

    success = write_byte(1U, 0x0000U, 1U);
    if (success == false)
        return false;

    success = write_word(0x0004U, 5000U);
    if (success == false)
        return false;

    success = write_byte(1U, 0x002FU, 1U);
    if (success == false)
        return false;
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

bool tcp_link_connected(void)
{
    return connected && abort_pending == false;
}

uint32_t tcp_link_session(void)
{
    return session;
}

void tcp_link_abort(void)
{
    abort_pending = true;
}

void tcp_link_poll(void)
{
    bool success;

    /* W5500 제어 오류는 재부팅 전까지 차단. */
    if (tcp_link_status == TCP_IO_FAULT)
        return;

    /* 연결 중 1 ms, 연결 전 10 ms 간격으로 처리. */
    if (timebase_elapsed_us(last_poll) < (connected ? 1000U : 10000U))
        return;
    last_poll = timebase_now_us();

    /* 모듈 준비 후 네트워크를 한 번 설정. */
    if (configured == false)
    {
        if (w5500_ready() == false)
            return;

        success = configure();
        if (success == false)
            return;
    }

    uint8_t phy, state, flags;

    /* PHYCFGR LNK[0]=0이면 연결 정리. 제어 오류 상태는 보존. */
    success = read_bytes(0U, 0x002EU, &phy, 1U);
    if (success == false)
        return;

    if ((phy & (0x1U << 0)) == 0U)
    {
        if (tcp_link_status != TCP_LINK_DOWN)
            close_socket();
        if (tcp_link_status != TCP_IO_FAULT)
            tcp_link_status = TCP_LINK_DOWN;
        return;
    }

    if (abort_pending)
    {
        close_socket();
        return;
    }

    success = read_bytes(1U, 0x0003U, &state, 1U);
    if (success == false)
        return;

    /* 상태가 바뀔 때만 대기 시작 시각 갱신. */
    if (state != previous_state)
    {
        previous_state = state;
        state_started = timebase_now_us();
    }

    if (state != 0x17U && connected)
        clear_session();

    /* CLOSED(00) → INIT(13) → LISTEN(14) → ESTABLISHED(17). */
    if (state == 0x00U)
    {
        clear_session();
        success = write_byte(1U, 0x0002U, 0x1FU);
        if (success == false)
            return;

        success = command(0x01U);
        if (success == false)
            return;
        tcp_link_status = TCP_LISTENING;
        return;
    }

    if (state == 0x13U)
    {
        success = command(0x02U);
        if (success == true)
            tcp_link_status = TCP_LISTENING;

        return;
    }

    if (state == 0x14U)
        return;

    /* SYNRECV(16): 연결 협상이 2초를 넘으면 종료. */
    if (state == 0x16U)
    {
        if (timebase_elapsed_us(state_started) >= 2000000U)
            close_socket();
        return;
    }

    if (state != 0x17U)
    {
        close_socket();
        return;
    }

    if (connected == false)
    {
        clear_session();
        connected = true;
        ++session;
        tcp_link_status = TCP_CONNECTED;
    }

    /* Sn_IR: TIMEOUT[3]은 종료, SENDOK[4]는 1을 써서 해제. */
    success = read_bytes(1U, 0x0002U, &flags, 1U);
    if (success == false)
        return;

    if ((flags & (0x1U << 3)) != 0U)
    {
        close_socket();
        return;
    }

    if (sending)
    {
        if ((flags & (0x1U << 4)) != 0U)
        {
            success = write_byte(1U, 0x0002U, (0x1U << 4));
            if (success == false)
                return;
            sending = false;
        }
        else if (timebase_elapsed_us(send_started) >= 2000000U)
        {
            close_socket();
            return;
        }
    }

    /* 송신 공간 대기도 2초로 제한. */
    if (tx_size != 0U && timebase_elapsed_us(queue_started) >= 2000000U)
    {
        close_socket();
        return;
    }

    /* W5500 버퍼에 복사 → 쓰기 포인터 갱신 → SEND 요청. */
    if (sending == false && tx_size != 0U)
    {
        uint16_t free_size, pointer;

        success = stable_word(0x0020U, &free_size);
        if (success == false)
            return;

        if (free_size >= tx_size)
        {
            /* 쓸 위치 확인. */
            success = read_word(0x0024U, &pointer);
            if (success == false)
                return;

            /* 보낼 데이터 복사. */
            success = write_bytes(2U, pointer, tx, tx_size);
            if (success == false)
                return;

            /* 복사한 만큼 쓰기 위치 이동. */
            success = write_word(0x0024U, (uint16_t)(pointer + tx_size));
            if (success == false)
                return;

            /* 이전 SENDOK[4] 표시 지우기. */
            success = write_byte(1U, 0x0002U, (0x1U << 4));
            if (success == false)
                return;

            /* SEND: 이제 LAN으로 보내기. */
            success = command(0x20U);
            if (success == false)
                return;

            sending = true;
            send_started = timebase_now_us();
            tx_size = 0U;
        }
    }

    /* 이전 데이터를 다 읽었으면 다음 수신 처리. */
    if (rx_position == rx_size)
    {
        uint16_t available, pointer;

        /* 도착한 데이터 길이 확인. */
        success = stable_word(0x0026U, &available);
        if (success == false)
            return;

        if (available == 0U)
            return;

        /* STM32 버퍼에 들어갈 만큼만 읽기. */
        if (available > sizeof(rx))
            available = sizeof(rx);

        /* 읽을 위치 확인. */
        success = read_word(0x0028U, &pointer);
        if (success == false)
            return;

        /* W5500에서 STM32 버퍼로 복사. */
        success = read_bytes(3U, pointer, rx, available);
        if (success == false)
            return;

        /* 읽은 만큼 읽기 위치 이동. */
        success = write_word(0x0028U, (uint16_t)(pointer + available));
        if (success == false)
            return;

        /* RECV: W5500에 데이터를 가져갔다고 알림. */
        success = command(0x40U);
        if (success == false)
            return;

        rx_size = available;
        rx_position = 0U;
    }
}

uint8_t tcp_link_try_read_byte(uint8_t *value)
{
    if (tcp_link_connected() == false || rx_position == rx_size)
        return 0U;
    *value = rx[rx_position++];
    return 1U;
}

void tcp_link_write_text(const char *text)
{
    if (tcp_link_connected() == false)
        return;
    size_t length = strlen(text);
    if (length > sizeof(tx) - tx_size)
    {
        abort_pending = true;
        return;
    }

    if (tx_size == 0U)
        queue_started = timebase_now_us();
    memcpy(tx + tx_size, text, length);
    tx_size += (uint16_t)length;
}

void tcp_link_write_u32(uint32_t value)
{
    char digits[11];
    unsigned i = sizeof(digits) - 1U;
    digits[i] = '\0';
    do
    {
        digits[--i] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    tcp_link_write_text(&digits[i]);
}
