/* 실제 i2c1.c/protocol.c/app_state.c를 가짜 주변장치와 연결하는 호스트 시험.
 * 전기적 동작, 파형, 실제 시간 정확도 및 실기 인터럽트 타이밍은 검증하지 않는다. */
#include "stm32f401xe.h"
#include "i2c1.h"
#include "app_state.h"
#include "hcsr04.h"
#include "timebase.h"
#include "uart2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

host_i2c_t host_i2c;
host_rcc_t host_rcc;
host_gpio_t host_gpio;
host_timer_t host_timer;
uint32_t SystemCoreClock = 16000000U;
const uint8_t APBPrescTable[8] = {0, 0, 0, 0, 1, 2, 3, 4};

enum stage { START, WRITE_ADDRESS, REGISTER, RESTART, READ_ADDRESS, RECEIVE, STOP, DONE, WRITE_DATA };
static enum stage stage;
static int stall_stage;
static int error_stage;
static uint32_t error_bits;
static uint32_t now_us, tick_us, ticks, primask, critical_count;
static uint8_t sensor_id;
static int write_mode;
static uint8_t expected_write_value, written_value;
static int wake_mode, wake_transaction, wake_error_transaction, wake_stall_transaction;
static int ignore_sensor_write, sensor_write_count;
static uint8_t sensor_power, readback_xor;
static int persistent_busy;
static int safe_output_count;
static int i2c_init_count, ultrasonic_init_count, ultrasonic_measure_count;
static hcsr04_measurement_t simulated_ultrasonic;
static const char *input;
static char output[1024];

void SystemCoreClockUpdate(void) { SystemCoreClock = 16000000U; i2c_init_count++; }
uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1U; critical_count++; }
void __set_PRIMASK(uint32_t value)
{
    /* 수신 주소 ACK 이후 ACK/POS=0, STOP=1인 채로 임계구간을 빠져나와야 한다. */
    assert(stage == RECEIVE);
    assert((I2C1->CR1 & ((0x1U << 10) | (0x1U << 11))) == 0U);
    assert((I2C1->CR1 & (0x1U << 9)) != 0U);
    primask = value;
}

