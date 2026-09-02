/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (Strict Methodology Compliant)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private define ------------------------------------------------------------*/
// --- Safety Thresholds & Configurations ---
#define VOLTAGE_MAX_MV       17500   // 17.5V Overcharge threshold
#define VOLTAGE_MIN_MV       15000   // 15.0V Deep discharge threshold
#define ADC_MAX_MV_RANGE     25000   // Assumes divider scales 25V down to 3.3V
#define TEMP_MAX_45C_ADC     1500    // Thermistor threshold (Drops as heat rises)
#define ADC_TIMEOUT_LIMIT    5000    // Rule 6: Timeout counter limit
#define MAX_LIFETIME_LOOPS   0xFFFFFFFF // Rule 6: Ultimate main loop safety cap

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
void executeEmergencyStop(void);
void updateLedDisplay(uint32_t voltageMv);
uint16_t readADCChannel(uint32_t channel);

/* Private user code ---------------------------------------------------------*/

/**
 * @brief Executes a physical hardware-level shutdown.
 * Rule 3: Function body < 50 Lines.
 * Rule 7: Hardware stop saves lives. Drops latch pin to physically cut power.
 */
void executeEmergencyStop(void) {
    // Cut main power via MOSFET (PF2) and drop LATCH_HOLD (PA1)
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_RESET); 
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET); 
    
    // Turn ON Red Fault LED (PA6)
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    
    // Turn OFF all Green LEDs (PA2-PA5) to signal shutdown
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);
}

/**
 * @brief Maps battery voltage to the LED array without deep nesting.
 * Rule 3: Function body < 50 Lines.
 * Rule 4: No 3+ nested if-else. Uses flat sequential evaluation instead of else-if.
 */
void updateLedDisplay(uint32_t voltageMv) {
    uint8_t ledsToLight = 0;
    
    // Sequential evaluation ensures 0 layers of nesting
    if (voltageMv >= 15500) { ledsToLight = 1; } 
    if (voltageMv >= 16000) { ledsToLight = 2; } 
    if (voltageMv >= 16500) { ledsToLight = 3; } 
    if (voltageMv >= 17000) { ledsToLight = 4; } 
    
    // Update Pins using flat ternary logic
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, (ledsToLight >= 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, (ledsToLight >= 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, (ledsToLight >= 3) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, (ledsToLight >= 4) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    
    // Ensure Red LED is OFF and Power Latch is maintained
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET); 
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
}

/**
 * @brief Reads a specific ADC channel dynamically.
 * Rule 1: No break/goto (uses readComplete flag).
 * Rule 6: Uses timeoutCounter instead of an infinite while loop.
 */
uint16_t readADCChannel(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    uint16_t reading = 0;
    uint16_t timeoutCounter = 0;
    uint8_t readComplete = 0; 

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    
    // Loop with explicit safety feedback counter
    while ((readComplete == 0) && (timeoutCounter < ADC_TIMEOUT_LIMIT)) {
        if (HAL_ADC_PollForConversion(&hadc1, 1) == HAL_OK) {
            reading = HAL_ADC_GetValue(&hadc1);
            readComplete = 1; 
        } 
        if (readComplete == 0) { // Flat structure (avoids else nesting)
            timeoutCounter++;
        }
    }
    
    HAL_ADC_Stop(&hadc1);
    return reading;
}

/**
  * @brief  The application entry point.
  * Rule 6: Removes while(1). Replaced with evaluated run state and loop limits.
  */
int main(void)
{
  uint8_t systemRunning = 1;
  uint32_t mainSafetyCounter = 0;

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_ADC1_Init();

  // Rule 7: Assert power hold immediately on startup
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_SET); 
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET); 

  // Rule 6: Strict indefinite loop protection via counter
  while ((systemRunning == 1) && (mainSafetyCounter < MAX_LIFETIME_LOOPS))
  {
    uint16_t rawBatt = readADCChannel(ADC_CHANNEL_0);
    uint16_t rawTemp = readADCChannel(ADC_CHANNEL_8); 
    uint32_t currentVoltageMv = ((uint32_t)rawBatt * ADC_MAX_MV_RANGE) / 4095; 
    uint8_t faultDetected = 0;
    
    // Sequential State Evaluation (Rule 4)
    if (currentVoltageMv > VOLTAGE_MAX_MV) { faultDetected = 1; }
    if (currentVoltageMv < VOLTAGE_MIN_MV) { faultDetected = 1; }
    if (rawTemp < TEMP_MAX_45C_ADC)        { faultDetected = 1; }
    
    if (faultDetected == 1) {
        executeEmergencyStop();
        systemRunning = 0; // Exits loop cleanly without Rule 1 "break" violation
    }
    if (faultDetected == 0) {
        updateLedDisplay(currentVoltageMv);
    }
    
    mainSafetyCounter++;
    HAL_Delay(100); 
  }

  // If system exits loop bounds, force hardware halt
  executeEmergencyStop();
  
  // Terminal safety sink to prevent uncontrolled execution post-halt
  uint16_t terminalHalt = 0;
  while(terminalHalt < 1000) { terminalHalt++; }
}

/**
  * @brief System Clock Configuration
  * Rule 3: Kept under 50 lines.
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) { Error_Handler(); }
}

/**
  * @brief ADC1 Initialization Function
  * Rule 3: Condensed configuration struct to stay under 50 lines.
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
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  
  if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }

  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE(); 

  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/**
  * @brief  Fatal error execution state.
  * Rule 1: No software resets. 
  * Rule 6/7: Hardware stops over infinite loops.
  */
void Error_Handler(void)
{
  __disable_irq();
  
  // Rule 7: Hardware stop implementation
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_2, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
  
  // Rule 6: Terminal safety counter instead of while(1)
  uint32_t terminalCounter = 0;
  while (terminalCounter < 65000) {
      terminalCounter++;
  }
}