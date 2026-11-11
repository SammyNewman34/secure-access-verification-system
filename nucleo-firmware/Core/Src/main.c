/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SERVO_MIN_US       650    // safe range: measured stops (540 / 2480) plus ~100 us margin
#define SERVO_MAX_US       2380
#define SERVO_LOCKED_US    700    // placeholder, choose after trying the positions
#define SERVO_UNLOCKED_US  2300   // placeholder, choose after trying the positions
#define UNLOCK_HOLD_MS     5000   // how long the lock stays open after a grant (my starting value)
#define RFID_POLL_MS       100    // how often the card reader is polled (same rate as before)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void uart_print(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), 100);
}

static void servo_set(uint16_t us)
{
    if (us < SERVO_MIN_US) us = SERVO_MIN_US;   // never command past the safe range
    if (us > SERVO_MAX_US) us = SERVO_MAX_US;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, us);
}

typedef struct {
    const uint16_t *steps;   // durations in ms, alternating ON, OFF, ON, ...
    uint8_t count;
} buzzer_pattern_t;

static const uint16_t PAT_GRANTED_STEPS[] = { 300 };
static const uint16_t PAT_DENIED_STEPS[]  = { 100, 100, 100, 100, 100 };
static const uint16_t PAT_TEST_STEPS[]    = { 200 };

static const buzzer_pattern_t PAT_GRANTED = { PAT_GRANTED_STEPS, 1 };
static const buzzer_pattern_t PAT_DENIED  = { PAT_DENIED_STEPS,  5 };
static const buzzer_pattern_t PAT_TEST    = { PAT_TEST_STEPS,    1 };

static const buzzer_pattern_t *buz_pat = NULL;   // NULL = idle
static uint8_t  buz_step = 0;
static uint32_t buz_step_start = 0;

static void buzzer_start(const buzzer_pattern_t *p)   // returns immediately
{
    buz_pat = p;
    buz_step = 0;
    buz_step_start = HAL_GetTick();
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);   // step 0 is always ON
}

static void buzzer_update(void)                        // call every loop pass
{
    if (buz_pat == NULL) return;
    if ((HAL_GetTick() - buz_step_start) < buz_pat->steps[buz_step]) return;

    buz_step++;
    if (buz_step >= buz_pat->count) {                  // pattern finished
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        buz_pat = NULL;
        return;
    }
    buz_step_start = HAL_GetTick();
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin,
                      (buz_step % 2 == 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);   // even = ON, odd = OFF
}

typedef enum { LOCK_LOCKED, LOCK_UNLOCKED } lock_state_t;
static lock_state_t lock_state = LOCK_LOCKED;
static uint32_t unlock_ms = 0;            // HAL_GetTick() when the lock last opened

static void lock_set(lock_state_t s)      // the only place that moves the servo for access logic
{
    lock_state = s;
    servo_set(s == LOCK_UNLOCKED ? SERVO_UNLOCKED_US : SERVO_LOCKED_US);
    if (s == LOCK_UNLOCKED) unlock_ms = HAL_GetTick();
}


static void i2c_scan(void)
{
    char b[40];
    uint8_t found = 0;
    uart_print("I2C scan...\r\n");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        // HAL wants the 7-bit address shifted left by 1; 1 try, 10 ms timeout
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1, 10) == HAL_OK) {
            snprintf(b, sizeof b, "  device at 0x%02X\r\n", addr);
            uart_print(b);
            found++;
        }
    }
    snprintf(b, sizeof b, "scan done, %u found\r\n", found);
    uart_print(b);
}


static void i2c_diag(void)
{
    char b[96];
    uint8_t scl = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_SET);
    uint8_t sda = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET);
    snprintf(b, sizeof b, "SCL=%u SDA=%u SR1=0x%04lX SR2=0x%04lX\r\n",
             scl, sda,
             (unsigned long)hi2c1.Instance->SR1,
             (unsigned long)hi2c1.Instance->SR2);
    uart_print(b);
}

static void i2c_bus_clear(void)
{
    char b[80];
    GPIO_InitTypeDef g = {0};

    HAL_I2C_DeInit(&hi2c1);                       // peripheral lets go of PB8/PB9

    g.Pin   = GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode  = GPIO_MODE_OUTPUT_OD;                // open-drain: writing 1 = release the line
    g.Pull  = GPIO_NOPULL;                        // module's 4.7k pull-ups do the pulling
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
    HAL_Delay(1);

    uint8_t sda_before = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET);
    uint8_t pulses = 0;
    while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET && pulses < 9) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET); HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);   HAL_Delay(1);
        pulses++;
    }

    // STOP condition: SDA goes low then high while SCL is high
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET); HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);   HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);   HAL_Delay(1);

    uint8_t sda_after = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET);
    snprintf(b, sizeof b, "bus clear: SDA before=%u, pulses=%u, SDA after=%u\r\n",
             sda_before, pulses, sda_after);
    uart_print(b);

    MX_I2C1_Init();                               // hand the pins back to the I2C peripheral
}


static void ds1307_dump(void)
{
    uint8_t r[7];
    char b[96];

    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c1, (0x68 << 1), 0x00,
                                            I2C_MEMADD_SIZE_8BIT, r, 7, 100);
    if (st != HAL_OK) {
        snprintf(b, sizeof b, "read failed: status=%d err=0x%02lX state=0x%02X\r\n",
                 (int)st,
                 (unsigned long)HAL_I2C_GetError(&hi2c1),
                 (unsigned)HAL_I2C_GetState(&hi2c1));
        uart_print(b);
        return;
    }

    snprintf(b, sizeof b, "DS1307 raw: %02X %02X %02X %02X %02X %02X %02X\r\n",
             r[0], r[1], r[2], r[3], r[4], r[5], r[6]);
    uart_print(b);
}

static uint8_t bcd_ok(uint8_t v, uint8_t max_bcd)
{
    return ((v & 0x0F) <= 9) && ((v >> 4) <= 9) && (v <= max_bcd);
}

static void ds1307_stats(void)
{
    uint8_t r[7];
    char b[96];
    uint16_t ok = 0, failed = 0, invalid = 0;
    uint8_t first_fail = 0;
    uint32_t last_err = 0;

    for (uint8_t i = 1; i <= 100; i++) {
        HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c1, (0x68 << 1), 0x00,
                                                I2C_MEMADD_SIZE_8BIT, r, 7, 100);
        if (st != HAL_OK) {
            failed++;
            last_err = HAL_I2C_GetError(&hi2c1);
            if (!first_fail) first_fail = i;
        } else if (!bcd_ok(r[0] & 0x7F, 0x59) ||    // seconds (mask off the CH bit)
                   !bcd_ok(r[1], 0x59) ||           // minutes
                   !bcd_ok(r[2], 0x23)) {           // hours (assumes 24 h mode)
            invalid++;
            if (!first_fail) first_fail = i;
        } else {
            ok++;
        }
        HAL_Delay(100);
    }

    snprintf(b, sizeof b, "stats/100: ok=%u failed=%u invalid=%u first_bad=%u last_err=0x%02lX\r\n",
             ok, failed, invalid, first_fail, (unsigned long)last_err);
    uart_print(b);
    i2c_diag();     // shows SCL/SDA/SR1/SR2 afterwards
}

typedef struct { uint8_t sec, min, hour, date, month, year; } rtc_time_t;   // year = 0..99

