/**
 ******************************************************************************
 * @file        esptim.c
 * @version     V1.0
 * @brief       高分辨率定时器（ESP定时器）驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __ESPTIM_H
#define __ESPTIM_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "soc/timer_group_struct.h"
#include "driver/timer.h"
#include "esp_timer.h"


extern esp_timer_handle_t esp_tim_handle;                          /* 定时器回调函数句柄 */

extern uint8_t frameup;
/* 函数声明 */
void esptim_int_init(uint16_t arr, uint64_t tp);    /* 初始化初始化高分辨率定时器 */
void esptim_int_deinit(void);
void TIM_PeriodElapsedCallback(void *arg);          /* 定时器回调函数 */

#endif
