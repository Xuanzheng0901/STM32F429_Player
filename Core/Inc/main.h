/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define EPD_DC_Pin GPIO_PIN_4
#define EPD_DC_GPIO_Port GPIOC
#define EPD_RST_Pin GPIO_PIN_5
#define EPD_RST_GPIO_Port GPIOC
#define EPD_BUSY_Pin GPIO_PIN_0
#define EPD_BUSY_GPIO_Port GPIOB
#define EAR_DET_Pin GPIO_PIN_14
#define EAR_DET_GPIO_Port GPIOB
#define ES9018_RST_Pin GPIO_PIN_8
#define ES9018_RST_GPIO_Port GPIOD
#define MAX97220_MUTE_Pin GPIO_PIN_9
#define MAX97220_MUTE_GPIO_Port GPIOD
#define BUTTON_SIDE1_Pin GPIO_PIN_10
#define BUTTON_SIDE1_GPIO_Port GPIOD
#define BUTTON_SIDE2_Pin GPIO_PIN_11
#define BUTTON_SIDE2_GPIO_Port GPIOD
#define BUTTON_SIDE3_Pin GPIO_PIN_12
#define BUTTON_SIDE3_GPIO_Port GPIOD
#define BUTTON_UP1_Pin GPIO_PIN_13
#define BUTTON_UP1_GPIO_Port GPIOD
#define BUTTON_UP2_Pin GPIO_PIN_14
#define BUTTON_UP2_GPIO_Port GPIOD
#define BUTTON_UP3_Pin GPIO_PIN_15
#define BUTTON_UP3_GPIO_Port GPIOD
#define BUTTON_UP4_Pin GPIO_PIN_6
#define BUTTON_UP4_GPIO_Port GPIOC
#define BUTTON_UP5_Pin GPIO_PIN_7
#define BUTTON_UP5_GPIO_Port GPIOC
#define SDIO_CD_Pin GPIO_PIN_15
#define SDIO_CD_GPIO_Port GPIOA
#define KNOB_BUTTON_Pin GPIO_PIN_3
#define KNOB_BUTTON_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