static uint8_t to_bcd(uint8_t v)   { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint8_t from_bcd(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }

// Fills t from the compile-time __DATE__ ("Nov  5 2026") and __TIME__ ("12:34:56"). Returns 1 on success.
static uint8_t parse_build_time(rtc_time_t *t)
{
    static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__, *tm = __TIME__;
    uint8_t m = 0;
    for (uint8_t i = 0; i < 12; i++)
        if (strncmp(d, mon + 3 * i, 3) == 0) m = (uint8_t)(i + 1);
    if (m == 0) return 0;
    t->month = m;
    t->date  = (uint8_t)((d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0'));
    t->year  = (uint8_t)((d[9] - '0') * 10 + (d[10] - '0'));
    t->hour  = (uint8_t)((tm[0] - '0') * 10 + (tm[1] - '0'));
    t->min   = (uint8_t)((tm[3] - '0') * 10 + (tm[4] - '0'));
    t->sec   = (uint8_t)((tm[6] - '0') * 10 + (tm[7] - '0'));
    return 1;
}

static HAL_StatusTypeDef ds1307_write_time(const rtc_time_t *t)
{
    uint8_t r[7] = { to_bcd(t->sec),      // bit 7 = 0 -> oscillator runs
                     to_bcd(t->min),
                     to_bcd(t->hour),     // bit 6 = 0 -> 24 h mode
                     1,                   // day-of-week: unused, fixed at 1
                     to_bcd(t->date),
                     to_bcd(t->month),
                     to_bcd(t->year) };
    return HAL_I2C_Mem_Write(&hi2c1, (0x68 << 1), 0x00, I2C_MEMADD_SIZE_8BIT, r, 7, 100);
}

// returns 0 = ok, 1 = I2C failure, 2 = invalid values, 3 = clock halted
static uint8_t ds1307_read_time(rtc_time_t *t)
{
    uint8_t r[7];
    if (HAL_I2C_Mem_Read(&hi2c1, (0x68 << 1), 0x00, I2C_MEMADD_SIZE_8BIT, r, 7, 100) != HAL_OK)
        return 1;
    if (r[0] & 0x80) return 3;                       // clock-halt bit set
    if (!bcd_ok(r[0], 0x59) || !bcd_ok(r[1], 0x59) || !bcd_ok(r[2], 0x23) ||   // 12 h bit also fails here
        !bcd_ok(r[4], 0x31) || !bcd_ok(r[5], 0x12) || !bcd_ok(r[6], 0x99))
        return 2;
    t->sec = from_bcd(r[0]);  t->min = from_bcd(r[1]);  t->hour = from_bcd(r[2]);
    t->date = from_bcd(r[4]); t->month = from_bcd(r[5]); t->year = from_bcd(r[6]);
    if (t->date < 1 || t->month < 1) return 2;       // 00 is not a valid date or month
    return 0;
}

static void rtc_print_status(void)
{
    rtc_time_t t;
    char b[64];
    uint8_t rc = ds1307_read_time(&t);
    if (rc == 0)
        snprintf(b, sizeof b, "RTC %04u-%02u-%02u %02u:%02u:%02u\r\n",
                 2000 + t.year, t.month, t.date, t.hour, t.min, t.sec);
    else
        snprintf(b, sizeof b, "RTC read problem: code %u (1=I2C 2=invalid 3=halted)\r\n", rc);
    uart_print(b);
}

static uint32_t rtc_ok_cnt = 0, rtc_fail_cnt = 0;

// Writes either a wall-clock stamp or an uptime stamp into out. Never fails the caller.
static void make_timestamp(char *out, size_t n, uint32_t *took_ms, uint8_t *rc_out)
{
    rtc_time_t t;
    uint32_t t0 = HAL_GetTick();
    uint8_t rc = ds1307_read_time(&t);
    *took_ms = HAL_GetTick() - t0;
    *rc_out = rc;
    if (rc == 0) {
        rtc_ok_cnt++;
        snprintf(out, n, "%04u-%02u-%02u %02u:%02u:%02u",
                 2000 + t.year, t.month, t.date, t.hour, t.min, t.sec);
    } else {
        rtc_fail_cnt++;
        uint32_t up = HAL_GetTick();
        snprintf(out, n, "up %lu.%lus", (unsigned long)(up / 1000), (unsigned long)((up % 1000) / 100));
    }
}


static uint8_t rc522_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(0x80 | (reg << 1)), 0x00 };
    uint8_t rx[2] = {0};
    HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, 100);
    HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_SET);
    return rx[1];
}

static void rc522_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), val };
    HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, tx, 2, 100);
    HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_SET);

}

static void rc522_dump_regs(void)
{
    char buf[48];
    uart_print("--- RC522 register dump ---\r\n");
    for (uint8_t r = 0; r < 0x40; r++) {
        if (r == 0x09) {
            snprintf(buf, sizeof buf, "0x%02X: skipped (FIFOData)\r\n", r);
        } else {
            snprintf(buf, sizeof buf, "0x%02X: 0x%02X\r\n", r, rc522_read_reg(r));
        }
        uart_print(buf);
    }
    uart_print("--- end ---\r\n");
}

// returns 0 = ok, 1 = timeout (no card), 2 = ErrIRq, 3 = unexpected length
static uint8_t rc522_reqa(uint8_t *atqa)
{
    rc522_write_reg(0x01, 0x00);   // Idle
    rc522_write_reg(0x04, 0x7F);   // clear all ComIrq flags
    rc522_write_reg(0x0A, 0x80);   // flush FIFO (assumed bit 7)
    rc522_write_reg(0x0D, 0x07);   // TxLastBits = 7 (7-bit frame)
    rc522_write_reg(0x09, 0x26);   // REQA into FIFO
    rc522_write_reg(0x01, 0x0C);   // Transceive
    rc522_write_reg(0x0D, 0x87);   // StartSend + TxLastBits = 7

    uint32_t t0 = HAL_GetTick();
    uint8_t irq;
    do {
        irq = rc522_read_reg(0x04);
        if (irq & 0x22) break;     // RxIRq (bit 5) or ErrIRq (bit 1)
    } while (HAL_GetTick() - t0 < 30);

    rc522_write_reg(0x0D, 0x00);   // clear StartSend
    rc522_write_reg(0x01, 0x00);   // back to Idle

    if (!(irq & 0x22))  return 1;
    if (irq & 0x02)     return 2;
    if (rc522_read_reg(0x0A) != 2) return 3;
    atqa[0] = rc522_read_reg(0x09);
    atqa[1] = rc522_read_reg(0x09);
    return 0;
}