static void advance_device(void)
{
    /* SLEEP 명령은 읽기/쓰기/재읽기의 세 전송을 같은 모의 센서에서 수행한다. */
    if (wake_mode && stage == DONE && ((I2C1->CR1 & (0x1U << 8)) != 0U))
    {
        wake_transaction++;
        assert(wake_transaction <= 2);
        write_mode = wake_transaction == 1;
        critical_count = 0U;
        stage = START;
    }
    if (wake_mode && wake_transaction == wake_stall_transaction) return;
    if (wake_mode && wake_transaction == wake_error_transaction)
    {
        error_stage = WRITE_ADDRESS;
        wake_error_transaction = -1;
    }
    if (persistent_busy) { I2C1->SR2 = (0x1U << 1); return; } /* SR2 bit 1: BUSY. */
    if ((int)stage == stall_stage) return;
    /* 전송 중 오류 정리가 STOP을 요청하면 남은 주소/데이터 단계는 진행하지 않는다.
     * 정상 1바이트 수신은 STOP이 먼저 예약되므로 RECEIVE 단계만 예외다. */
    if (((I2C1->CR1 & (0x1U << 9)) != 0U) && (stage != RECEIVE))
    {
        I2C1->CR1 &= ~(0x1U << 9); /* CR1 bit 9: STOP 완료. */
        I2C1->SR1 = I2C1->SR2 = 0U;
        stage = DONE;
        return;
    }
    if ((int)stage == error_stage)
    {
        I2C1->SR1 = error_bits;
        /* SR1 bit 9(ARLO)을 모의하면 컨트롤러 지위를 잃고 SR2 bit 0(MSL)=0. */
        if ((error_bits & (0x1U << 9)) != 0U) I2C1->SR2 = (0x1U << 1);
        error_stage = -1;
        stage = STOP;
        return;
    }
    switch (stage)
    {
    case START:
    case RESTART:
        if ((I2C1->CR1 & (0x1U << 8)) != 0U) /* CR1 bit 8: START. */
        {
            I2C1->CR1 &= ~(0x1U << 8);
            I2C1->SR1 = (0x1U << 0); /* SR1 bit 0: SB. */
            I2C1->SR2 = (0x1U << 0) | (0x1U << 1); /* MSL, BUSY. */
            stage = stage == START ? WRITE_ADDRESS : READ_ADDRESS;
        }
        break;
    case WRITE_ADDRESS:
        assert(I2C1->DR == 0xD0U);
        I2C1->SR1 = (0x1U << 1); /* SR1 bit 1: ADDR. */
        stage = REGISTER;
        break;
    case REGISTER:
        if (I2C1->DR == 0xD0U) I2C1->SR1 = (0x1U << 7); /* SR1 bit 7: TxE. */
        else
        {
            assert(I2C1->DR == ((write_mode || wake_mode) ? 0x6BU : 0x75U));
            I2C1->SR1 = (0x1U << 2); /* SR1 bit 2: BTF. */
            stage = write_mode ? WRITE_DATA : RESTART;
        }
        break;
    case WRITE_DATA:
        assert(I2C1->DR == expected_write_value);
        written_value = (uint8_t)I2C1->DR;
        if (wake_mode)
        {
            sensor_write_count++;
            if (!ignore_sensor_write) sensor_power = written_value;
        }
        I2C1->SR1 = (0x1U << 2); /* SR1 bit 2(BTF): 데이터 ACK 후 완료. */
        stage = STOP;
        break;
    case READ_ADDRESS:
        assert(I2C1->DR == 0xD1U);
        I2C1->SR1 = (0x1U << 1); /* SR1 bit 1: ADDR. */
        stage = RECEIVE;
        break;
    case RECEIVE:
        assert(critical_count == 1U);
        I2C1->DR = wake_mode ? (sensor_power ^ (wake_transaction == 2 ? readback_xor : 0U)) : sensor_id;
        I2C1->SR1 = (0x1U << 6); /* SR1 bit 6: RxNE. */
        stage = STOP;
        break;
    case STOP:
        I2C1->CR1 &= ~(0x1U << 9); /* CR1 bit 9: STOP 완료. */
        I2C1->SR1 = 0U;
        I2C1->SR2 = 0U;
        stage = DONE;
        break;
    case DONE: break;
    }
}

void timebase_init(void)
{
    RCC->APB1ENR |= (0x1U << 3); /* APB1ENR bit 3: TIM5EN. */
    TIM5->CR1 = (0x1U << 0); /* TIM5 CR1 bit 0: CEN. */
}
uint32_t timebase_now_us(void) { return now_us; }
uint32_t timebase_elapsed_us(uint32_t start)
{
    assert(++ticks < 5000U); /* 실제 코드의 무한 대기를 모의 시험에서도 검출한다. */
    now_us += tick_us;
    advance_device();
    return now_us - start;
}
void board_io_init(void) { }
void board_io_set_safe_outputs(void) { safe_output_count++; }
void hcsr04_init(void) { ultrasonic_init_count++; }
void hcsr04_measure(hcsr04_measurement_t *m) { ultrasonic_measure_count++; *m = simulated_ultrasonic; }
void uart2_init(void) { }
uint8_t uart2_try_read_byte(uint8_t *byte)
{
    if (*input == '\0') return 0U;
    *byte = (uint8_t)*input++;
    return 1U;
}
void uart2_write_byte(uint8_t byte)
{
    size_t length = strlen(output);
    assert(length + 1U < sizeof(output));
    output[length] = (char)byte;
    output[length + 1U] = '\0';
}
void uart2_write_text(const char *text) { while (*text) uart2_write_byte((uint8_t)*text++); }
void uart2_write_u32(uint32_t value)
{
    char number[11];
    snprintf(number, sizeof(number), "%lu", (unsigned long)value);
    uart2_write_text(number);
}

