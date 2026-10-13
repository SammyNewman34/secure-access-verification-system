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

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_print(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), 100);
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

  {
      char b[64];
      rc522_write_reg(0x14, rc522_read_reg(0x14) | 0x03);
      snprintf(b, sizeof b, "TxCtl after antenna on = 0x%02X\r\n", rc522_read_reg(0x14));
      uart_print(b);
  }

  //lcd_pins_test()

  lcd_init();
  lcd_clear();
  lcd_set_cursor(0, 0);
  lcd_print("Access System");
  lcd_set_cursor(1, 0);
  lcd_print("Ready");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  char buf[80];
	  uint8_t atqa[2], uid[5];
	  uint8_t r = rc522_reqa(atqa);
	  if (r == 0) {
	      uint8_t a = rc522_anticoll(uid);
	      if (a == 0)
	    	  snprintf(buf, sizeof buf, "UID = %02X %02X %02X %02X -> %s\r\n",
	    	           uid[0], uid[1], uid[2], uid[3],
	    	           is_authorized(uid) ? "GRANTED" : "DENIED");
	      else
	          snprintf(buf, sizeof buf, "ANTICOLL result=%u ErrorReg=0x%02X\r\n",
	                   a, rc522_read_reg(0x06));
	      uart_print(buf);
	  }
	  HAL_Delay(300);
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
  HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LCD_D5_Pin|LCD_D4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_E_GPIO_Port, LCD_E_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LCD_D6_Pin|LCD_RS_Pin|LCD_D7_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

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

  /*Configure GPIO pin : LCD_E_Pin */
  GPIO_InitStruct.Pin = LCD_E_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_E_GPIO_Port, &GPIO_InitStruct);

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