// returns 0 = ok, 1 = timeout, 2 = ErrIRq, 3 = bad length, 4 = BCC mismatch
static uint8_t rc522_anticoll(uint8_t *uid)
{
    rc522_write_reg(0x01, 0x00);   // Idle
    rc522_write_reg(0x04, 0x7F);   // clear ComIrq flags
    rc522_write_reg(0x0A, 0x80);   // flush FIFO
    rc522_write_reg(0x0D, 0x00);   // TxLastBits = 0 (full bytes)
    rc522_write_reg(0x09, 0x93);   // SEL: cascade level 1
    rc522_write_reg(0x09, 0x20);   // NVB: no UID bits known
    rc522_write_reg(0x01, 0x0C);   // Transceive
    rc522_write_reg(0x0D, 0x80);   // StartSend

    uint32_t t0 = HAL_GetTick();
    uint8_t irq;
    do {
        irq = rc522_read_reg(0x04);
        if (irq & 0x22) break;
    } while (HAL_GetTick() - t0 < 30);

    rc522_write_reg(0x0D, 0x00);
    rc522_write_reg(0x01, 0x00);

    if (!(irq & 0x22))  return 1;
    if (irq & 0x02)     return 2;
    if ((rc522_read_reg(0x0A) & 0x7F) != 5) return 3;
    for (uint8_t i = 0; i < 5; i++) uid[i] = rc522_read_reg(0x09);
    if ((uid[0] ^ uid[1] ^ uid[2] ^ uid[3]) != uid[4]) return 4;
    return 0;
}

static const uint8_t WHITELIST[][4] = {
    {0x01, 0x17, 0x37, 0x02},   // white card
};
#define WHITELIST_COUNT (sizeof(WHITELIST) / sizeof(WHITELIST[0]))

static uint8_t is_authorized(const uint8_t *uid)
{
    for (uint32_t i = 0; i < WHITELIST_COUNT; i++)
        if (memcmp(uid, WHITELIST[i], 4) == 0) return 1;
    return 0;
}

static void lcd_pins_test(void)
{
    GPIO_TypeDef *ports[6] = { LCD_RS_GPIO_Port, LCD_E_GPIO_Port, LCD_D4_GPIO_Port,
                               LCD_D5_GPIO_Port, LCD_D6_GPIO_Port, LCD_D7_GPIO_Port };
    uint16_t pins[6] = { LCD_RS_Pin, LCD_E_Pin, LCD_D4_Pin,
                         LCD_D5_Pin, LCD_D6_Pin, LCD_D7_Pin };
    const char *names[6] = { "RS", "E", "D4", "D5", "D6", "D7" };
    char b[48];

    for (uint8_t i = 0; i < 6; i++) {
        HAL_GPIO_WritePin(ports[i], pins[i], GPIO_PIN_SET);
        HAL_Delay(5);
        uint8_t hi = (HAL_GPIO_ReadPin(ports[i], pins[i]) == GPIO_PIN_SET);
        HAL_GPIO_WritePin(ports[i], pins[i], GPIO_PIN_RESET);
        HAL_Delay(5);
        uint8_t lo = (HAL_GPIO_ReadPin(ports[i], pins[i]) == GPIO_PIN_RESET);
        snprintf(b, sizeof b, "%s: high=%u low=%u\r\n", names[i], hi, lo);
        uart_print(b);
    }
}