static void reset_device(void)
{
    memset(&host_i2c, 0, sizeof(host_i2c));
    memset(&host_rcc, 0, sizeof(host_rcc));
    memset(&host_timer, 0, sizeof(host_timer));
    stage = START;
    stall_stage = error_stage = -1;
    error_bits = 0U;
    now_us = ticks = primask = critical_count = 0U;
    tick_us = 10U;
    sensor_id = 0x68U;
    write_mode = 0;
    expected_write_value = written_value = 0U;
    wake_mode = wake_transaction = ignore_sensor_write = sensor_write_count = 0;
    wake_error_transaction = wake_stall_transaction = -1;
    sensor_power = 0x40U;
    readback_xor = 0U;
    persistent_busy = safe_output_count = 0;
    i2c_init_count = ultrasonic_init_count = ultrasonic_measure_count = 0;
    simulated_ultrasonic = (hcsr04_measurement_t){ HCSR04_STATUS_TIMEOUT, 0U, 0U };
    input = "";
    output[0] = '\0';
    timebase_init();
    assert(i2c1_init());
}

static void check_failed(i2c1_result_t expected)
{
    uint8_t value = 0xAAU;
    uint32_t start = now_us;
    assert(i2c1_read_register(0x68U, 0x75U, &value, 200U) == expected);
    assert(value == 0xAAU);
    assert(now_us - start <= 1200U);
}

static void request(const char *command, const char *expected)
{
    output[0] = '\0';
    input = command;
    app_state_run();
    assert(strcmp(output, expected) == 0);
}

static void prepare_wake(uint8_t power)
{
    reset_device();
    app_state_init();
    app_state_run();
    wake_mode = 1;
    sensor_power = power;
    expected_write_value = power & ~(0x1U << 6); /* PWR_MGMT_1 bit 6(SLEEP). */
}

