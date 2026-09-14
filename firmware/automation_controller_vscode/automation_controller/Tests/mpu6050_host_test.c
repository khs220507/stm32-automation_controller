/* 센서 레지스터 모형으로 실제 센서 처리/명령/응답 코드를 시험한다. 실기 측정이 아니다. */
#include "mpu6050.h"
#include "i2c1.h"
#include "app_state.h"
#include "hcsr04.h"
#include "timebase.h"
#include "uart2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t regs[128];
static uint32_t now;
static int operations, fail_at, ignore_write, no_data, writes, bursts;
static i2c1_result_t injected;
static const char *input;
static char output[256];
extern bool host_tcp_connected;
extern uint32_t host_tcp_session;

void timebase_init(void) { }
uint32_t timebase_now_us(void) { return now; }
uint32_t timebase_elapsed_us(uint32_t start) { now += 1000U; return now - start; }
bool i2c1_init(void) { return true; }
static i2c1_result_t transfer(void) { return ++operations == fail_at ? injected : I2C1_RESULT_OK; }
i2c1_result_t i2c1_read_register(uint8_t address, uint8_t reg, uint8_t *value, uint32_t timeout)
{
    assert(address == 0x68U && reg < sizeof regs && timeout > 0U && timeout <= 10000U);
    i2c1_result_t result = transfer();
    if (result != I2C1_RESULT_OK) return result;
    *value = reg == 0x3AU ? (no_data ? 0U : 1U) : regs[reg];
    return result;
}
i2c1_result_t i2c1_write_register(uint8_t address, uint8_t reg, uint8_t value, uint32_t timeout)
{
    assert(address == 0x68U && reg < sizeof regs && timeout == 10000U);
    i2c1_result_t result = transfer();
    if (result != I2C1_RESULT_OK) return result;
    writes++;
    if (!ignore_write) regs[reg] = value;
    return result;
}
i2c1_result_t i2c1_read_registers(uint8_t address, uint8_t reg, uint8_t *value, uint8_t count, uint32_t timeout)
{
    assert(address == 0x68U && reg == 0x3BU && count == 6U && timeout == 10000U);
    bursts++;
    i2c1_result_t result = transfer();
    if (result == I2C1_RESULT_OK) memcpy(value, &regs[reg], count);
    return result;
}
void board_io_init(void) { }
void board_io_set_safe_outputs(void) { }
void hcsr04_init(void) { }
void hcsr04_measure(hcsr04_measurement_t *value) { (void)value; assert(0); }
void uart2_init(void) { }
uint8_t uart2_try_read_byte(uint8_t *byte)
{
    if (*input == '\0') return 0U;
    *byte = (uint8_t)*input++;
    return 1U;
}
void uart2_write_text(const char *text)
{
    assert(strlen(output) + strlen(text) < sizeof output);
    strcat(output, text);
}
void uart2_write_u32(uint32_t value)
{
    char text[12];
    snprintf(text, sizeof text, "%lu", (unsigned long)value);
    uart2_write_text(text);
}
static void request(const char *command, const char *response)
{
    output[0] = '\0'; input = command; app_state_run();
    assert(strcmp(output, response) == 0);
}
static void reset(void)
{
    memset(regs, 0, sizeof regs);
    regs[0x75] = 0x72U;
    regs[0x6B] = 0x69U; /* SLEEP/CYCLE/TEMP_DIS/CLKSEL. */
    regs[0x6C] = 0xFFU;
    regs[0x1C] = 0xF8U;
    regs[0x1A] = 0x38U;
    regs[0x38] = 0x10U;
    regs[0x3B] = 0x40U; regs[0x3D] = 0xC0U; regs[0x3F] = 0x80U;
    now = 0U; operations = writes = bursts = 0;
    fail_at = -1; ignore_write = no_data = 0; injected = I2C1_RESULT_NACK;
    input = ""; output[0] = '\0';
    app_state_init(); app_state_run();
}
int main(void)
{
    reset();
    request("READ_ACCEL\r\n", "ERR,READ_ACCEL,NOT_CONFIGURED\r\n");
    request("CONFIG_ACCEL\r\n", "OK,CONFIG_ACCEL,2G\r\n");
    assert(writes == 6 && operations == 19);
    assert(regs[0x6B] == 0x08U && regs[0x6C] == 0xC7U && regs[0x1C] == 0U);
    assert(regs[0x1A] == 3U && regs[0x19] == 9U && regs[0x38] == 0x11U);
    request("READ_ACCEL\r\n", "ERR,READ_ACCEL,WARMING_UP\r\n");
    now += 100000U;
    request("READ_ACCEL\r\n", "OK,READ_ACCEL,16384,-16384,-32768\r\n");
    assert(bursts == 1);
    regs[0x3B] = 0x7FU; regs[0x3C] = 0xFFU;
    regs[0x3D] = 0xFFU; regs[0x3E] = 0xFFU;
    regs[0x3F] = 0U; regs[0x40] = 1U;
    request("READ_ACCEL\r\n", "OK,READ_ACCEL,32767,-1,1\r\n");
    puts("PASS: configure/read commands, register preservation, big-endian signed axes, UART response");
    host_tcp_connected = false;
    host_tcp_session++;
    input = "";
    app_state_run();
    mpu6050_accel_t independent_sample;
    const char *independent_error = mpu6050_accel_read(&independent_sample);
    assert(independent_error == NULL);
    assert(independent_sample.x == 32767);
    host_tcp_connected = true;
    host_tcp_session++;
    app_state_run();
    puts("PASS: idle TCP disconnect preserves the independent sensor diagnostic configuration");

    for (int operation = 1; operation <= 19; operation++)
    {
        reset(); fail_at = operation;
        request("CONFIG_ACCEL\r\n", "ERR,CONFIG_ACCEL,NACK\r\n");
        assert(operations == operation);
        request("READ_ACCEL\r\n", "ERR,READ_ACCEL,NOT_CONFIGURED\r\n");
        request("PING\r\n", "OK,PING,PONG\r\n");
    }
    reset(); regs[0x75] = 0x68U;
    request("CONFIG_ACCEL\r\n", "ERR,CONFIG_ACCEL,ID_MISMATCH\r\n"); assert(writes == 0);
    reset(); regs[0x6B] |= (0x1U << 7); /* DEVICE_RESET. */
    request("CONFIG_ACCEL\r\n", "ERR,CONFIG_ACCEL,SENSOR_RESET\r\n"); assert(writes == 0);
    reset(); ignore_write = 1;
    request("CONFIG_ACCEL\r\n", "ERR,CONFIG_ACCEL,VERIFY_FAILED\r\n");

    mpu6050_accel_t sample;
    for (int operation = 1; operation <= 9; operation++)
    {
        reset(); assert(mpu6050_accel_configure() == NULL); now += 100000U;
        fail_at = operations + operation;
        sample = (mpu6050_accel_t){123, 456, 789};
        assert(strcmp(mpu6050_accel_read(&sample), "NACK") == 0);
        assert(sample.x == 123 && sample.y == 456 && sample.z == 789);
        assert(strcmp(mpu6050_accel_read(&sample), "NOT_CONFIGURED") == 0);
    }
    reset(); assert(mpu6050_accel_configure() == NULL); now += 100000U; no_data = 1;
    uint32_t start = now;
    request("READ_ACCEL\r\n", "ERR,READ_ACCEL,DATA_TIMEOUT\r\n");
    assert(now - start <= 32000U && bursts == 0);
    request("PING\r\n", "OK,PING,PONG\r\n");
    reset(); assert(mpu6050_accel_configure() == NULL); now += 100000U; regs[0x1C] = 8U;
    request("READ_ACCEL\r\n", "ERR,READ_ACCEL,CONFIG_CHANGED\r\n"); assert(bursts == 0);
    reset(); now = 0xFFFF0000U; assert(mpu6050_accel_configure() == NULL); now += 100000U;
    request("READ_ACCEL\r\n", "OK,READ_ACCEL,16384,-16384,-32768\r\n");
    request("START\r\n", "AUTO\r\n");
    request("CONFIG_ACCEL\r\n", "ERR,CONFIG_ACCEL,INVALID_STATE\r\n");
    request("READ_ACCEL\r\n", "ERR,READ_ACCEL,INVALID_STATE\r\n");
    puts("PASS: every transfer failure, reset/identity/readback guards, stale config, data deadline, wrap, IDLE restriction");
    return 0;
}
