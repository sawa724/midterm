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

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/

/* USER CODE BEGIN PTD */

/* 定義洗衣機的四種運作狀態 */
typedef enum
{
    IDLE = 0,  // 空閒：綠燈亮
    WASHING,   // 洗衣中：黃燈亮
    WAITING,   // 洗衣完成待取：藍色呼吸燈
    OVERDUE    // 取衣逾時：紅燈亮
} WasherState;

/* 定義單一使用者的資料結構 */
typedef struct
{
    /* 使用者 ID，例如 USER01 */
    char id[USER_ID_LENGTH];

    /* 帳號限制狀態：0 = 正常，1 = 限制 */
    uint8_t banned;

} User;

/* USER CODE END PTD */


/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* UART 接收環形緩衝區的容量：128 Bytes */
#define UART_RX_BUFFER_SIZE 128U

/* UART 單行指令最大緩衝容量（包含結尾 '\0'） */
#define UART_CMD_BUFFER_SIZE 64U

/* 系統最多管理 10 位使用者 */
#define MAX_USERS 10U

/* 每位使用者 ID 的儲存空間，包含字串結尾 */
#define USER_ID_LENGTH 20U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;

TIM_HandleTypeDef htim4;


/* USER CODE BEGIN PV */

/* 記錄洗衣機目前的狀態，開機預設為 IDLE */
WasherState washerState = IDLE;

/* 記錄上一次更新呼吸燈的時間 */
uint32_t lastBreathTick = 0;

/* PWM 目前的比較值，初始為 0 */
uint16_t breathDuty = 0;

/* 呼吸燈變化方向：1 變亮，-1 變暗 */
int8_t breathDirection = 1;

/* UART 接收到的單一字元 */
uint8_t uartRxByte = 0;



/* ================= UART Ring Buffer ================= */

/* 建立 128 Bytes 的 UART 接收緩衝陣列 */
uint8_t rxBuffer[UART_RX_BUFFER_SIZE] = {0};

/* Head：下一筆 UART 資料要寫入的位置 */
volatile uint16_t rxHead = 0;

/* Tail：主程式下一筆資料要讀取的位置 */
volatile uint16_t rxTail = 0;

/* ==================================================== */

/* ================= UART 指令緩衝區 ================= */

/* 儲存使用者輸入的一整行指令 */
char uartCmdBuffer[UART_CMD_BUFFER_SIZE] = {0};

/* 目前已接收的指令字元數量 */
uint16_t uartCmdIndex = 0;

/* ================================================= */


/* ================= 使用者管理 ================= */

/* 儲存最多 10 位使用者的資料 */
User users[MAX_USERS] = {0};

/* 目前已建立的使用者數量 */
uint8_t userCount = 0;

/* 目前登入的使用者索引 */
/* -1 代表目前沒有任何使用者登入 */
int8_t currentUserIndex = -1;

/* ============================================== */

/* USER CODE END PV */


/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM4_Init(void);

/* USER CODE BEGIN PFP */

/* 宣告 LED 狀態更新函式 */
void Update_LED(void);

/* 宣告非阻塞 PWM 呼吸燈更新函式 */
void Update_Breathing_LED(void);

/* 宣告 UART 文字傳送函式 */
void UART_SendText(const char *text);

/* 將一個字元寫入 Ring Buffer */
uint8_t RingBuffer_Put(uint8_t data);

/* 從 Ring Buffer 取出一個字元 */
uint8_t RingBuffer_Get(uint8_t *data);

/* 將 UART 收到的字元組合成完整指令 */
void UART_ProcessByte(uint8_t data);

/* USER CODE END PFP */


