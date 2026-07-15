/**
 ******************************************************************************
 * @file        lv_photo_ui.c
 * @version     V1.0
 * @brief       LVGL 照片 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "lv_photo_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


LV_IMG_DECLARE(Vectorback_next)
LV_IMG_DECLARE(Vectorback_pro)
lv_photo_ui_t photo_cfig;
static lv_obj_t *photo_pre_hot = NULL;
static lv_obj_t *photo_next_hot = NULL;
extern lv_obj_t * back_btn;

extern lv_group_t *ctrl_g;
extern lv_indev_t *indev_keypad;    /* 按键组 */

/* PIC Task Configuration
 * Including: task handle, task priority, stack size, creation task
 */
#define PIC_PRIO      2                                 /* task priority */
#define PIC_STK_SIZE  5 * 1024                          /* task stack size */
#define PHOTO_CACHE_MAGIC 0x50353636U
#define PHOTO_CACHE_PATH_SIZE (255 * 2 + 8)
#define PHOTO_DIAG_FILE_PATH "0:/PHOTO_DIAG.LOG"
#define PHOTO_DIAG_MESSAGE_SIZE 768
#define PHOTO_DIAG_LINE_SIZE 896
#define PHOTO_DIAG_LOCK_TIMEOUT_MS 1000
#define PHOTO_DIAG_SD_ENABLED 0
#define PHOTO_DIAG_SERIAL_ENABLED 0
#if PHOTO_DIAG_SD_ENABLED || PHOTO_DIAG_SERIAL_ENABLED
static const char *PHOTO_TAG = "PHOTO_DIAG";
#endif
TaskHandle_t          PICTask_Handler;                  /* task handle */
extern TaskHandle_t VIDEOTask_Handler;
void pic(void *pvParameters);                           /* Task function */
void lv_pic_png_bmp_jpeg_decode(uint16_t w,uint16_t h,uint8_t * pic_buf);
static bool lv_photo_is_supported_type(uint8_t type);

typedef struct
{
    uint32_t magic;
    uint16_t target_w;
    uint16_t target_h;
    uint16_t img_w;
    uint16_t img_h;
    uint32_t data_size;
    uint32_t data_checksum;
    uint32_t src_size;
    uint16_t src_date;
    uint16_t src_time;
    uint32_t src_checksum;
} photo_cache_header_t;

static bool photo_cache_save_enabled = false;
static char photo_cache_path[PHOTO_CACHE_PATH_SIZE];
static photo_cache_header_t photo_cache_header;
static bool photo_sd_session = false;
#if PHOTO_DIAG_SD_ENABLED
static FIL photo_diag_file;
static SemaphoreHandle_t photo_diag_mutex = NULL;
static bool photo_diag_file_opened = false;
static bool photo_diag_file_faulted = false;
static char photo_diag_message[PHOTO_DIAG_MESSAGE_SIZE];
static char photo_diag_line[PHOTO_DIAG_LINE_SIZE];
#endif

#if PHOTO_DIAG_SD_ENABLED
static const char *photo_diag_reset_reason_name(esp_reset_reason_t reason)
{
    static const char *const names[] = {
        "UNKNOWN",
        "POWERON",
        "EXT",
        "SW",
        "PANIC",
        "INT_WDT",
        "TASK_WDT",
        "WDT",
        "DEEPSLEEP",
        "BROWNOUT",
        "SDIO",
        "USB",
        "JTAG",
        "EFUSE",
        "PWR_GLITCH",
        "CPU_LOCKUP"
    };

    unsigned int index = (unsigned int)reason;
    return index < (sizeof(names) / sizeof(names[0])) ? names[index] : "INVALID";
}

static bool photo_diag_mutex_init(void)
{
    if (photo_diag_mutex == NULL)
    {
        photo_diag_mutex = xSemaphoreCreateMutex();
    }

    return photo_diag_mutex != NULL;
}

static char photo_diag_level_char(esp_log_level_t level)
{
    if (level == ESP_LOG_ERROR)
    {
        return 'E';
    }
    if (level == ESP_LOG_WARN)
    {
        return 'W';
    }
    return 'I';
}
#endif

static void photo_diag_log(esp_log_level_t level, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void photo_diag_log(esp_log_level_t level, const char *format, ...)
{
#if !PHOTO_DIAG_SERIAL_ENABLED
    (void)level;
    (void)format;
    return;
#else
    va_list args;

#if !PHOTO_DIAG_SD_ENABLED
    va_start(args, format);
    esp_log_writev(level, PHOTO_TAG, format, args);
    va_end(args);
    esp_log_write(level, PHOTO_TAG, "\n");
    return;
#else
    if (!photo_diag_mutex_init())
    {
        va_start(args, format);
        esp_log_writev(level, PHOTO_TAG, format, args);
        va_end(args);
        esp_log_write(level, PHOTO_TAG, "\n");
        return;
    }

    if (xSemaphoreTake(photo_diag_mutex, pdMS_TO_TICKS(PHOTO_DIAG_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        esp_log_write(ESP_LOG_WARN, PHOTO_TAG, "diagnostic log dropped because mutex timed out\n");
        return;
    }

    va_start(args, format);
    vsnprintf(photo_diag_message, sizeof(photo_diag_message), format, args);
    va_end(args);

    esp_log_write(level, PHOTO_TAG, "%s\n", photo_diag_message);

    if (photo_diag_file_opened && !photo_diag_file_faulted)
    {
        int line_result = snprintf(photo_diag_line,
                                   sizeof(photo_diag_line),
                                   "%lu %c %s\r\n",
                                   (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                                   photo_diag_level_char(level),
                                   photo_diag_message);
        size_t line_length = line_result > 0 ? (size_t)line_result : 0;
        if (line_length >= sizeof(photo_diag_line))
        {
            line_length = sizeof(photo_diag_line) - 1;
        }

        UINT bytes_written = 0;
        FRESULT write_res = FR_TIMEOUT;
        FRESULT sync_res = FR_TIMEOUT;
        if (sd_access_begin(pdMS_TO_TICKS(PHOTO_DIAG_LOCK_TIMEOUT_MS)))
        {
            write_res = f_write(&photo_diag_file, photo_diag_line, (UINT)line_length, &bytes_written);
            if (write_res == FR_OK && bytes_written == line_length)
            {
                sync_res = f_sync(&photo_diag_file);
            }
            sd_access_end();
        }

        if (write_res != FR_OK || bytes_written != line_length || sync_res != FR_OK)
        {
            photo_diag_file_faulted = true;
            esp_log_write(ESP_LOG_ERROR,
                          PHOTO_TAG,
                          "SD log write failed write_res=%d bytes=%u expected=%u sync_res=%d\n",
                          (int)write_res,
                          (unsigned int)bytes_written,
                          (unsigned int)line_length,
                          (int)sync_res);
        }
    }

    xSemaphoreGive(photo_diag_mutex);
#endif
#endif
}

#if PHOTO_DIAG_SD_ENABLED
static bool photo_diag_file_open(const char *session_kind)
{
    if (!photo_diag_mutex_init())
    {
        esp_log_write(ESP_LOG_ERROR, PHOTO_TAG, "SD log mutex allocation failed\n");
        return false;
    }

    if (xSemaphoreTake(photo_diag_mutex, pdMS_TO_TICKS(PHOTO_DIAG_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        return false;
    }

    if (photo_diag_file_opened)
    {
        xSemaphoreGive(photo_diag_mutex);
        return true;
    }

    FRESULT res = sd_f_open(&photo_diag_file, PHOTO_DIAG_FILE_PATH, FA_WRITE | FA_OPEN_APPEND);
    photo_diag_file_opened = (res == FR_OK);
    photo_diag_file_faulted = false;
    xSemaphoreGive(photo_diag_mutex);

    if (!photo_diag_file_opened)
    {
        esp_log_write(ESP_LOG_ERROR, PHOTO_TAG, "SD log open failed path=%s res=%d\n", PHOTO_DIAG_FILE_PATH, (int)res);
        return false;
    }

    esp_reset_reason_t reset_reason = esp_reset_reason();
    photo_diag_log(ESP_LOG_INFO,
                   "session begin kind=%s path=%s reset_reason=%d name=%s",
                   session_kind,
                   PHOTO_DIAG_FILE_PATH,
                   (int)reset_reason,
                   photo_diag_reset_reason_name(reset_reason));
    return true;
}
#endif

static void photo_diag_file_close(bool write_footer)
{
#if !PHOTO_DIAG_SD_ENABLED
    (void)write_footer;
    return;
#else
    if (!photo_diag_file_opened || photo_diag_mutex == NULL)
    {
        return;
    }

    if (write_footer)
    {
        photo_diag_log(ESP_LOG_INFO, "session end path=%s", PHOTO_DIAG_FILE_PATH);
    }

    if (xSemaphoreTake(photo_diag_mutex, pdMS_TO_TICKS(PHOTO_DIAG_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        return;
    }

    FRESULT close_res = sd_f_close(&photo_diag_file);
    photo_diag_file_opened = false;
    photo_diag_file_faulted = false;
    xSemaphoreGive(photo_diag_mutex);

    esp_log_write(ESP_LOG_INFO,
                  PHOTO_TAG,
                  "SD log closed path=%s close_res=%d\n",
                  PHOTO_DIAG_FILE_PATH,
                  (int)close_res);
#endif
}

#undef ESP_LOGI
#undef ESP_LOGW
#undef ESP_LOGE
#define ESP_LOGI(tag, format, ...) photo_diag_log(ESP_LOG_INFO, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) photo_diag_log(ESP_LOG_WARN, format, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) photo_diag_log(ESP_LOG_ERROR, format, ##__VA_ARGS__)

void lv_photo_diag_record_boot(void)
{
#if !PHOTO_DIAG_SD_ENABLED
    return;
#else
    if (!sd_card_is_mounted())
    {
        esp_log_write(ESP_LOG_WARN, PHOTO_TAG, "boot SD log skipped because card is not mounted\n");
        return;
    }

    if (photo_diag_file_open("boot"))
    {
        photo_diag_file_close(false);
    }
#endif
}

static void photo_log_runtime(const char *stage)
{
    UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(NULL);

    ESP_LOGI(PHOTO_TAG,
             "runtime stage=%s heap8_free=%lu heap8_min=%lu heap8_largest=%lu internal_free=%lu internal_largest=%lu psram_free=%lu psram_largest=%lu stack_hwm=%lu",
             stage,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned long)stack_hwm);
}

static void photo_log_lvgl_memory(const char *stage)
{
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);
    ESP_LOGI(PHOTO_TAG,
             "lvgl stage=%s free=%lu largest=%lu used_pct=%u frag_pct=%u",
             stage,
             (unsigned long)mon.free_size,
             (unsigned long)mon.free_biggest_size,
             (unsigned int)mon.used_pct,
             (unsigned int)mon.frag_pct);
}

static void photo_sd_session_release(void)
{
    if (photo_sd_session)
    {
        photo_sd_session = false;
        sd_local_session_end();
    }
}

/**
 * @brief       Obtain the total number of target files in the path path
 * @param       path : path
 * @retval      Total number of valid files
 */
uint16_t pic_get_tnum(char *path)
{
    uint8_t res;
    uint16_t rval = 0;
    FF_DIR tdir;
    FILINFO *tfileinfo;
    tfileinfo = (FILINFO *)malloc(sizeof(FILINFO));
    res = sd_f_opendir(&tdir, (const TCHAR *)path);

    if (res == FR_OK)
    {
        if (tfileinfo)
        {
            while (1)
            {
                res = sd_f_readdir(&tdir, tfileinfo);

                if (res != FR_OK || tfileinfo->fname[0] == 0)break;
                res = exfuns_file_type(tfileinfo->fname);

                if (lv_photo_is_supported_type(res))
                {
                    rval++;
                }
            }
        }

        sd_f_closedir(&tdir);
    }
    free(tfileinfo);
    return rval;
}

lv_img_dsc_t img_pic_dsc = {
    .header.always_zero = 0,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};

static uint8_t *photo_img_buf = NULL;
static volatile bool photo_decode_ok = false;
static volatile bool photo_switching = false;
static volatile bool photo_closing = false;
static lv_timer_t *photo_close_timer = NULL;
static void photo_finish_close(void);
static void photo_close_timer_cb(lv_timer_t *timer);

static void photo_sd_lost(void)
{
    ESP_LOGE(PHOTO_TAG, "sd lost while photo app active");
    photo_log_runtime("sd_lost");
    lv_smail_icon_clear_state(TF_STATE);
    photo_switching = false;
    photo_cfig.pic_start = 0;
    lv_request_active_app_close_from_task(lv_pic_del);
}

static bool lv_photo_is_supported_type(uint8_t type)
{
    return type == T_BMP || type == T_JPG || type == T_JPEG || type == T_PNG;
}

static uint32_t lv_photo_cache_checksum(const uint8_t *buf, uint32_t len)
{
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < len; i++)
    {
        hash ^= buf[i];
        hash *= 16777619U;
    }

    return hash;
}

static bool lv_photo_make_cache_path(const char *src, char *dst, size_t dst_size)
{
    size_t src_len = strlen(src);
    const char *slash = strrchr(src, '/');
    const char *dot = strrchr(src, '.');
    const char *cache_ext = ".C56";

    switch (exfuns_file_type((char *)src))
    {
        case T_JPG:
            cache_ext = ".J56";
            break;
        case T_JPEG:
            cache_ext = ".E56";
            break;
        case T_PNG:
            cache_ext = ".P56";
            break;
        default:
            cache_ext = ".C56";
            break;
    }

    if (src_len + 1 > dst_size)
    {
        return false;
    }

    strcpy(dst, src);

    if (dot != NULL && (slash == NULL || dot > slash))
    {
        size_t base_len = (size_t)(dot - src);

        if (base_len + strlen(cache_ext) + 1 > dst_size)
        {
            return false;
        }

        strcpy(dst + base_len, cache_ext);
    }
    else
    {
        if (src_len + strlen(cache_ext) + 1 > dst_size)
        {
            return false;
        }

        strcat(dst, cache_ext);
    }

    return true;
}

static bool lv_photo_get_src_info(const char *src, uint32_t *src_size, uint16_t *src_date, uint16_t *src_time)
{
    FILINFO info;
    FRESULT res;

    res = sd_f_stat(src, &info);

    if (res != FR_OK)
    {
        return false;
    }

    *src_size = (uint32_t)info.fsize;
    *src_date = info.fdate;
    *src_time = info.ftime;
    return true;
}

static bool lv_photo_cache_header_valid(const photo_cache_header_t *header, uint16_t target_w, uint16_t target_h, uint32_t src_size, uint16_t src_date, uint16_t src_time)
{
    uint32_t data_size;
    uint32_t max_data_size;

    if (header->magic != PHOTO_CACHE_MAGIC ||
        header->target_w != target_w ||
        header->target_h != target_h ||
        header->img_w == 0 ||
        header->img_h == 0 ||
        header->img_w > target_w ||
        header->img_h > target_h ||
        header->src_size != src_size ||
        header->src_date != src_date ||
        header->src_time != src_time)
    {
        return false;
    }

    data_size = (uint32_t)header->img_w * header->img_h * 2;
    max_data_size = (uint32_t)target_w * target_h * 2;
    return header->data_size == data_size && header->data_size <= max_data_size;
}

static void lv_photo_cache_begin(const char *src, uint16_t target_w, uint16_t target_h)
{
    uint32_t src_size;
    uint16_t src_date;
    uint16_t src_time;

    photo_cache_save_enabled = false;
    ESP_LOGI(PHOTO_TAG, "cache prepare begin src=%s target=%ux%u", src, (unsigned int)target_w, (unsigned int)target_h);

    if (!lv_photo_make_cache_path(src, photo_cache_path, sizeof(photo_cache_path)))
    {
        ESP_LOGE(PHOTO_TAG, "cache path generation failed src=%s", src);
        return;
    }

    if (!lv_photo_get_src_info(src, &src_size, &src_date, &src_time))
    {
        ESP_LOGE(PHOTO_TAG, "cache source stat failed src=%s", src);
        return;
    }

    ESP_LOGI(PHOTO_TAG,
             "cache source info src=%s cache=%s size=%lu date=0x%04x time=0x%04x",
             src,
             photo_cache_path,
             (unsigned long)src_size,
             (unsigned int)src_date,
             (unsigned int)src_time);

    photo_cache_header.magic = PHOTO_CACHE_MAGIC;
    photo_cache_header.target_w = target_w;
    photo_cache_header.target_h = target_h;
    photo_cache_header.img_w = 0;
    photo_cache_header.img_h = 0;
    photo_cache_header.data_size = 0;
    photo_cache_header.data_checksum = 0;
    photo_cache_header.src_size = src_size;
    photo_cache_header.src_date = src_date;
    photo_cache_header.src_time = src_time;
    photo_cache_header.src_checksum = 0;
    photo_cache_save_enabled = true;
    ESP_LOGI(PHOTO_TAG, "cache prepare ready cache=%s", photo_cache_path);
}

static void lv_photo_cache_end(void)
{
    photo_cache_save_enabled = false;
}

static void lv_photo_cache_save(uint16_t w, uint16_t h, const uint8_t *buf)
{
    FIL *fp;
    FRESULT res;
    FRESULT close_res = FR_OK;
    UINT bw = 0;
    photo_cache_header_t header;
    photo_cache_header_t invalid_header;
    bool payload_written = false;
    bool committed = false;
    TickType_t start_tick;

    if (!photo_cache_save_enabled)
    {
        return;
    }

    if (buf == NULL || w == 0 || h == 0)
    {
        ESP_LOGE(PHOTO_TAG, "cache save rejected cache=%s buf=%p size=%ux%u", photo_cache_path, (const void *)buf, (unsigned int)w, (unsigned int)h);
        return;
    }

    header = photo_cache_header;
    header.img_w = w;
    header.img_h = h;
    header.data_size = (uint32_t)w * h * 2;
    start_tick = xTaskGetTickCount();
    ESP_LOGI(PHOTO_TAG,
             "cache save begin cache=%s image=%ux%u bytes=%lu buf=%p",
             photo_cache_path,
             (unsigned int)w,
             (unsigned int)h,
             (unsigned long)header.data_size,
             (const void *)buf);
    photo_log_runtime("cache_save_begin");
    header.data_checksum = lv_photo_cache_checksum(buf, header.data_size);
    ESP_LOGI(PHOTO_TAG, "cache payload checksum complete value=0x%08lx", (unsigned long)header.data_checksum);
    invalid_header = header;
    invalid_header.magic = 0;

    fp = (FIL *)calloc(1, sizeof(FIL));
    if (fp == NULL)
    {
        ESP_LOGE(PHOTO_TAG, "cache save file handle allocation failed cache=%s", photo_cache_path);
        photo_log_runtime("cache_save_alloc_failed");
        return;
    }

    res = sd_f_open(fp, photo_cache_path, FA_WRITE | FA_CREATE_ALWAYS);
    ESP_LOGI(PHOTO_TAG, "cache file open cache=%s res=%d", photo_cache_path, (int)res);

    if (res == FR_OK)
    {
        res = sd_f_write(fp, &invalid_header, sizeof(invalid_header), &bw);
        ESP_LOGI(PHOTO_TAG, "cache invalid header write res=%d bytes=%u", (int)res, (unsigned int)bw);

        if (res == FR_OK && bw == sizeof(invalid_header))
        {
            bw = 0;
            res = sd_f_write(fp, buf, header.data_size, &bw);
            ESP_LOGI(PHOTO_TAG,
                     "cache payload write res=%d bytes=%u expected=%lu",
                     (int)res,
                     (unsigned int)bw,
                     (unsigned long)header.data_size);
        }

        if (res == FR_OK && bw == header.data_size)
        {
            res = sd_f_sync(fp);
            payload_written = (res == FR_OK);
            ESP_LOGI(PHOTO_TAG, "cache payload sync res=%d", (int)res);
        }

        if (res == FR_OK)
        {
            res = sd_f_lseek(fp, 0);
            ESP_LOGI(PHOTO_TAG, "cache header seek res=%d", (int)res);

            if (res == FR_OK)
            {
                bw = 0;
                res = sd_f_write(fp, &header, sizeof(header), &bw);
                ESP_LOGI(PHOTO_TAG, "cache final header write res=%d bytes=%u", (int)res, (unsigned int)bw);
            }

            if (res == FR_OK && bw == sizeof(header))
            {
                res = sd_f_sync(fp);
                committed = payload_written && (res == FR_OK);
                ESP_LOGI(PHOTO_TAG, "cache final sync res=%d", (int)res);
            }
        }

        close_res = sd_f_close(fp);
    }
    free(fp);

    ESP_LOGI(PHOTO_TAG,
             "cache save end cache=%s res=%d close_res=%d payload_written=%u committed=%u elapsed_ms=%lu",
             photo_cache_path,
             (int)res,
             (int)close_res,
             payload_written ? 1U : 0U,
             committed ? 1U : 0U,
             (unsigned long)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS));
    photo_log_runtime("cache_save_end");
}

static bool lv_photo_cache_load(const char *src, uint16_t target_w, uint16_t target_h)
{
    FIL *fp;
    FRESULT res;
    UINT br = 0;
    uint8_t *buf;
    uint32_t src_size;
    uint16_t src_date;
    uint16_t src_time;
    photo_cache_header_t header;
    bool opened = false;
    uint32_t expected_file_size;

    if (!lv_photo_make_cache_path(src, photo_cache_path, sizeof(photo_cache_path)))
    {
        ESP_LOGE(PHOTO_TAG, "cache load path generation failed src=%s", src);
        return false;
    }

    if (!lv_photo_get_src_info(src, &src_size, &src_date, &src_time))
    {
        ESP_LOGE(PHOTO_TAG, "cache load source stat failed src=%s", src);
        return false;
    }

    ESP_LOGI(PHOTO_TAG,
             "cache load begin src=%s cache=%s source_size=%lu target=%ux%u",
             src,
             photo_cache_path,
             (unsigned long)src_size,
             (unsigned int)target_w,
             (unsigned int)target_h);

    fp = (FIL *)calloc(1, sizeof(FIL));
    if (fp == NULL)
    {
        ESP_LOGE(PHOTO_TAG, "cache load file handle allocation failed cache=%s", photo_cache_path);
        photo_log_runtime("cache_load_alloc_failed");
        return false;
    }

    res = sd_f_open(fp, photo_cache_path, FA_READ);

    if (res == FR_OK)
    {
        opened = true;
        res = sd_f_read(fp, &header, sizeof(header), &br);
    }

    if (res != FR_OK || br != sizeof(header) || !lv_photo_cache_header_valid(&header, target_w, target_h, src_size, src_date, src_time))
    {
        ESP_LOGI(PHOTO_TAG,
                 "cache miss header cache=%s open_res=%d bytes=%u opened=%u",
                 photo_cache_path,
                 (int)res,
                 (unsigned int)br,
                 opened ? 1U : 0U);
        if (opened)
        {
            sd_f_close(fp);
        }
        free(fp);
        return false;
    }

    expected_file_size = (uint32_t)sizeof(header) + header.data_size;

    if ((uint32_t)f_size(fp) != expected_file_size)
    {
        ESP_LOGI(PHOTO_TAG,
                 "cache miss file size cache=%s actual=%lu expected=%lu",
                 photo_cache_path,
                 (unsigned long)f_size(fp),
                 (unsigned long)expected_file_size);
        sd_f_close(fp);
        free(fp);
        return false;
    }

    buf = (uint8_t *)malloc(header.data_size);
    if (buf == NULL)
    {
        ESP_LOGE(PHOTO_TAG, "cache image allocation failed cache=%s bytes=%lu", photo_cache_path, (unsigned long)header.data_size);
        photo_log_runtime("cache_image_alloc_failed");
        sd_f_close(fp);
        free(fp);
        return false;
    }

    res = sd_f_read(fp, buf, header.data_size, &br);
    sd_f_close(fp);
    free(fp);

    if (res != FR_OK || br != header.data_size)
    {
        ESP_LOGE(PHOTO_TAG,
                 "cache image read failed cache=%s res=%d bytes=%u expected=%lu",
                 photo_cache_path,
                 (int)res,
                 (unsigned int)br,
                 (unsigned long)header.data_size);
        free(buf);
        return false;
    }

    if (lv_photo_cache_checksum(buf, header.data_size) != header.data_checksum)
    {
        ESP_LOGE(PHOTO_TAG, "cache payload checksum mismatch cache=%s", photo_cache_path);
        free(buf);
        return false;
    }

    ESP_LOGI(PHOTO_TAG,
             "cache hit cache=%s image=%ux%u bytes=%lu",
             photo_cache_path,
             (unsigned int)header.img_w,
             (unsigned int)header.img_h,
             (unsigned long)header.data_size);
    photo_log_runtime("cache_hit");
    lv_pic_png_bmp_jpeg_decode(header.img_w, header.img_h, buf);
    return photo_decode_ok;
}

static void lv_photo_free_img_buf(void)
{
    if (photo_img_buf != NULL)
    {
        free(photo_img_buf);
        photo_img_buf = NULL;
    }

    img_pic_dsc.data = NULL;
    img_pic_dsc.data_size = 0;
}

static void lv_photo_show_black(void)
{
    if (photo_cfig.photo_obj_t.photo_img != NULL)
    {
        lv_obj_add_flag(photo_cfig.photo_obj_t.photo_img, LV_OBJ_FLAG_HIDDEN);
        lv_img_cache_invalidate_src(&img_pic_dsc);
        lv_photo_free_img_buf();
    }

    if (photo_cfig.photo_box != NULL)
    {
        lv_obj_invalidate(photo_cfig.photo_box);
    }

}

/**
 * @brief       PNG/BMPJPEG/JPG decoding
 * @param       filename : file name
 * @param       width    : image width
 * @param       height   : image height
 * @retval      None
 */
void lv_pic_png_bmp_jpeg_decode(uint16_t w,uint16_t h,uint8_t * pic_buf)
{
    uint32_t data_size = w * h * 2;

    ESP_LOGI(PHOTO_TAG,
             "decode callback begin image=%ux%u bytes=%lu buf=%p image_obj=%p",
             (unsigned int)w,
             (unsigned int)h,
             (unsigned long)data_size,
             (void *)pic_buf,
             (void *)photo_cfig.photo_obj_t.photo_img);
    photo_log_runtime("decode_callback_begin");

    if (pic_buf == NULL || data_size == 0)
    {
        ESP_LOGE(PHOTO_TAG, "decode callback rejected image=%ux%u buf=%p", (unsigned int)w, (unsigned int)h, (void *)pic_buf);
        free(pic_buf);
        photo_switching = false;
        return;
    }

    lv_photo_cache_save(w, h, pic_buf);
    ESP_LOGI(PHOTO_TAG, "decode callback cache phase complete image=%ux%u", (unsigned int)w, (unsigned int)h);

    if (!lvgl_mux_lock(200))
    {
        ESP_LOGE(PHOTO_TAG, "decode callback LVGL lock timeout image=%ux%u", (unsigned int)w, (unsigned int)h);
        photo_log_runtime("display_lock_failed");
        free(pic_buf);
        photo_switching = false;
        return;
    }

    photo_log_lvgl_memory("before_image_swap");
    lv_img_cache_invalidate_src(&img_pic_dsc);
    lv_photo_free_img_buf();
    photo_img_buf = pic_buf;

    img_pic_dsc.header.w = w;
    img_pic_dsc.header.h = h;
    img_pic_dsc.data_size = data_size;
    img_pic_dsc.data = (const uint8_t *)photo_img_buf;
    lv_img_set_src(photo_cfig.photo_obj_t.photo_img,&img_pic_dsc);
    lv_obj_clear_flag(photo_cfig.photo_obj_t.photo_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(photo_cfig.photo_obj_t.photo_img);
    photo_decode_ok = true;
    photo_switching = false;
    photo_log_lvgl_memory("after_image_swap");
    lvgl_mux_unlock();
    ESP_LOGI(PHOTO_TAG, "display complete image=%ux%u bytes=%lu", (unsigned int)w, (unsigned int)h, (unsigned long)data_size);
    photo_log_runtime("display_complete");
}

static bool lv_photo_get_file_by_index(uint16_t index)
{
    FRESULT res;
    FF_DIR dir;
    uint16_t count = 0;
    bool found = false;

    res = sd_f_opendir(&dir, "0:/PICTURE");

    if (res == FR_OK)
    {
        while (1)
        {
            res = sd_f_readdir(&dir, photo_cfig.pic_picfileinfo);

            if (res != FR_OK)
            {
                photo_sd_lost();
                break;
            }

            if (photo_cfig.pic_picfileinfo->fname[0] == 0)
            {
                break;
            }

            if (!lv_photo_is_supported_type(exfuns_file_type(photo_cfig.pic_picfileinfo->fname)))
            {
                continue;
            }

            if (count == index)
            {
                strcpy((char *)photo_cfig.pic_pname, "0:/PICTURE/");
                strcat((char *)photo_cfig.pic_pname, (const char *)photo_cfig.pic_picfileinfo->fname);
                found = true;
                break;
            }

            count++;
        }

        sd_f_closedir(&dir);
    }
    else
    {
        photo_sd_lost();
    }

    return found;
}

/**
 * @brief       pic task
 * @param       pvParameters : parameters (not used)
 * @retval      None
 */
void pic(void *pvParameters)
{
    pvParameters = pvParameters;
    uint8_t file_type = 0;
    int screen_w = 0;
    int screen_h = 0;
    TickType_t decode_start = 0;

    uint16_t consecutive_failures = 0;

    ESP_LOGI(PHOTO_TAG, "pic task started core=%d", xPortGetCoreID());
    photo_log_runtime("task_start");

    while(1)
    {
        if (!photo_cfig.pic_start)
        {
            ESP_LOGI(PHOTO_TAG, "pic task stopping because pic_start is clear");
            photo_log_runtime("task_stop");
            photo_sd_session_release();
            PICTask_Handler = NULL;
            vTaskDelete(NULL);
        }

		/* 选中SD卡 */
        photo_cfig.pic_totpicnum = pic_get_tnum("0:/PICTURE");
        ESP_LOGI(PHOTO_TAG, "picture scan complete count=%u", (unsigned int)photo_cfig.pic_totpicnum);

        if (photo_cfig.pic_totpicnum == 0)
        {
            ESP_LOGE(PHOTO_TAG, "picture scan returned no supported images");
            photo_sd_lost();
            photo_switching = false;
            vTaskDelay(10);
            continue;
        }

        if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
        {
            photo_cfig.pic_curindex = 0;
        }

        while (photo_cfig.pic_start)
        {
            if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
            {
                photo_cfig.pic_curindex = 0;
            }

            if (!lv_photo_get_file_by_index(photo_cfig.pic_curindex))
            {
                ESP_LOGE(PHOTO_TAG,
                         "picture lookup failed index=%u total=%u failures=%u",
                         (unsigned int)photo_cfig.pic_curindex,
                         (unsigned int)photo_cfig.pic_totpicnum,
                         (unsigned int)consecutive_failures);
                photo_cfig.pic_totpicnum = pic_get_tnum("0:/PICTURE");
                photo_switching = false;
                consecutive_failures++;
                photo_cfig.pic_curindex++;

                if (photo_cfig.pic_totpicnum == 0 || consecutive_failures >= photo_cfig.pic_totpicnum)
                {
                    ESP_LOGE(PHOTO_TAG, "pic task stopping after repeated lookup failures");
                    photo_cfig.pic_start = 0;
                    lv_request_active_app_close_from_task(lv_pic_del);
                    photo_sd_session_release();
                    PICTask_Handler = NULL;
                    vTaskDelete(NULL);
                }

                if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
                {
                    photo_cfig.pic_curindex = 0;
                }

                vTaskDelay(10);
                continue;
            }

            file_type = exfuns_file_type(photo_cfig.pic_pname);
            ESP_LOGI(PHOTO_TAG,
                     "picture selected index=%u total=%u type=0x%02x path=%s",
                     (unsigned int)photo_cfig.pic_curindex,
                     (unsigned int)photo_cfig.pic_totpicnum,
                     (unsigned int)file_type,
                     photo_cfig.pic_pname);
            screen_w = 0;
            screen_h = 0;

            if (lvgl_mux_lock(200))
            {
                screen_w = lv_obj_get_width(lv_scr_act());
                screen_h = lv_obj_get_height(lv_scr_act());
                lvgl_mux_unlock();
            }

            if (screen_w <= 0 || screen_h <= 20)
            {
                ESP_LOGW(PHOTO_TAG, "screen size unavailable width=%d height=%d", screen_w, screen_h);
                photo_switching = false;
                vTaskDelay(10);
                continue;
            }

            photo_decode_ok = false;
            decode_start = xTaskGetTickCount();
            ESP_LOGI(PHOTO_TAG,
                     "decode pipeline begin type=0x%02x target=%dx%d path=%s",
                     (unsigned int)file_type,
                     screen_w,
                     screen_h - 20,
                     photo_cfig.pic_pname);
            photo_log_runtime("decode_pipeline_begin");

            switch (file_type)
            {
                case T_BMP:
                    ESP_LOGI(PHOTO_TAG, "BMP decoder call begin path=%s", photo_cfig.pic_pname);
                    bmp_decode(photo_cfig.pic_pname,screen_w,screen_h,(void *)lv_pic_png_bmp_jpeg_decode);    /* BMP decode */
                    ESP_LOGI(PHOTO_TAG, "BMP decoder call returned ok=%u", photo_decode_ok ? 1U : 0U);
                    break;
                case T_JPG:
                case T_JPEG:
                    if (!lv_photo_cache_load(photo_cfig.pic_pname, (uint16_t)screen_w, (uint16_t)(screen_h - 20)))
                    {
                        lv_photo_cache_begin(photo_cfig.pic_pname, (uint16_t)screen_w, (uint16_t)(screen_h - 20));
                        ESP_LOGI(PHOTO_TAG, "JPEG decoder call begin path=%s", photo_cfig.pic_pname);
                        jpeg_decode(photo_cfig.pic_pname,screen_w,screen_h - 20,(void *)lv_pic_png_bmp_jpeg_decode);   /* JPG/JPEG decode */
                        ESP_LOGI(PHOTO_TAG, "JPEG decoder call returned ok=%u", photo_decode_ok ? 1U : 0U);
                        lv_photo_cache_end();
                    }
                    break;
                case T_PNG:
                    if (!lv_photo_cache_load(photo_cfig.pic_pname, (uint16_t)screen_w, (uint16_t)(screen_h - 20)))
                    {
                        lv_photo_cache_begin(photo_cfig.pic_pname, (uint16_t)screen_w, (uint16_t)(screen_h - 20));
                        ESP_LOGI(PHOTO_TAG, "PNG decoder call begin path=%s", photo_cfig.pic_pname);
                        png_decode(photo_cfig.pic_pname,screen_w,screen_h - 20,(void *)lv_pic_png_bmp_jpeg_decode);    /* PNG decode */
                        ESP_LOGI(PHOTO_TAG, "PNG decoder call returned ok=%u", photo_decode_ok ? 1U : 0U);
                        lv_photo_cache_end();
                    }
                    break;
                default:
                    photo_cfig.pic_state = PIC_NEXT;                                                                 /* Non image format */
                    break;
            }

            ESP_LOGI(PHOTO_TAG,
                     "decode pipeline end type=0x%02x ok=%u elapsed_ms=%lu path=%s",
                     (unsigned int)file_type,
                     photo_decode_ok ? 1U : 0U,
                     (unsigned long)((xTaskGetTickCount() - decode_start) * portTICK_PERIOD_MS),
                     photo_cfig.pic_pname);
            photo_log_runtime("decode_pipeline_end");

            if (lv_photo_is_supported_type(file_type))
            {
                if (!photo_decode_ok)
                {
                    ESP_LOGE(PHOTO_TAG,
                             "decode failed index=%u total=%u prior_failures=%u type=0x%02x path=%s",
                             (unsigned int)photo_cfig.pic_curindex,
                             (unsigned int)photo_cfig.pic_totpicnum,
                             (unsigned int)consecutive_failures,
                             (unsigned int)file_type,
                             photo_cfig.pic_pname);
                    photo_switching = false;
                    consecutive_failures++;
                    photo_cfig.pic_curindex++;

                    if (consecutive_failures >= photo_cfig.pic_totpicnum)
                    {
                        ESP_LOGE(PHOTO_TAG, "pic task stopping because every image failed to decode");
                        photo_cfig.pic_start = 0;
                        photo_switching = false;
                        if (!photo_closing && lvgl_mux_lock(200))
                        {
                            lv_msgbox("No decodable images");
                            lvgl_mux_unlock();
                        }
                        lv_request_active_app_close_from_task(lv_pic_del);
                        photo_sd_session_release();
                        PICTask_Handler = NULL;
                        vTaskDelete(NULL);
                    }

                    if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
                    {
                        photo_cfig.pic_curindex = 0;
                    }

                    vTaskDelay(10);
                    continue;
                }
                consecutive_failures = 0;
            }
            else
            {
                ESP_LOGW(PHOTO_TAG, "unsupported file type=0x%02x path=%s", (unsigned int)file_type, photo_cfig.pic_pname);
                consecutive_failures++;
                if (consecutive_failures >= photo_cfig.pic_totpicnum)
                {
                    ESP_LOGE(PHOTO_TAG, "pic task stopping because no supported file remained");
                    photo_cfig.pic_start = 0;
                    photo_switching = false;
                    lv_request_active_app_close_from_task(lv_pic_del);
                    photo_sd_session_release();
                    PICTask_Handler = NULL;
                    vTaskDelete(NULL);
                }
            }

            while (photo_cfig.pic_start)
            {
                if (lv_smail_icon_get_state(TF_STATE) == 0 && photo_cfig.pic_start == 0x01)
                {
                    photo_sd_lost();
                    break;
                }

                if (photo_cfig.pic_state == PIC_PREV)
                {
                    if (photo_cfig.pic_curindex)
                    {
                        photo_cfig.pic_curindex--;
                    }
                    else
                    {
                        photo_cfig.pic_curindex = photo_cfig.pic_totpicnum - 1;
                    }

                    photo_cfig.pic_state = PIC_NULL;
                    break;
                }
                else if (photo_cfig.pic_state == PIC_NEXT)
                {
                    photo_cfig.pic_curindex++;

                    if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
                    {
                        photo_cfig.pic_curindex = 0;
                    }

                    photo_cfig.pic_state = PIC_NULL;
                    break;
                }
                vTaskDelay(10);
            }

        }
		/* 取消选中SD卡 */
    }
}

/**
 * @brief  相册播放事件回调
 * @param  *e ：事件相关参数的集合，它包含了该事件的所有数据
 * @return 无
 */
static void pic_play_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);      /* 获取触发源 */
    lv_event_code_t code = lv_event_get_code(e);    /* 获取事件类型 */
    
    if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    if (photo_switching || photo_cfig.pic_state != PIC_NULL)
    {
        return;
    }

    if (target == photo_cfig.photo_obj_t.photo_next || target == photo_next_hot)                   /* 下一张 */
    {
        photo_switching = true;
        photo_cfig.pic_state = PIC_NEXT;
        lv_photo_show_black();
    }
    else if (target == photo_cfig.photo_obj_t.photo_pre || target == photo_pre_hot)              /* 上一张 */
    {
        photo_switching = true;
        photo_cfig.pic_state = PIC_PREV;
        lv_photo_show_black();
    }
}

void lv_photo_ui(void)
{
    ESP_LOGI(PHOTO_TAG,
             "open requested pic_task=%p video_task=%p tf_state=%u",
             (void *)PICTask_Handler,
             (void *)VIDEOTask_Handler,
             (unsigned int)lv_smail_icon_get_state(TF_STATE));
    photo_log_runtime("ui_enter");
    photo_log_lvgl_memory("ui_enter");

    if (PICTask_Handler != NULL || VIDEOTask_Handler != NULL)
    {
        ESP_LOGW(PHOTO_TAG, "open rejected because media task is still active");
        lv_msgbox("Media closing");
        return ;
    }

    if (!sd_local_session_begin())
    {
        ESP_LOGE(PHOTO_TAG, "open failed because SD local session could not start");
        lv_msgbox("SD device not detected");
        return;
    }
    photo_sd_session = true;

    uint8_t tf_state = lv_smail_icon_get_state(TF_STATE);
    bool sd_mounted = tf_state ? sd_card_is_mounted() : false;
    ESP_LOGI(PHOTO_TAG, "SD session ready tf_state=%u mounted=%u", (unsigned int)tf_state, sd_mounted ? 1U : 0U);

    if (tf_state && sd_mounted)
    {
        FRESULT res;

        photo_switching = false;

        photo_cfig.pic_curindex = 0;
        res = sd_f_opendir(&photo_cfig.picdir, "0:/PICTURE");

        if (res == FR_OK)
        {
            sd_f_closedir(&photo_cfig.picdir);
        }

        if (res != FR_OK)
        {
            ESP_LOGE(PHOTO_TAG, "PICTURE directory open failed res=%d", (int)res);
            photo_sd_session_release();
            lv_msgbox("PICTURE folder error");
            return ;
        }
        
        photo_cfig.pic_totpicnum = pic_get_tnum("0:/PICTURE");
        ESP_LOGI(PHOTO_TAG, "initial picture count=%u", (unsigned int)photo_cfig.pic_totpicnum);

        if (photo_cfig.pic_totpicnum == 0)
        {
            ESP_LOGW(PHOTO_TAG, "open stopped because PICTURE contains no supported images");
            photo_sd_session_release();
            lv_msgbox("No pic files");
            return ;
        }

        photo_cfig.pic_picfileinfo = (FILINFO *)malloc(sizeof(FILINFO));
        photo_cfig.pic_pname = malloc(255 * 2 + 1);
        photo_cfig.pic_picoffsettbl = NULL;
        ESP_LOGI(PHOTO_TAG,
                 "photo metadata allocation file_info=%p path=%p",
                 (void *)photo_cfig.pic_picfileinfo,
                 (void *)photo_cfig.pic_pname);

        if (!photo_cfig.pic_picfileinfo || !photo_cfig.pic_pname)
        {
            ESP_LOGE(PHOTO_TAG, "photo metadata allocation failed");
            photo_log_runtime("metadata_alloc_failed");
            free(photo_cfig.pic_picfileinfo);
            free(photo_cfig.pic_pname);
            free(photo_cfig.pic_picoffsettbl);
            photo_cfig.pic_picfileinfo = NULL;
            photo_cfig.pic_pname = NULL;
            photo_cfig.pic_picoffsettbl = NULL;
            photo_sd_session_release();
            lv_msgbox("memory allocation failed");
            return ;
        }

#if PHOTO_DIAG_SD_ENABLED
        photo_diag_file_open("photo");
        ESP_LOGI(PHOTO_TAG, "SD file logging ready path=%s opened=%u", PHOTO_DIAG_FILE_PATH, photo_diag_file_opened ? 1U : 0U);
        photo_log_runtime("file_logging_ready");
#endif

        /* 隐藏box */
        lv_hidden_box();

        photo_cfig.pic_start = 0x01;
        ESP_LOGI(PHOTO_TAG, "creating photo root object");
        photo_log_lvgl_memory("before_root_create");
        photo_cfig.photo_box = lv_obj_create(lv_scr_act());
        ESP_LOGI(PHOTO_TAG, "photo root object created ptr=%p", (void *)photo_cfig.photo_box);
        lv_obj_set_size(photo_cfig.photo_box,lv_obj_get_width(lv_scr_act()),lv_obj_get_height(lv_scr_act()));
        lv_obj_set_style_bg_color(photo_cfig.photo_box, lv_color_make(0,0,0), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(photo_cfig.photo_box,LV_OPA_100,LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(photo_cfig.photo_box,LV_OPA_0,LV_STATE_DEFAULT);
        lv_obj_set_style_radius(photo_cfig.photo_box, 0, LV_STATE_DEFAULT);
        lv_obj_set_pos(photo_cfig.photo_box,0,0);
        lv_obj_clear_flag(photo_cfig.photo_box, LV_OBJ_FLAG_SCROLLABLE);

        app_obj_general.del_parent = photo_cfig.photo_box;
        app_obj_general.APP_Function = lv_pic_del;
        app_obj_general.app_state = NOT_DEL_STATE;
        app_obj_general.requires_sd = 1;
        photo_closing = false;

        photo_cfig.photo_obj_t.photo_img = lv_img_create(photo_cfig.photo_box);
        ESP_LOGI(PHOTO_TAG, "photo image object created ptr=%p", (void *)photo_cfig.photo_obj_t.photo_img);
        lv_obj_set_style_bg_color(photo_cfig.photo_obj_t.photo_img, lv_color_make(50,52,67), LV_STATE_DEFAULT);
        lv_obj_add_flag(photo_cfig.photo_obj_t.photo_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(photo_cfig.photo_obj_t.photo_img);
        lv_obj_move_background(photo_cfig.photo_obj_t.photo_img);
        photo_cfig.photo_obj_t.photo_number = NULL;

        int hot_w = lv_obj_get_width(lv_scr_act()) / 16;
        int hot_h = lv_obj_get_height(lv_scr_act()) / 8;

        if (hot_w < 24)
        {
            hot_w = 24;
        }

        if (hot_w > 36)
        {
            hot_w = 36;
        }

        if (hot_h < 36)
        {
            hot_h = 36;
        }

        if (hot_h > 56)
        {
            hot_h = 56;
        }

        photo_pre_hot = lv_obj_create(photo_cfig.photo_box);
        lv_obj_set_size(photo_pre_hot, hot_w, hot_h);
        lv_obj_align(photo_pre_hot, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(photo_pre_hot, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(photo_pre_hot, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(photo_pre_hot, 0, LV_STATE_DEFAULT);
        lv_obj_clear_flag(photo_pre_hot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(photo_pre_hot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(photo_pre_hot, pic_play_event_cb, LV_EVENT_ALL, NULL);

        photo_next_hot = lv_obj_create(photo_cfig.photo_box);
        lv_obj_set_size(photo_next_hot, hot_w, hot_h);
        lv_obj_align(photo_next_hot, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(photo_next_hot, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(photo_next_hot, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(photo_next_hot, 0, LV_STATE_DEFAULT);
        lv_obj_clear_flag(photo_next_hot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(photo_next_hot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(photo_next_hot, pic_play_event_cb, LV_EVENT_ALL, NULL);

        photo_cfig.photo_obj_t.photo_pre = lv_img_create(photo_cfig.photo_box);
        lv_img_set_src(photo_cfig.photo_obj_t.photo_pre,&Vectorback_pro);
        lv_obj_align(photo_cfig.photo_obj_t.photo_pre,LV_ALIGN_LEFT_MID,0,0);
        lv_obj_set_size(photo_cfig.photo_obj_t.photo_pre, Vectorback_pro.header.w, Vectorback_pro.header.h);
        lv_obj_add_flag(photo_cfig.photo_obj_t.photo_pre,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(photo_cfig.photo_obj_t.photo_pre, pic_play_event_cb, LV_EVENT_ALL, NULL);

        photo_cfig.photo_obj_t.photo_next = lv_img_create(photo_cfig.photo_box);
        lv_img_set_src(photo_cfig.photo_obj_t.photo_next,&Vectorback_next);
        lv_obj_align(photo_cfig.photo_obj_t.photo_next,LV_ALIGN_RIGHT_MID,0,0);
        lv_obj_set_size(photo_cfig.photo_obj_t.photo_next, Vectorback_next.header.w, Vectorback_next.header.h);
        lv_obj_add_flag(photo_cfig.photo_obj_t.photo_next,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(photo_cfig.photo_obj_t.photo_next, pic_play_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_move_foreground(back_btn);
        ESP_LOGI(PHOTO_TAG,
                 "photo controls ready prev_hot=%p next_hot=%p prev=%p next=%p",
                 (void *)photo_pre_hot,
                 (void *)photo_next_hot,
                 (void *)photo_cfig.photo_obj_t.photo_pre,
                 (void *)photo_cfig.photo_obj_t.photo_next);
        photo_log_lvgl_memory("ui_objects_ready");
        photo_log_runtime("ui_objects_ready");

        if (indev_keypad != NULL)
        {
            lv_group_add_obj(ctrl_g, photo_cfig.photo_obj_t.photo_pre);
            lv_group_add_obj(ctrl_g, photo_cfig.photo_obj_t.photo_next);
            lv_group_focus_obj(photo_cfig.photo_obj_t.photo_pre);  /* 聚焦第一个APP */
        }

        if (PICTask_Handler == NULL)
        {
            BaseType_t task_result = xTaskCreatePinnedToCore((TaskFunction_t )pic,
                                    (const char*    )"pic",
                                    (uint16_t       )PIC_STK_SIZE,
                                    (void*          )NULL,
                                    (UBaseType_t    )PIC_PRIO,
                                    (TaskHandle_t*  )&PICTask_Handler,
                                    (BaseType_t     ) 1);
            if (task_result != pdPASS)
            {
                ESP_LOGE(PHOTO_TAG, "pic task creation failed result=%ld", (long)task_result);
                photo_log_runtime("task_create_failed");
                photo_cfig.pic_start = 0;
                PICTask_Handler = NULL;
                photo_sd_session_release();
                lv_pic_del();
                lv_msgbox("Task creation failed");
            }
            else
            {
                ESP_LOGI(PHOTO_TAG, "pic task created handle=%p", (void *)PICTask_Handler);
            }
        }
		/* 取消选中SD卡 */
    }
    else
    {
        ESP_LOGE(PHOTO_TAG, "open failed after SD check tf_state=%u mounted=%u", (unsigned int)tf_state, sd_mounted ? 1U : 0U);
        photo_sd_session_release();
        lv_msgbox("SD device not detected");
    }
}

/**
  * @brief  del pic
  * @param  None
  * @retval None
  */
static void photo_finish_close(void)
{
    ESP_LOGI(PHOTO_TAG, "finish close begin");
    photo_log_runtime("finish_close_begin");
    photo_log_lvgl_memory("finish_close_begin");

    if (photo_close_timer != NULL)
    {
        lv_timer_del(photo_close_timer);
        photo_close_timer = NULL;
    }

    photo_cache_save_enabled = false;
    photo_decode_ok = false;
    photo_switching = false;
    photo_cfig.pic_start = 0;
    photo_cfig.pic_state = PIC_NULL;

    photo_sd_session_release();

    photo_cache_save_enabled = false;
    lv_img_cache_invalidate_src(&img_pic_dsc);
    lv_photo_free_img_buf();

    if (photo_cfig.photo_box != NULL && lv_obj_is_valid(photo_cfig.photo_box))
    {
        lv_obj_del(photo_cfig.photo_box);
    }

    photo_cfig.photo_box = NULL;
    app_obj_general.del_parent = NULL;
    app_obj_general.APP_Function = NULL;
    app_obj_general.app_state = NOT_DEL_STATE;
    app_obj_general.requires_sd = 0;
    photo_cfig.photo_obj_t.photo_img = NULL;
    photo_cfig.photo_obj_t.photo_number = NULL;
    photo_cfig.photo_obj_t.photo_pre = NULL;
    photo_cfig.photo_obj_t.photo_next = NULL;
    photo_pre_hot = NULL;
    photo_next_hot = NULL;

    if (photo_cfig.pic_picfileinfo || photo_cfig.pic_pname || photo_cfig.pic_picoffsettbl)
    {
        free(photo_cfig.pic_picfileinfo);
        free(photo_cfig.pic_pname);
        free(photo_cfig.pic_picoffsettbl);
        photo_cfig.pic_picfileinfo = NULL;
        photo_cfig.pic_pname = NULL;
        photo_cfig.pic_picoffsettbl = NULL;
    }

    photo_cfig.pic_totpicnum = 0;
    photo_cfig.pic_curindex = 0;
    photo_closing = false;

    lv_display_box();
    photo_log_lvgl_memory("finish_close_end");
    photo_log_runtime("finish_close_end");
    ESP_LOGI(PHOTO_TAG, "finish close complete");
    photo_diag_file_close(true);
}

static void photo_close_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (PICTask_Handler == NULL)
    {
        photo_finish_close();
    }
}

void lv_pic_del(void)
{
    ESP_LOGI(PHOTO_TAG,
             "close requested closing=%u pic_task=%p switching=%u decode_ok=%u",
             photo_closing ? 1U : 0U,
             (void *)PICTask_Handler,
             photo_switching ? 1U : 0U,
             photo_decode_ok ? 1U : 0U);

    if (photo_closing)
    {
        return;
    }

    photo_closing = true;
    photo_cache_save_enabled = false;
    photo_switching = false;
    photo_cfig.pic_start = 0;
    photo_cfig.pic_state = PIC_NULL;

    if (PICTask_Handler != NULL)
    {
        photo_close_timer = lv_timer_create(photo_close_timer_cb, 20, NULL);
        if (photo_close_timer == NULL)
        {
            ESP_LOGE(PHOTO_TAG, "close timer allocation failed");
            photo_log_lvgl_memory("close_timer_alloc_failed");
            photo_diag_file_close(true);
            photo_closing = false;
        }
        else
        {
            ESP_LOGI(PHOTO_TAG, "waiting for pic task to stop before closing UI");
        }
        return;
    }

    photo_finish_close();
}
