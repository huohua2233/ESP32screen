/**
 ******************************************************************************
 * @file        main.c
 * @version     V1.0
 * @brief       LVGL 综合实验
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3
 ******************************************************************************
 */

#include "nvs_flash.h"
#include "myiic.h"
#include "xl9555.h"
#include "lvgl_demo.h"
#include "my_spi.h"
#include "key.h"
#include "exfuns.h"
#include "esp_rtc.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "pc_ai_ble.h"
#include "tud_sd.h"

static const char *TAG = "APP_MAIN";

static const char *app_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_EXT:
            return "EXT";
        case ESP_RST_SW:
            return "SW";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INT_WDT";
        case ESP_RST_TASK_WDT:
            return "TASK_WDT";
        case ESP_RST_WDT:
            return "WDT";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_SDIO:
            return "SDIO";
        case ESP_RST_USB:
            return "USB";
        case ESP_RST_JTAG:
            return "JTAG";
        case ESP_RST_EFUSE:
            return "EFUSE";
        case ESP_RST_PWR_GLITCH:
            return "PWR_GLITCH";
        case ESP_RST_CPU_LOCKUP:
            return "CPU_LOCKUP";
        case ESP_RST_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}


/**
 * @brief       程序入口
 * @param       无
 * @retval      无
 */
void app_main(void)
{
    esp_err_t ret;
    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "boot reset_reason=%d name=%s", (int)reset_reason, app_reset_reason_name(reset_reason));
    ret = nvs_flash_init();             /* 初始化NVS */

    lcd_cfg_t lcd_config_info = {0};
    lcd_config_info.notify_flush_ready = lvgl_notify_flush_ready;
    lcd_config_info.user_ctx = &disp_drv;

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ret = nvs_flash_erase();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(ret));
            return;
        }
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = myiic_init();                 /* 初始化IIC0 */
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = my_spi_init();                /* 初始化SPI */
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret));
        return;
    }
    key_init();                         /* 初始化KEY */
    ret = xl9555_init();                /* 初始化IO扩展芯片 */
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "XL9555 init failed: %s", esp_err_to_name(ret));
        return;
    }
    rtc_time_init();
    rtc_restore_saved_time();
    ret = lcd_init(lcd_config_info);    /* 初始化LCD */
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = tud_usb_sd();                 /* 初始化USB硬件 */
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "USB MSC unavailable: %s", esp_err_to_name(ret));
    }
    ret = lvgl_demo();                  /* UI界面程序入口 */
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LVGL init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = pc_ai_ble_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "PC AI BLE init failed: %s", esp_err_to_name(ret));
    }
}