/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_LPUART1_UART_Init();
  MX_TIM4_Init();


  /* USER CODE BEGIN 2 */

  /* 開機時依照 washerState 更新 LED */
  /* washerState 初始值為 IDLE，因此應亮綠燈 */
  Update_LED();

  /* 傳送系統啟動訊息到 Tera Term */
  UART_SendText("\r\n");
  UART_SendText("================================\r\n");
  UART_SendText(" Smart Laundry Management System\r\n");
  UART_SendText("================================\r\n");
  UART_SendText("System initialized.\r\n");
  UART_SendText("Current State: IDLE\r\n");
  UART_SendText("Please enter user ID.\r\n");

  /* 啟動 LPUART1 中斷接收，每次接收 1 個字元 */
  HAL_UART_Receive_IT(&hlpuart1, &uartRxByte, 1);

  /* USER CODE END 2 */



  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	  /* 非阻塞更新藍色 PWM 呼吸燈 */
	  Update_Breathing_LED();

	  /* 建立變數，存放 Ring Buffer 取出的字元 */
	  uint8_t receivedChar;


	  /* 持續從 Ring Buffer 取出 UART 字元 */
	  while (RingBuffer_Get(&receivedChar) == 1)
	  {
	      /* 將字元交給完整指令處理函式 */
	      UART_ProcessByte(receivedChar);
	  }

  /* USER CODE END 3 */
}
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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 79;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 999;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
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
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED_YELLOW_Pin|LED_RED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : DOOR_BUTTON_Pin */
  GPIO_InitStruct.Pin = DOOR_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DOOR_BUTTON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_GREEN_Pin LED_YELLOW_Pin LED_RED_Pin */
  GPIO_InitStruct.Pin = LED_GREEN_Pin|LED_YELLOW_Pin|LED_RED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}


/* USER CODE BEGIN 4 */

/* 根據洗衣機目前的狀態，更新 LED 顯示 */
void Update_LED(void)
{
    /* 先關閉全部 GPIO 狀態燈，避免多個燈同時亮 */
    HAL_GPIO_WritePin(GPIOB,
                      GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_10,
                      GPIO_PIN_RESET);

    /* 先停止藍色 LED 的 PWM 輸出 */
    HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);

    /* 根據 washerState 決定要顯示的燈號 */
    switch (washerState)
    {
        case IDLE:
            /* 空閒：亮綠燈 PB0 */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
            break;

        case WASHING:
            /* 洗衣中：亮黃燈 PB1 */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
            break;


        case WAITING:
            /* 進入等待取衣狀態，重設呼吸燈 */
            breathDuty = 0;
            breathDirection = 1;
            lastBreathTick = HAL_GetTick();

            /* PWM 從 0% 亮度開始 */
            __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0);

            /* 啟動 TIM4_CH1 PWM */
            HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
            break;


        case OVERDUE:
            /* 取衣逾時：亮紅燈 PB10 */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
            break;

        default:
            /* 異常狀態：恢復為空閒 */
            washerState = IDLE;
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
            break;
    }
}

/* 非阻塞式藍色 PWM 呼吸燈 */
void Update_Breathing_LED(void)
{
    /* 只有 WAITING 狀態才更新呼吸燈 */
    if (washerState != WAITING)
    {
        return;
    }

    /* 取得目前系統運作時間，單位 ms */
    uint32_t now = HAL_GetTick();

    /* 每經過 10 ms，才改變一次亮度 */
    if ((uint32_t)(now - lastBreathTick) >= 10U)
    {
        /* 記錄本次更新時間 */
        lastBreathTick = now;

        /* 根據方向增加或減少 PWM 數值 */
        if (breathDirection == 1)
        {
            /* 逐漸變亮 */
            if (breathDuty < 990)
            {
                breathDuty += 10;
            }
            else
            {
                breathDuty = 999;
                breathDirection = -1;
            }
        }
        else
        {
            /* 逐漸變暗 */
            if (breathDuty >= 10)
            {
                breathDuty -= 10;
            }
            else
            {
                breathDuty = 0;
                breathDirection = 1;
            }
        }

        /* 更新 TIM4 Channel 1 的 PWM 比較值 */
        __HAL_TIM_SET_COMPARE(&htim4,
                              TIM_CHANNEL_1,
                              breathDuty);
    }
}

