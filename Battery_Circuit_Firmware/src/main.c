/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (Methodology Compliant)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// --- Safety Thresholds & Configurations ---
#define VOLTAGE_MAX_MV       17500   // 17.5V Overcharge threshold
#define VOLTAGE_MIN_MV       15000   // 15.0V Deep discharge threshold
#define ADC_MAX_MV_RANGE     25000   // Assumes divider scales 25V down to 3.3V
#define TEMP_MAX_45C_ADC     1500    // Thermistor threshold (NTC resistance drops as heat rises)
#define ADC_TIMEOUT_LIMIT    5000    // Timeout counter limit for ADC polling (Rule 6)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief Reads a specific ADC channel dynamically.
 * Rule 1: No break/goto (uses readComplete flag).
 * Rule 3: Function body < 50 Lines.
 * Rule 6: Uses a safety timeout counter instead of an infinite loop.
 */
uint16_t readADCChannel(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    
    uint16_t reading = 0;
    uint16_t timeoutCounter = 0;
    uint8_t readComplete = 0; 
    
    // Safety timeout loop (Rule 6)
    while ((readComplete == 0) && (timeoutCounter < ADC_TIMEOUT_LIMIT)) {
        if (HAL_ADC_PollForConversion(&hadc1, 1) == HAL_OK) {
            reading = HAL_ADC_GetValue(&hadc1);
            readComplete = 1; 
        } else {
            timeoutCounter++;
        }
    }
    
    HAL_ADC_Stop(&hadc1);
    return reading;
}

/**
 * @brief Executes a hardware-level shutdown.
 * Rule 3: Function body < 50 Lines.
 * Rule 7: Hardware stop saves lives. Drops latch pin to physically cut power.
 */
void executeEmergencyStop(void) {
    // Cut main power via MOSFET (PF2)
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_RESET); 
    
    // Turn ON Red Fault LED (PA6)
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    
    // Turn OFF all Green LEDs (PA2-PA5) to signal shutdown
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);
}

/**
 * @brief Maps battery voltage to the 4-Green LED array output (25% increments).
 * Rule 3: Function body < 50 Lines.
 * Rule 4: Logic kept completely flat (never exceeds 1 nested layer).
 */
void updateLedDisplay(uint32_t voltageMv) {
    uint8_t ledsToLight = 0;
    
    // Flat logic evaluation
    if (voltageMv >= 17000)      { ledsToLight = 4; } // 100%
    else if (voltageMv >= 16500) { ledsToLight = 3; } // 75%
    else if (voltageMv >= 16000) { ledsToLight = 2; } // 50%
    else if (voltageMv >= 15500) { ledsToLight = 1; } // 25%
    
    // Update Pins
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, (ledsToLight >= 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, (ledsToLight >= 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, (ledsToLight >= 3) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, (ledsToLight >= 4) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    
    // Ensure Red LED is OFF and Power is ON during normal operation
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET); 
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_SET);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();

  /* USER CODE BEGIN 2 */
  // Assert power hold immediately on startup to keep the board alive
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_SET);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 1. Read Sensors
    uint16_t rawBattAdc = readADCChannel(ADC_CHANNEL_0); // Battery Voltage Divider
    uint16_t rawTempAdc = readADCChannel(ADC_CHANNEL_1); // NTC Thermistor Divider
    
    // 2. Convert ADC to Millivolts (Using 32-bit math to prevent overflow)
    uint32_t currentVoltageMv = ((uint32_t)rawBattAdc * ADC_MAX_MV_RANGE) / 4095; 
    
    // 3. Flat State Evaluation (Rule 4)
    uint8_t faultDetected = 0;
    
    if (currentVoltageMv > VOLTAGE_MAX_MV) { faultDetected = 1; } // Overcharge
    if (currentVoltageMv < VOLTAGE_MIN_MV) { faultDetected = 1; } // Under-discharge
    if (rawTempAdc < TEMP_MAX_45C_ADC)     { faultDetected = 1; } // Overheat
    
    // 4. Act on State
    if (faultDetected == 1) {
        executeEmergencyStop();
    } else {
        updateLedDisplay(currentVoltageMv);
    }
    
    // Delay to prevent ADC thrashing and LED flickering
    HAL_Delay(100); 
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

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.LowPowerAutoPowerOff = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_1CYCLE_5;
  hadc1.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_1CYCLE_5;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level (PA2-PA6) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6, GPIO_PIN_RESET);

  /*Configure GPIO pin : PF2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pins : PA2 PA3 PA4 PA5 PA6 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */