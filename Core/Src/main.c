/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 共享洗衣設備狀態追蹤與逾時管理系統 (PC13 PullDown 最終完整版)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STATE_IDLE = 0,     // 閒置 (綠燈)
    STATE_WASHING,      // 洗衣中 (黃燈)
    STATE_DONE,         // 完成/待取衣 (PB6 / TIM4_CH1 藍色 PWM 呼吸燈)
    STATE_OVERDUE       // 逾時未取 (紅燈)
} SystemState_t;

typedef struct {
    char id[8];
    int is_restricted;  // 1 代表逾時遭限制，0 代表正常
} User_t;
/* USER CODE END PTD */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;
TIM_HandleTypeDef htim4;

/* USER CODE BEGIN PV */
uint8_t rx_byte;
char rx_buffer[32];
uint8_t rx_index = 0;

volatile SystemState_t current_state = STATE_IDLE;
char current_user[8] = "NONE";

// EXTI 按鈕觸發旗標 (volatile 確保中斷與主迴圈同步)
volatile uint8_t button_pressed_flag = 0;

User_t users[3] = {
    {"U01", 0},
    {"U02", 0},
    {"U03", 0}
};

uint32_t wash_duration_ms = 15000;   // 洗衣時間 15 秒 (測試用)
uint32_t overdue_limit_ms = 10000;    // 逾時限制 10 秒 (測試用)
uint32_t timer_start_tick = 0;

// 呼吸燈控制變數
uint32_t last_breath_tick = 0;
int16_t breath_duty = 0;
int8_t breath_step = 25;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM4_Init(void);

/* USER CODE BEGIN PFP */
void UART_SendString(const char *str);
void Process_Command(char *cmd);
void Update_LEDs(SystemState_t state);
void Update_Breathing_LEDs(void);
int Find_User(const char *id);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void UART_SendString(const char *str) {
    HAL_UART_Transmit(&hlpuart1, (uint8_t*)str, strlen(str), HAL_MAX_DELAY);
}

int Find_User(const char *id) {
    for (int i = 0; i < 3; i++) {
        if (strcmp(users[i].id, id) == 0) return i;
    }
    return -1;
}

void Update_LEDs(SystemState_t state) {
    // 關閉所有一般 LED
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);  // 綠 (IDLE)
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);  // 黃 (WASHING)
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET); // 紅 (OVERDUE)

    // 先停止 TIM4 PWM
    HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0);

    switch (state) {
        case STATE_IDLE:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
            break;
        case STATE_WASHING:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
            break;
        case STATE_DONE:
            breath_duty = 0;
            breath_step = 25;
            last_breath_tick = HAL_GetTick();

            __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0);

            if (HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1) != HAL_OK)
            {
                UART_SendString("[ERROR] PWM start failed!\r\n");
            }
            else
            {
                UART_SendString("[OK] Blue breathing mode started.\r\n");
            }
            break;
        case STATE_OVERDUE:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
            break;
    }
}

void Update_Breathing_LEDs(void)
{
    if (current_state != STATE_DONE)
    {
        return;
    }

    uint32_t now = HAL_GetTick();

    if ((now - last_breath_tick) >= 10)
    {
        last_breath_tick = now;

        breath_duty += breath_step;

        if (breath_duty >= 999)
        {
            breath_duty = 999;
            breath_step = -25;
        }
        else if (breath_duty <= 0)
        {
            breath_duty = 0;
            breath_step = 25;
        }

        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, breath_duty);
    }
}

void Process_Command(char *cmd) {
    cmd[strcspn(cmd, "\r\n")] = 0;
    if (strlen(cmd) == 0) return;

    char response[64];

    if (strncmp(cmd, "USER ", 5) == 0) {
        char *id = cmd + 5;
        int idx = Find_User(id);
        if (idx != -1) {
            strcpy(current_user, id);
            snprintf(response, sizeof(response), "[OK] User %s authenticated. Restricted: %s\r\n",
                     current_user, users[idx].is_restricted ? "YES" : "NO");
            UART_SendString(response);
        } else {
            UART_SendString("[ERROR] User not found (Use U01, U02, U03).\r\n");
        }
    }
    else if (strcmp(cmd, "CLOSE") == 0) {
        if (strcmp(current_user, "NONE") == 0) {
            UART_SendString("[ERROR] Please authenticate first using 'USER <ID>'.\r\n");
            return;
        }
        int idx = Find_User(current_user);
        if (idx != -1 && users[idx].is_restricted) {
            UART_SendString("[ERROR] Access Denied! You are restricted due to previous overdue violation.\r\n");
            return;
        }

        if (current_state == STATE_IDLE) {
            current_state = STATE_WASHING;
            timer_start_tick = HAL_GetTick();
            Update_LEDs(current_state);
            snprintf(response, sizeof(response), "[OK] Washing started for %s (15s).\r\n", current_user);
            UART_SendString(response);
        } else {
            UART_SendString("[ERROR] Device is busy.\r\n");
        }
    }
    else if (strcmp(cmd, "TAKE") == 0) {
        if (current_state == STATE_WASHING) {
            current_state = STATE_IDLE;
            Update_LEDs(current_state);
            snprintf(response, sizeof(response), "[INFO] Washing cancelled by user %s. Reset to IDLE.\r\n", current_user);
            UART_SendString(response);
            strcpy(current_user, "NONE");
        }
        else if (current_state == STATE_DONE || current_state == STATE_OVERDUE) {
            snprintf(response, sizeof(response), "[INFO] User %s took clothes. Reset to IDLE.\r\n", current_user);
            UART_SendString(response);
            current_state = STATE_IDLE;
            strcpy(current_user, "NONE");
            Update_LEDs(current_state);
        } else {
            UART_SendString("[WARN] Cannot take clothes in current state.\r\n");
        }
    }
    else if (strncmp(cmd, "UNLOCK ", 7) == 0) {
        char *id = cmd + 7;
        int idx = Find_User(id);
        if (idx != -1) {
            users[idx].is_restricted = 0;
            snprintf(response, sizeof(response), "[OK] User %s restriction removed.\r\n", id);
            UART_SendString(response);
        } else {
            UART_SendString("[ERROR] User not found.\r\n");
        }
    }
    else if (strcmp(cmd, "STATUS") == 0) {
        snprintf(response, sizeof(response), "[STATUS] State: %d, User: %s\r\n", current_state, current_user);
        UART_SendString(response);
    }
    else {
        UART_SendString("[ERROR] Unknown command.\r\n");
    }
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_LPUART1_UART_Init();
  MX_TIM4_Init();

  Update_LEDs(STATE_IDLE);

  UART_SendString("\r\n=== Smart Washing System Ready (PC13 PullDown + EXTI RISING) ===\r\n");

  HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);

  while (1)
  {
      // 1. 執行非阻塞藍色呼吸燈更新
      Update_Breathing_LEDs();

      // 2. 響應 EXTI 按鈕中斷旗標
      if (button_pressed_flag) {
          button_pressed_flag = 0;

          if (current_state == STATE_WASHING) {
              current_state = STATE_IDLE;
              Update_LEDs(current_state);
              UART_SendString("[INFO] EXTI Button: Washing cancelled! Back to IDLE.\r\n");
              strcpy(current_user, "NONE");
          }
          else if (current_state == STATE_DONE || current_state == STATE_OVERDUE) {
              current_state = STATE_IDLE;
              Update_LEDs(current_state);
              UART_SendString("[INFO] EXTI Button: Clothes taken! Back to IDLE.\r\n");
              strcpy(current_user, "NONE");
          }
      }

      uint32_t current_tick = HAL_GetTick();

      // 3. 狀態機逾時與自動切換
      if (current_state == STATE_WASHING) {
          if ((current_tick - timer_start_tick) >= wash_duration_ms) {
              current_state = STATE_DONE;
              timer_start_tick = current_tick;
              Update_LEDs(current_state);
              UART_SendString("[INFO] Washing done! PB6 Blue breathing LED ON.\r\n");
          }
      }
      else if (current_state == STATE_DONE) {
          if ((current_tick - timer_start_tick) >= overdue_limit_ms) {
              current_state = STATE_OVERDUE;
              Update_LEDs(current_state);

              int idx = Find_User(current_user);
              if (idx != -1) {
                  users[idx].is_restricted = 1;
              }
              UART_SendString("[WARN] Overdue! Red LED ON. User is now RESTRICTED!\r\n");
          }
      }
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK
                              |RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1
                              |RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_SYSCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_LPUART1_UART_Init(void)
{
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_TIM4_Init(void)
{
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 170 - 1;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 1000 - 1;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_TIM_MspPostInit(&htim4);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10, GPIO_PIN_RESET);

  // 1. PC13 設定為 PULLDOWN 內部下拉，搭配 EXTI 上升沿 (RISING) 觸發
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  // 2. 設定 PB0(綠), PB1(黃), PB10(紅) 為一般推挽輸出
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // 3. 啟用 EXTI15_10 中斷通道與優先權
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == LPUART1) {
        if (rx_byte == '\r' || rx_byte == '\n') {
            rx_buffer[rx_index] = '\0';
            Process_Command(rx_buffer);
            rx_index = 0;
        } else {
            if (rx_index < sizeof(rx_buffer) - 1) {
                rx_buffer[rx_index++] = rx_byte;
            }
        }
        HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
    }
}

// 外部中斷回呼函數 (搭配 200ms 軟體去彈跳)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13)
    {
        button_pressed_flag = 1;
    }
}


void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