/* 使用 LPUART1 傳送文字到 Tera Term */
void UART_SendText(const char *text)
{
    /* 確認傳入的文字指標有效 */
    if (text == NULL)
    {
        return;
    }

    /* 計算字串長度，不包含結尾的 '\0' */
    uint16_t length = 0;

    while (text[length] != '\0')
    {
        length++;
    }

    /* 透過 LPUART1 傳送字串 */
    HAL_UART_Transmit(
        &hlpuart1,           // LPUART1 控制結構
        (uint8_t *)text,     // 要傳送的文字
        length,              // 傳送字元數量
        1000                 // 最長等待 1000 ms
    );
}

/* UART 接收完成中斷回呼函式 */

/* UART 接收完成中斷回呼函式 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* 確認中斷來自 LPUART1 */
    if (huart->Instance == LPUART1)
    {
        /* 將接收到的字元存入 Ring Buffer */
        RingBuffer_Put(uartRxByte);

        /* 立即重新啟動下一個字元的 UART 中斷接收 */
        HAL_UART_Receive_IT(&hlpuart1, &uartRxByte, 1);
    }
}

/* 將一個 UART 字元存入 Ring Buffer */
uint8_t RingBuffer_Put(uint8_t data)
{
    /* 計算下一個寫入位置 */
    uint16_t nextHead =
        (rxHead + 1U) % UART_RX_BUFFER_SIZE;

    /* 如果下一個位置等於 Tail，表示 Buffer 已滿 */
    if (nextHead == rxTail)
    {
        /* 緩衝區已滿，不覆蓋尚未讀取的資料 */
        return 0;
    }

    /* 將收到的字元存入目前 Head 位置 */
    rxBuffer[rxHead] = data;

    /* 更新 Head，指向下一個可寫入位置 */
    rxHead = nextHead;

    /* 回傳 1，表示寫入成功 */
    return 1;
}


/* 從 UART Ring Buffer 取出一個字元 */
uint8_t RingBuffer_Get(uint8_t *data)
{
    /* 確認資料指標有效 */
    if (data == NULL)
    {
        return 0;
    }

    /* Head 等於 Tail，代表緩衝區沒有資料 */
    if (rxHead == rxTail)
    {
        return 0;
    }

    /* 讀取目前 Tail 指向的字元 */
    *data = rxBuffer[rxTail];

    /* 更新 Tail，並在到達尾端時回到 0 */
    rxTail = (rxTail + 1U) % UART_RX_BUFFER_SIZE;

    /* 回傳 1，表示成功讀取 */
    return 1;
}

/* 處理 UART 接收到的單一字元 */
void UART_ProcessByte(uint8_t data)
{
    /* 判斷是否收到 Enter（CR 或 LF） */
    if (data == '\r' || data == '\n')
    {
        /* 如果目前沒有輸入任何字元，就忽略 */
        if (uartCmdIndex == 0)
        {
            return;
        }

        /* 在字串結尾加入 '\0' */
        uartCmdBuffer[uartCmdIndex] = '\0';

        /* 回傳完整指令到 Tera Term */
        UART_SendText("\r\nCommand received: ");
        UART_SendText(uartCmdBuffer);
        UART_SendText("\r\n");

        /* 清空指令長度，準備接收下一行 */
        uartCmdIndex = 0;
    }
    else
    {
        /* 檢查是否還有空間存放字元 */
        if (uartCmdIndex < UART_CMD_BUFFER_SIZE - 1U)
        {
            /* 將字元存入指令緩衝區 */
            uartCmdBuffer[uartCmdIndex] = (char)data;

            /* 更新已接收字元數量 */
            uartCmdIndex++;
        }
        else
        {
            /* 指令過長，顯示錯誤並清空 */
            UART_SendText("\r\nError: Command too long.\r\n");

            /* 重設指令位置 */
            uartCmdIndex = 0;
        }
    }
}

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