static void lcd_write_nibble(uint8_t nib)
{
    HAL_GPIO_WritePin(LCD_D4_GPIO_Port, LCD_D4_Pin, (nib & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_D5_GPIO_Port, LCD_D5_Pin, (nib & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_D6_GPIO_Port, LCD_D6_Pin, (nib & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_D7_GPIO_Port, LCD_D7_Pin, (nib & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_E_GPIO_Port, LCD_E_Pin, GPIO_PIN_SET);
    HAL_Delay(1);                       // generous; real minimum is microseconds
    HAL_GPIO_WritePin(LCD_E_GPIO_Port, LCD_E_Pin, GPIO_PIN_RESET);
    HAL_Delay(1);
}

static void lcd_send(uint8_t byte, uint8_t is_data)
{
    HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, is_data ? GPIO_PIN_SET : GPIO_PIN_RESET);
    lcd_write_nibble(byte >> 4);        // high nibble first
    lcd_write_nibble(byte & 0x0F);      // then low nibble
}

static void lcd_init(void)
{
    HAL_Delay(50);                      // wait after power-up
    HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, GPIO_PIN_RESET);
    lcd_write_nibble(0x03); HAL_Delay(5);
    lcd_write_nibble(0x03); HAL_Delay(1);
    lcd_write_nibble(0x03); HAL_Delay(1);
    lcd_write_nibble(0x02); HAL_Delay(1);   // switch to 4-bit mode
    lcd_send(0x28, 0);                  // function set: 4-bit, 2 lines, 5x8 font
    lcd_send(0x0C, 0);                  // display on, cursor off
    lcd_send(0x01, 0); HAL_Delay(2);    // clear display (slow command)
    lcd_send(0x06, 0);                  // entry mode: cursor moves right
}


static void lcd_print(const char *s)
{
    while (*s) lcd_send((uint8_t)*s++, 1);
}

static void lcd_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0 ? 0x00 : 0x40) + col;   // line 2 starts at 0x40 (standard HD44780 layout, from memory)
    lcd_send(0x80 | addr, 0);                         // "set DDRAM address" command
}

static void lcd_clear(void)
{
    lcd_send(0x01, 0);
    HAL_Delay(2);                                      // clear is a slow command
}


#define RESULT_HOLD_MS 2000   // keep result on screen this long after the card leaves

typedef enum { STATE_IDLE, STATE_SHOWING } ui_state_t;

static void lcd_show_idle(void)
{
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("ACCESS SYSTEM");
    lcd_set_cursor(1, 0);
    lcd_print("READY");
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(50);

  /*
  rc522_dump_regs();

  {
      char b[64];
      rc522_write_reg(0x01, 0x0F);   // CommandReg = SoftReset
      HAL_Delay(50);                 // wait for the chip to restart (my guess at a safe time)
      snprintf(b, sizeof b, "After soft reset: Cmd=0x%02X Mode=0x%02X TxCtl=0x%02X\r\n",
               rc522_read_reg(0x01), rc522_read_reg(0x11), rc522_read_reg(0x14));
      uart_print(b);
  }
  */

  //lcd_pins_test()

  lcd_init();
  lcd_clear();
  lcd_set_cursor(0, 0);
  lcd_print("ACCESS SYSTEM");
  lcd_set_cursor(1, 0);
  lcd_print("STARTING...");
  uart_print("\r\n=== Access System boot ===\r\n");
  uart_print("Build: " __DATE__ " " __TIME__ "\r\n");

  uint8_t reader_ok = 1;
  {
      rc522_write_reg(0x14, rc522_read_reg(0x14) | 0x03);   // antenna on (do not remove)
      uint8_t txctl = rc522_read_reg(0x14);
      if ((txctl & 0x03) != 0x03) {                          // only speak up if the antenna did not turn on
          reader_ok = 0;
          char b[64];
          snprintf(b, sizeof b, "READER FAULT: TxControl = 0x%02X (antenna not on)\r\n", txctl);
          uart_print(b);
      }
  }

  ui_state_t ui_state = STATE_IDLE;
  uint32_t last_seen_ms = 0;
  uint8_t last_uid[4] = {0};
  uint32_t last_poll_ms = 0;

  /* --- LOCK START --- */
  lock_set(LOCK_LOCKED);                       // set the compare value first, so the first pulse is LOCKED
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  uart_print("Lock ready: LOCKED\r\n");
  /* --- END LOCK START --- */

  buzzer_start(&PAT_TEST);                     // short boot beep (200 ms)
  HAL_Delay(1500);                             // keep the title readable (boot only, so blocking is fine)
  buzzer_update();                             // beep is long over, so this switches the buzzer off
  if (reader_ok) {
      lcd_show_idle();
      uart_print("Ready\r\n");
  } else {
      lcd_clear();
      lcd_set_cursor(0, 0);
      lcd_print("READER FAULT");
      lcd_set_cursor(1, 0);
      lcd_print("CHECK RC522");
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  char buf[80];
	  uint8_t atqa[2], uid[5];

	  /* --- DEBUG KEYS (temporary) --- */
	  uint8_t key;
	  if (HAL_UART_Receive(&huart2, &key, 1, 0) == HAL_OK) {   // timeout 0 = don't wait
	      if (key == 'l') { lock_set(LOCK_LOCKED);   uart_print("LOCKED (manual)\r\n"); }
	      if (key == 'u') { lock_set(LOCK_UNLOCKED); uart_print("UNLOCKED (manual)\r\n"); }
	      if (key == 's') i2c_scan();
	      if (key == 'd') ds1307_dump();
	      if (key == 'b') i2c_diag();
	      if (key == 'c') i2c_bus_clear();
	      if (key == 't') ds1307_stats();
	      if (key == 'z') buzzer_start(&PAT_TEST);
	      if (key == 'g') buzzer_start(&PAT_GRANTED);
	      if (key == 'n') buzzer_start(&PAT_DENIED);
	      if (key == 'w') {
	          rtc_time_t t;
	          if (parse_build_time(&t) && ds1307_write_time(&t) == HAL_OK) uart_print("RTC set from build time\r\n");
	          else uart_print("RTC set FAILED\r\n");
	          rtc_print_status();                     // read back to verify
	      }
	      if (key == 'r') rtc_print_status();
	      if (key == 'k') {
	          snprintf(buf, sizeof buf, "RTC reads: ok=%lu fail=%lu\r\n",
	                   (unsigned long)rtc_ok_cnt, (unsigned long)rtc_fail_cnt);
	          uart_print(buf);
	      }
	  }
	  /* --- END DEBUG KEYS --- */

	  if ((HAL_GetTick() - last_poll_ms) >= RFID_POLL_MS) {
	      last_poll_ms = HAL_GetTick();

	      if (rc522_reqa(atqa) == 0 && rc522_anticoll(uid) == 0) {
	          last_seen_ms = HAL_GetTick();
	          uint8_t changed = (ui_state == STATE_IDLE) || (memcmp(uid, last_uid, 4) != 0);
	          if (changed) {
	              memcpy(last_uid, uid, 4);
	              ui_state = STATE_SHOWING;
	              uint8_t granted = is_authorized(uid);

	              if (granted) lock_set(LOCK_UNLOCKED);          // decision actions first
	              lcd_clear();
	              lcd_set_cursor(0, 0);
	              lcd_print(granted ? "GRANTED" : "DENIED");
	              lcd_set_cursor(1, 0);
	              snprintf(buf, sizeof buf, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
	              lcd_print(buf);
	              buzzer_start(granted ? &PAT_GRANTED : &PAT_DENIED);

	              // RTC last: a failure here must never affect the decision (AC-18)
	              char stamp[32], logline[112];
	              uint32_t took; uint8_t rc;
	              make_timestamp(stamp, sizeof stamp, &took, &rc);
	              snprintf(logline, sizeof logline,
	                       "[%s] UID = %02X %02X %02X %02X -> %s  (rtc rc=%u, %lu ms)\r\n",
	                       stamp, uid[0], uid[1], uid[2], uid[3],
	                       granted ? "GRANTED" : "DENIED", rc, (unsigned long)took);
	              uart_print(logline);
	          }
	      } else if (ui_state == STATE_SHOWING &&
	                 (HAL_GetTick() - last_seen_ms) > RESULT_HOLD_MS) {
	          ui_state = STATE_IDLE;
	          lcd_show_idle();
	      }
	  }

	  buzzer_update();

	  if (lock_state == LOCK_UNLOCKED && (HAL_GetTick() - unlock_ms) >= UNLOCK_HOLD_MS) {
	      lock_set(LOCK_LOCKED);
	      uart_print("LOCKED (timeout)\r\n");
	  }
	  /* no HAL_Delay: each task now has its own timer */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 83;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 19999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 1500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, BUZZER_Pin|LCD_E_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LCD_D5_Pin|LCD_D4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LCD_D6_Pin|LCD_RS_Pin|LCD_D7_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BUZZER_Pin LCD_E_Pin */
  GPIO_InitStruct.Pin = BUZZER_Pin|LCD_E_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : RC522_CS_Pin LCD_D6_Pin LCD_RS_Pin LCD_D7_Pin */
  GPIO_InitStruct.Pin = RC522_CS_Pin|LCD_D6_Pin|LCD_RS_Pin|LCD_D7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : RC522_RST_Pin LCD_D5_Pin LCD_D4_Pin */
  GPIO_InitStruct.Pin = RC522_RST_Pin|LCD_D5_Pin|LCD_D4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