int main(void)
{
    uint8_t value;
    setvbuf(stdout, NULL, _IONBF, 0);
    /* 쓰기 전송: 주소/레지스터/데이터 순서, 래핑, 오류와 유한 대기를 확인한다.
     * 0x6B는 전송 시험용 주소이며 실제 센서 설정 적용 시험이 아니다. */
    const uint8_t write_values[] = { 0x00U, 0x01U, 0x6BU, 0xD0U, 0xFFU };
    for (unsigned i = 0; i < sizeof(write_values); i++)
    {
        reset_device();
        write_mode = 1;
        expected_write_value = write_values[i];
        now_us = 0xFFFFFFD0U;
        primask = i & 1U;
        assert(i2c1_write_register(0x68U, 0x6BU, expected_write_value, 200U) == I2C1_RESULT_OK);
        assert(stage == DONE && written_value == expected_write_value);
        assert(critical_count == 0U && primask == (i & 1U));
    }
    const int write_phases[] = { START, WRITE_ADDRESS, REGISTER, WRITE_DATA, STOP };
    for (unsigned i = 0; i < sizeof(write_phases) / sizeof(write_phases[0]); i++)
    {
        reset_device();
        write_mode = 1;
        stall_stage = write_phases[i];
        assert(i2c1_write_register(0x68U, 0x6BU, 0U, 200U) == I2C1_RESULT_TIMEOUT);
        assert(now_us <= 1200U);
        assert(I2C1->CR2 == 16U && I2C1->CCR == 80U && I2C1->TRISE == 17U);
        assert(I2C1->CR1 == (0x1U << 0)); /* CR1 bit 0(PE): 리셋 후 활성화. */
    }
    for (int bit = 8; bit <= 11; bit++) /* SR1 bit 8=BERR, 9=ARLO, 10=AF, 11=OVR. */
    {
        const i2c1_result_t expected[] = { I2C1_RESULT_BUS_ERROR,
            I2C1_RESULT_ARBITRATION_LOST, I2C1_RESULT_NACK, I2C1_RESULT_OVERRUN };
        for (unsigned phase = 1; phase <= 3; phase++)
        {
            reset_device();
            write_mode = 1;
            error_stage = write_phases[phase];
            error_bits = 0x1U << bit;
            assert(i2c1_write_register(0x68U, 0x6BU, 0U, 200U) == expected[bit - 8]);
            assert(now_us <= 1200U);
            /* 같은 드라이버로 다음 쓰기를 다시 수행할 수 있는지 확인. */
            /* 가짜 레지스터는 RCC 리셋에 반응하지 않으므로 리셋 결과를 모의한다.
             * 외부 버스도 해제된 조건이며, 실제 리셋 동작 검증은 아니다. */
            I2C1->SR1 = I2C1->SR2 = 0U;
            stage = START;
            assert(i2c1_write_register(0x68U, 0x6BU, 0U, 200U) == I2C1_RESULT_OK);
        }
    }
    reset_device();
    assert(i2c1_write_register(0x80U, 0x6BU, 0U, 200U) == I2C1_RESULT_INVALID_ARGUMENT);
    assert(i2c1_write_register(0x68U, 0x6BU, 0U, 0U) == I2C1_RESULT_INVALID_ARGUMENT);
    assert(i2c1_write_register(0x68U, 0x6BU, 0U, 0x80000000U) == I2C1_RESULT_INVALID_ARGUMENT);
    TIM5->CR1 = 0U;
    assert(i2c1_write_register(0x68U, 0x6BU, 0U, 200U) == I2C1_RESULT_NOT_READY);
    reset_device();
    persistent_busy = 1;
    I2C1->SR2 = (0x1U << 1); /* SR2 bit 1(BUSY): 버스 점유 지속. */
    assert(i2c1_write_register(0x68U, 0x6BU, 0U, 200U) == I2C1_RESULT_BUS_BUSY);
    reset_device();
    write_mode = 1;
    tick_us = 40U;
    assert(i2c1_write_register(0x68U, 0x6BU, 0U, 200U) == I2C1_RESULT_TIMEOUT);
    puts("PASS: write bytes, deadline/wrap, errors/retry, busy and invalid arguments");
    reset_device();
    assert(i2c1_read_register(0x68U, 0x75U, NULL, 200U) == I2C1_RESULT_INVALID_ARGUMENT);
    assert(i2c1_read_register(0x80U, 0x75U, &value, 200U) == I2C1_RESULT_INVALID_ARGUMENT);
    assert(i2c1_read_register(0x68U, 0x75U, &value, 0U) == I2C1_RESULT_INVALID_ARGUMENT);
    TIM5->CR1 = 0U;
    check_failed(I2C1_RESULT_NOT_READY);
    puts("PASS: invalid arguments and stopped timebase");

    for (unsigned mask = 0; mask < 2; mask++)
    {
        reset_device();
        primask = mask;
        now_us = 0xFFFFFFD0U;
        assert(i2c1_read_register(0x68U, 0x75U, &value, 200U) == I2C1_RESULT_OK);
        assert(value == 0x68U && primask == mask && stage == DONE);
    }
    puts("PASS: complete one-byte read, clock wrap, PRIMASK restoration");

    for (int phase = START; phase <= STOP; phase++)
    {
        reset_device();
        stall_stage = phase;
        check_failed(I2C1_RESULT_TIMEOUT);
    }
    reset_device();
    tick_us = 40U;
    check_failed(I2C1_RESULT_TIMEOUT);
    puts("PASS: timeout at every phase and shared transaction deadline");

    const uint32_t errors[] = { (0x1U << 10), (0x1U << 8), (0x1U << 9), (0x1U << 11) };
    /* SR1 bit 10=AF, bit 8=BERR, bit 9=ARLO, bit 11=OVR의 오류 주입. */
    const i2c1_result_t results[] = { I2C1_RESULT_NACK, I2C1_RESULT_BUS_ERROR,
        I2C1_RESULT_ARBITRATION_LOST, I2C1_RESULT_OVERRUN };
    for (unsigned index = 0; index < 4; index++)
    {
        reset_device();
        error_stage = WRITE_ADDRESS;
        error_bits = errors[index];
        check_failed(results[index]);
        if (results[index] == I2C1_RESULT_ARBITRATION_LOST) assert(ticks == 2U);
        else
        {
            stage = START;
            critical_count = 0U;
            assert(i2c1_read_register(0x68U, 0x75U, &value, 200U) == I2C1_RESULT_OK);
        }
    }
    reset_device();
    I2C1->SR2 = (0x1U << 1); /* SR2 bit 1: 외부 BUSY 지속. */
    persistent_busy = 1;
    check_failed(I2C1_RESULT_BUS_BUSY);
    puts("PASS: NACK/BERR/ARLO/OVR, retry after cleanup, persistent BUSY");

    reset_device();
    app_state_init();
    app_state_run();
    assert(strcmp(output, "READY\r\n") == 0);
    assert(i2c_init_count == 1 && ultrasonic_init_count == 0);
    request("PING\r\n", "OK,PING,PONG\r\n");
    assert(i2c_init_count == 1 && ultrasonic_measure_count == 0);
    request("CHECK_MPU6050\r\n", "OK,CHECK_MPU6050,104\r\n");
    assert(i2c_init_count == 2 && ultrasonic_init_count == 0 && ultrasonic_measure_count == 0);
    request("GET_STATUS\r\n", "OK,GET_STATUS,IDLE,0\r\n");
    stage = START;
    critical_count = 0U;
    sensor_id = 0x70U;
    request("CHECK_MPU6050\r\n", "OK,CHECK_MPU6050,112\r\n");
    stage = START;
    critical_count = 0U;
    error_stage = WRITE_ADDRESS;
    error_bits = (0x1U << 10); /* SR1 bit 10: AF를 통한 NACK 모의. */
    request("CHECK_MPU6050\r\n", "ERR,CHECK_MPU6050,NACK\r\n");
    request("GET_STATUS\r\n", "OK,GET_STATUS,IDLE,0\r\n");
    puts("PASS: command parser -> I2C driver -> UART identity/error response");

    reset_device();
    persistent_busy = 1;
    I2C1->SR2 = (0x1U << 1); /* SR2 bit 1: 기동 시 BUSY 고장. */
    app_state_init();
    app_state_run();
    assert(safe_output_count == 0 && strcmp(output, "READY\r\n") == 0);
    assert(i2c_init_count == 1 && ultrasonic_init_count == 0);
    request("PING\r\n", "OK,PING,PONG\r\n");
    request("CHECK_MPU6050\r\n", "ERR,CHECK_MPU6050,BUS_BUSY\r\n");
    assert(ultrasonic_measure_count == 0);
    request("PING\r\n", "OK,PING,PONG\r\n");
    request("GET_STATUS\r\n", "OK,GET_STATUS,IDLE,0\r\n");
    request("CHECK_HCSR04\r\n", "OK,CHECK_HCSR04,TIMEOUT,0,0\r\n");
    assert(ultrasonic_init_count == 1 && ultrasonic_measure_count == 1);
    assert(i2c_init_count == 2);
    simulated_ultrasonic = (hcsr04_measurement_t){ HCSR04_STATUS_OK, 25U, 1450U };
    request("CHECK_HCSR04\r\n", "OK,CHECK_HCSR04,OK,25,1450\r\n");
    assert(ultrasonic_init_count == 1 && ultrasonic_measure_count == 2);
    simulated_ultrasonic = (hcsr04_measurement_t){ HCSR04_STATUS_OUT_OF_RANGE, 0U, 25000U };
    request("CHECK_HCSR04\r\n", "OK,CHECK_HCSR04,OUT_OF_RANGE,0,25000\r\n");
    request("PING\r\n", "OK,PING,PONG\r\n");
    request("GET_STATUS\r\n", "OK,GET_STATUS,IDLE,0\r\n");
    for (int index = 0; index < 10; index++) app_state_run();
    assert(ultrasonic_measure_count == 3 && i2c_init_count == 2);
    puts("PASS: lazy initialization, I2C fault isolation, one request/one ultrasonic measurement");

    /* 이전 START/STOP 호환 경로도 센서별 시험과 동시에 실행하지 않는다. */
    request("START\r\n", "AUTO\r\n");
    request("CHECK_MPU6050\r\n", "ERR,CHECK_MPU6050,INVALID_STATE\r\n");
    request("CHECK_HCSR04\r\n", "ERR,CHECK_HCSR04,INVALID_STATE\r\n");
    assert(ultrasonic_measure_count == 3);
    request("STOP\r\n", "STOP\r\n");
    request("", "IDLE\r\n");
    request("PING\r\n", "OK,PING,PONG\r\n");
    puts("PASS: legacy AUTO rejects individual sensor tests, STOP returns to IDLE");

    /* 실제 app_state -> read/write/read -> UART까지 연결하는 SLEEP 해제 시험. */
    prepare_wake(0x40U);
    now_us = 0xFFFFFFD0U;
    request("WAKE_MPU6050\r\n", "OK,WAKE_MPU6050,64,0\r\n");
    assert(wake_transaction == 2 && sensor_write_count == 1 && sensor_power == 0U);
    request("GET_STATUS\r\n", "OK,GET_STATUS,IDLE,0\r\n");
    /* bit 5(CYCLE), bit 3(TEMP_DIS), bit 2:0(CLKSEL) 보존. */
    prepare_wake(0x69U);
    request("WAKE_MPU6050\r\n", "OK,WAKE_MPU6050,105,41\r\n");
    assert(sensor_power == 0x29U && sensor_write_count == 1);
    prepare_wake(0x01U);
    request("WAKE_MPU6050\r\n", "OK,WAKE_MPU6050,1,1\r\n");
    prepare_wake(0xC0U); /* bit 7(DEVICE_RESET)=1: 쓰기 금지. */
    request("WAKE_MPU6050\r\n", "ERR,WAKE_MPU6050,SENSOR_RESET\r\n");
    assert(sensor_write_count == 0 && wake_transaction == 0);
    prepare_wake(0x40U);
    ignore_sensor_write = 1;
    request("WAKE_MPU6050\r\n", "ERR,WAKE_MPU6050,VERIFY_FAILED\r\n");
    assert(sensor_write_count == 1);
    prepare_wake(0x41U);
    readback_xor = 0x01U; /* SLEEP은 해제됐지만 CLKSEL이 바뀐 경우도 실패. */
    request("WAKE_MPU6050\r\n", "ERR,WAKE_MPU6050,VERIFY_FAILED\r\n");
    for (int transaction = 0; transaction < 3; transaction++)
    {
        prepare_wake(0x40U);
        wake_error_transaction = transaction;
        error_bits = (0x1U << 10); /* SR1 bit 10(AF): 주소 NACK. */
        request("WAKE_MPU6050\r\n", "ERR,WAKE_MPU6050,NACK\r\n");
        assert(wake_transaction == transaction);
        assert(sensor_write_count == (transaction == 2 ? 1 : 0));
        request("PING\r\n", "OK,PING,PONG\r\n");
        prepare_wake(0x40U);
        wake_stall_transaction = transaction;
        request("WAKE_MPU6050\r\n", "ERR,WAKE_MPU6050,TIMEOUT\r\n");
        assert(wake_transaction == transaction && now_us <= 31000U);
        request("PING\r\n", "OK,PING,PONG\r\n");
    }
    prepare_wake(0x40U);
    TIM5->CR1 = 0U;
    request("WAKE_MPU6050\r\n", "ERR,WAKE_MPU6050,NOT_READY\r\n");
    assert(sensor_write_count == 0);
    prepare_wake(0x40U);
    I2C1->SR2 = (0x1U << 1); /* SR2 bit 1(BUSY): 외부 버스 점유. */
    persistent_busy = 1;
    request("WAKE_MPU6050\r\n", "ERR,WAKE_MPU6050,BUS_BUSY\r\n");
    assert(sensor_write_count == 0);
    prepare_wake(0x40U);
    request("START\r\n", "AUTO\r\n");
    request("WAKE_MPU6050\r\n", "ERR,WAKE_MPU6050,INVALID_STATE\r\n");
    assert(sensor_write_count == 0 && ticks == 0U);
    puts("PASS: wake command read/write/verify, preserved bits, reset guard, failures and IDLE restriction");
    return 0;
}
