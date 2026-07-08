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

#include "esptim.h"


uint8_t frameup;
esp_timer_handle_t esp_tim_handle;                          /* 定时器回调函数句柄 */

/**
 * @brief       初始化高分辨率定时器
 * @param       arr: 自动重装值
 * @param       tp: 定时器周期
 * @retval      无
 */
void esptim_int_init(uint16_t arr, uint64_t tp)
{
    timer_config_t esp_timx_handle = {0};                   /* 定时器句柄 */
    esp_err_t err;

    esptim_int_deinit();

    /* 定义一个定时器结构体 */
    esp_timer_create_args_t tim_periodic_arg = {
    .callback =	&TIM_PeriodElapsedCallback,                 /* 设置回调函数 */
    .arg = NULL,                                            /* 不携带参数 */
    };

    /* 配置定时器 */
    esp_timx_handle.alarm_en = TIMER_ALARM_DIS;             /* 失能定时器报警 */
    esp_timx_handle.counter_en = TIMER_START;               /* 使能定时器 */
    esp_timx_handle.intr_type = TIMER_INTR_MAX;             /* 配置定时器中断模式 */
    esp_timx_handle.counter_dir = TIMER_COUNT_UP;           /* 递增计数模式 */
    esp_timx_handle.auto_reload = arr;                      /* 自动重装载值 */
    esp_timx_handle.clk_src = TIMER_SRC_CLK_DEFAULT;        /* 配置定时器中断源 */

    err = esp_timer_create(&tim_periodic_arg, &esp_tim_handle);   /* 创建一个事件 */
    if (err != ESP_OK)
    {
        esp_tim_handle = NULL;
        return;
    }

    err = esp_timer_start_periodic(esp_tim_handle, tp);           /* 每周期内触发一次 */
    if (err != ESP_OK)
    {
        esp_timer_delete(esp_tim_handle);
        esp_tim_handle = NULL;
    }
}

void esptim_int_deinit(void)
{
    if (esp_tim_handle != NULL)
    {
        esp_timer_stop(esp_tim_handle);
        esp_timer_delete(esp_tim_handle);
        esp_tim_handle = NULL;
    }

    frameup = 0;
}

/**
 * @brief       定时器回调函数
 * @param       无
 * @retval      无
 */
void TIM_PeriodElapsedCallback(void *arg)
{
    frameup = 1;
}
