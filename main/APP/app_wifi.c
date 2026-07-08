/**
 ******************************************************************************
 * @file        app_wifi.c
 * @version     V1.0
 * @brief       LVGL WIFI APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "app_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_rtc.h"
#include <stdlib.h>
#include <string.h>

LV_IMG_DECLARE(wifi0)
LV_IMG_DECLARE(wifi1)
LV_IMG_DECLARE(wifi2)
LV_IMG_DECLARE(wifi3)
LV_IMG_DECLARE(lv_wifi)
LV_IMG_DECLARE(l_wifi)

LV_FONT_DECLARE(Font11chn)
LV_FONT_DECLARE(Font12WIFI)
LV_FONT_DECLARE(Font10WIFI)

static const char *TAG = "WIFI_APP";         /* 日志标签定义 */
static wifi_ui_t wifi_ui;                    /* WiFi界面组件集合 */
extern lv_obj_t *back_btn;                   /* 外部声明的返回按钮 */

/**************************** 全局变量定义 ******************************/
static lv_obj_t *kb = NULL;                     /* 键盘对象 */
static volatile bool wifi_ui_active = false;    /* WiFi界面激活状态标志 */
static bool wifi_initialized = false;           /* WiFi驱动初始化标志 */
static bool event_handlers_registered = false;  /* 事件处理器注册标志 */
static bool wifi_back_event_registered = false;
static bool wifi_started = false;
static volatile bool wifi_scan_running = false;
static volatile bool wifi_connect_wait_running = false;
static lv_obj_t *status_label = NULL;           /* 连接状态标签 */
static TimerHandle_t status_timer = NULL;       /* 定时器句柄 */
static bool status_shown = false;               /* 状态信息是否已显示标志 */
static bool styles_initialized = false;

/***************************** 样式定义 *******************************/
static lv_style_t box_style;           /* 容器样式 */
static lv_style_t btn_style;           /* 按钮样式 */
static lv_style_t list_style;          /* 列表样式 */
static lv_style_t title_style;         /* 标题样式 */
static lv_style_t ta_style;            /* 输入框样式 */

/**-************************* WiFi配置参数 ******************************/
#define WIFI_CONNECTED_BIT BIT0            /* WiFi连接成功标志位 */
#define WIFI_FAIL_BIT      BIT1            /* WiFi连接失败标志位 */
#define MAX_PASSWORD_LEN 64                /* 最大密码长度 */
#define WIFI_SAVED_MAX 8
#define WIFI_SAVED_MAGIC 0x57494649U
#define WIFI_SAVED_NAMESPACE "wifi_store"
#define WIFI_SAVED_KEY "saved"
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_WORK_TASK_STACK 4096
#define WIFI_WORK_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
EventGroupHandle_t wifi_event_group;       /* WiFi事件组句柄 */

typedef struct {
    char ssid[33];
    char password[MAX_PASSWORD_LEN + 1];
    uint8_t authmode;
} saved_wifi_item_t;

typedef struct {
    uint32_t magic;
    uint8_t count;
    saved_wifi_item_t items[WIFI_SAVED_MAX];
} saved_wifi_store_t;

typedef struct {
    esp_err_t err;
    uint16_t ap_count;
    wifi_ap_record_t *ap_list;
} wifi_scan_result_t;

typedef struct {
    char ssid[33];
    char password[MAX_PASSWORD_LEN + 1];
    wifi_auth_mode_t authmode;
} wifi_connect_wait_t;

typedef struct {
    bool success;
    char ssid[33];
} wifi_connect_result_t;

/************************** 按钮定义 **************************/
static const char *connect_btns[] = {"Connect", "Cancel", ""}; /* 连接对话框按钮 */

/**************************** 函数声明 *****************************/
static void wifi_connect_event(lv_event_t *e);
static void conn_btn_event(lv_event_t *e);
static void ap_free_event(lv_event_t *e);
static void conn_box_delete_event(lv_event_t *e);
static void show_connect_result(bool success, const char *ssid);
static void async_update_status_label(const char *text);
static void clear_status_label(TimerHandle_t xTimer);
static void wifi_scan_handler(void);
static bool wifi_start_sta(void);
static bool wifi_start_scan(void);
static void wifi_scan_task(void *arg);
static void wifi_scan_result_cb(void *arg);
static void wifi_connect_wait_task(void *arg);
static void wifi_connect_result_cb(void *arg);
static void style_init(void);
static void ta_event_cb(lv_event_t *e);
static void scan_btn_event(lv_event_t *e);
static const void* get_wifi_icon(int rssi);                     
static void async_update_status_label_cb(void *arg);
static void back_button_event(lv_event_t *e);
static bool wifi_saved_find_password(const char *ssid, char *password, size_t password_size);
static bool wifi_saved_remember(const char *ssid, const char *password, wifi_auth_mode_t authmode);
void wifi_module_init(void);

/**
 * @brief       AP信息释放事件
 * @param       e 事件对象
 * @note        在控件删除时释放关联的AP记录内存
 */
static void ap_free_event(lv_event_t *e) 
{
    void *data = lv_event_get_user_data(e);
    if (data) 
	{
        free(data);
        ESP_LOGI(TAG, "Freed AP record memory");
    }
}

static void conn_box_delete_event(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);

    if (target == wifi_ui.conn_box)
    {
        wifi_ui.conn_box = NULL;
        wifi_ui.pwd_ta = NULL;
    }
}

static void wifi_saved_init_store(saved_wifi_store_t *store)
{
    memset(store, 0, sizeof(*store));
    store->magic = WIFI_SAVED_MAGIC;
}

static bool wifi_saved_load_store(saved_wifi_store_t *store)
{
    nvs_handle_t handle;
    size_t size = sizeof(*store);

    wifi_saved_init_store(store);

    if (nvs_open(WIFI_SAVED_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    {
        return false;
    }

    esp_err_t err = nvs_get_blob(handle, WIFI_SAVED_KEY, store, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != sizeof(*store) || store->magic != WIFI_SAVED_MAGIC || store->count > WIFI_SAVED_MAX)
    {
        wifi_saved_init_store(store);
        return false;
    }

    return true;
}

static bool wifi_saved_save_store(const saved_wifi_store_t *store)
{
    nvs_handle_t handle;

    if (nvs_open(WIFI_SAVED_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    {
        return false;
    }

    esp_err_t err = nvs_set_blob(handle, WIFI_SAVED_KEY, store, sizeof(*store));
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err == ESP_OK;
}

static bool wifi_saved_find_password(const char *ssid, char *password, size_t password_size)
{
    saved_wifi_store_t store;

    if (ssid == NULL || password == NULL || password_size == 0)
    {
        return false;
    }

    wifi_saved_load_store(&store);

    for (uint8_t i = 0; i < store.count; i++)
    {
        if (strcmp(store.items[i].ssid, ssid) == 0 && store.items[i].password[0] != '\0')
        {
            strncpy(password, store.items[i].password, password_size - 1);
            password[password_size - 1] = '\0';
            return true;
        }
    }

    return false;
}

static bool wifi_saved_remember(const char *ssid, const char *password, wifi_auth_mode_t authmode)
{
    saved_wifi_store_t store;
    saved_wifi_item_t item = {0};
    int found = -1;

    if (ssid == NULL || password == NULL || ssid[0] == '\0' || password[0] == '\0')
    {
        return false;
    }

    wifi_saved_load_store(&store);

    strncpy(item.ssid, ssid, sizeof(item.ssid) - 1);
    strncpy(item.password, password, sizeof(item.password) - 1);
    item.authmode = (uint8_t)authmode;

    for (uint8_t i = 0; i < store.count; i++)
    {
        if (strcmp(store.items[i].ssid, item.ssid) == 0)
        {
            found = i;
            break;
        }
    }

    if (found == 0)
    {
        store.items[0] = item;
    }
    else if (found > 0)
    {
        memmove(&store.items[1], &store.items[0], sizeof(saved_wifi_item_t) * found);
        store.items[0] = item;
    }
    else
    {
        if (store.count < WIFI_SAVED_MAX)
        {
            store.count++;
        }

        if (store.count > 1)
        {
            memmove(&store.items[1], &store.items[0], sizeof(saved_wifi_item_t) * (store.count - 1));
        }

        store.items[0] = item;
    }

    return wifi_saved_save_store(&store);
}

/**
 * @brief       WiFi扫描处理
 * @note        执行WiFi扫描并更新AP列表
 */
static void wifi_scan_handler(void) 
{
    wifi_start_scan();
    return;

    if (esp_wifi_scan_start(NULL, true) != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi scan failed");
        return;
    }

    uint16_t ap_count = 0;
    wifi_ap_record_t *ap_list = NULL;
    
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) 
	{
        ESP_LOGW(TAG, "No AP found");
        return;
    }
    
    ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) 
	{
        ESP_LOGE(TAG, "AP list malloc failed");
        return;
    }
    
    esp_err_t ret = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    if (ret != ESP_OK) 
	{
        ESP_LOGE(TAG, "Get AP records failed: %s", esp_err_to_name(ret));
        free(ap_list);
        return;
    }
    
    lv_obj_clean(wifi_ui.list);
    
    for (int i = 0; i < ap_count; i++) {
        wifi_ap_record_t *ap_copy = malloc(sizeof(wifi_ap_record_t));
        if (!ap_copy)
        {
            ESP_LOGE(TAG, "AP record malloc failed");
            break;
        }
        memcpy(ap_copy, &ap_list[i], sizeof(wifi_ap_record_t));
        
        // 创建列表项容器
        lv_obj_t *container = lv_obj_create(wifi_ui.list);
        lv_obj_set_size(container, LV_PCT(100), 40);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 5, 0);

        // 添加信号强度图标
        lv_obj_t *icon = lv_img_create(container);
        lv_img_set_src(icon, get_wifi_icon(ap_copy->rssi));
        lv_obj_set_size(icon, 24, 24);
        lv_obj_set_align(icon, LV_ALIGN_LEFT_MID);

        // 添加SSID标签
        lv_obj_t *label = lv_label_create(container);
        lv_label_set_text(label, (const char *)ap_copy->ssid);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
		//lv_obj_set_style_text_font(label, &Font10WIFI, 0);  // 修改为Font10WIFI
        lv_obj_set_style_pad_left(label, 10, 0);

        // 添加点击事件
        lv_obj_add_event_cb(container, wifi_connect_event, LV_EVENT_CLICKED, ap_copy);
        lv_obj_add_event_cb(container, ap_free_event, LV_EVENT_DELETE, ap_copy);
    }
    free(ap_list);
}

static bool wifi_start_sta(void)
{
    wifi_module_init();
    esp_wifi_set_mode(WIFI_MODE_STA);

    if (!wifi_started)
    {
        esp_err_t ret = esp_wifi_start();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
            return false;
        }
        wifi_started = true;
    }

    return true;
}

static bool wifi_start_scan(void)
{
    if (wifi_scan_running)
    {
        return true;
    }

    if (!wifi_start_sta())
    {
        async_update_status_label("WiFi启动失败");
        return false;
    }

    wifi_scan_running = true;
    async_update_status_label("扫描WIFI...");

    BaseType_t ret = xTaskCreatePinnedToCore(wifi_scan_task,
                                             "wifi_scan",
                                             WIFI_WORK_TASK_STACK,
                                             NULL,
                                             WIFI_WORK_TASK_PRIORITY,
                                             NULL,
                                             0);
    if (ret != pdPASS)
    {
        wifi_scan_running = false;
        async_update_status_label("WiFi扫描失败");
        return false;
    }

    return true;
}

static void wifi_scan_task(void *arg)
{
    (void)arg;

    wifi_scan_result_t *result = calloc(1, sizeof(wifi_scan_result_t));
    if (result == NULL)
    {
        wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    result->err = esp_wifi_scan_start(NULL, true);
    if (result->err == ESP_OK)
    {
        esp_wifi_scan_get_ap_num(&result->ap_count);
        if (result->ap_count > 0)
        {
            result->ap_list = malloc(sizeof(wifi_ap_record_t) * result->ap_count);
            if (result->ap_list == NULL)
            {
                result->err = ESP_ERR_NO_MEM;
                result->ap_count = 0;
            }
            else
            {
                result->err = esp_wifi_scan_get_ap_records(&result->ap_count, result->ap_list);
            }
        }
    }

    if (lv_async_call(wifi_scan_result_cb, result) != LV_RES_OK)
    {
        free(result->ap_list);
        free(result);
        wifi_scan_running = false;
    }

    vTaskDelete(NULL);
}

static void wifi_scan_result_cb(void *arg)
{
    wifi_scan_result_t *result = (wifi_scan_result_t *)arg;
    wifi_scan_running = false;

    if (result == NULL)
    {
        return;
    }

    if (!wifi_ui_active || wifi_ui.list == NULL || !lv_obj_is_valid(wifi_ui.list))
    {
        free(result->ap_list);
        free(result);
        return;
    }

    lv_obj_clean(wifi_ui.list);

    if (result->err != ESP_OK)
    {
        async_update_status_label("WiFi扫描失败");
        free(result->ap_list);
        free(result);
        return;
    }

    if (result->ap_count == 0)
    {
        async_update_status_label("没有找到WiFi");
        free(result->ap_list);
        free(result);
        return;
    }

    for (int i = 0; i < result->ap_count; i++)
    {
        wifi_ap_record_t *ap_copy = malloc(sizeof(wifi_ap_record_t));
        if (!ap_copy)
        {
            ESP_LOGE(TAG, "AP record malloc failed");
            break;
        }
        memcpy(ap_copy, &result->ap_list[i], sizeof(wifi_ap_record_t));

        lv_obj_t *container = lv_obj_create(wifi_ui.list);
        lv_obj_set_size(container, LV_PCT(100), 40);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 5, 0);

        lv_obj_t *icon = lv_img_create(container);
        lv_img_set_src(icon, get_wifi_icon(ap_copy->rssi));
        lv_obj_set_size(icon, 24, 24);
        lv_obj_set_align(icon, LV_ALIGN_LEFT_MID);

        lv_obj_t *label = lv_label_create(container);
        lv_label_set_text(label, (const char *)ap_copy->ssid);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_left(label, 10, 0);

        lv_obj_add_event_cb(container, wifi_connect_event, LV_EVENT_CLICKED, ap_copy);
        lv_obj_add_event_cb(container, ap_free_event, LV_EVENT_DELETE, ap_copy);
    }

    async_update_status_label("");
    free(result->ap_list);
    free(result);
}

static void wifi_connect_wait_task(void *arg)
{
    wifi_connect_wait_t *wait = (wifi_connect_wait_t *)arg;
    EventBits_t bits = 0;

    if (wait == NULL)
    {
        wifi_connect_wait_running = false;
        vTaskDelete(NULL);
        return;
    }

    if (wifi_event_group)
    {
        bits = xEventGroupWaitBits(wifi_event_group,
                                   WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                   pdFALSE,
                                   pdFALSE,
                                   pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    }

    bool success = (bits & WIFI_CONNECTED_BIT) != 0;
    if (success)
    {
        wifi_saved_remember(wait->ssid, wait->password, wait->authmode);
    }

    wifi_connect_result_t *result = calloc(1, sizeof(wifi_connect_result_t));
    if (result != NULL)
    {
        result->success = success;
        strncpy(result->ssid, wait->ssid, sizeof(result->ssid) - 1);
        if (lv_async_call(wifi_connect_result_cb, result) != LV_RES_OK)
        {
            free(result);
            wifi_connect_wait_running = false;
        }
    }
    else
    {
        wifi_connect_wait_running = false;
    }

    free(wait);
    vTaskDelete(NULL);
}

static void wifi_connect_result_cb(void *arg)
{
    wifi_connect_result_t *result = (wifi_connect_result_t *)arg;

    if (result != NULL)
    {
        show_connect_result(result->success, result->ssid);
        free(result);
    }

    if (wifi_ui.conn_box && lv_obj_is_valid(wifi_ui.conn_box))
    {
        lv_msgbox_close(wifi_ui.conn_box);
    }

    wifi_connect_wait_running = false;
}

/**
 * @brief       WiFi连接事件
 * @param       e 事件对象
 * @note        处理AP选择事件，弹出密码输入框
 */
static void wifi_connect_event(lv_event_t *e) 
{
    wifi_ap_record_t *ap = (wifi_ap_record_t *)lv_event_get_user_data(e);
    if (!ap) 
	{
        ESP_LOGE(TAG, "Invalid AP record");
        return;
    }
    
    /* 创建连接对话框 */
    if (wifi_ui.conn_box && lv_obj_is_valid(wifi_ui.conn_box))
    {
        lv_msgbox_close(wifi_ui.conn_box);
    }

    wifi_ui.conn_box = lv_msgbox_create(lv_scr_act(), "Enter Password", (const char *)ap->ssid, connect_btns, true);
    if (wifi_ui.conn_box == NULL)
    {
        ESP_LOGE(TAG, "Create connection box failed");
        wifi_ui.pwd_ta = NULL;
        return;
    }
    lv_obj_add_event_cb(wifi_ui.conn_box, conn_box_delete_event, LV_EVENT_DELETE, NULL);
	//wifi_ui.conn_box = lv_msgbox_create(lv_scr_act(), "输入密码", (const char *)ap->ssid, connect_btns, true);
    lv_obj_set_size(wifi_ui.conn_box, 260, 180);
    lv_obj_align(wifi_ui.conn_box, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_add_style(wifi_ui.conn_box, &box_style, 0);

	lv_obj_t *close_btn = lv_msgbox_get_close_btn(wifi_ui.conn_box); /* 获取按钮矩阵部分 */
	lv_obj_set_style_text_color(close_btn, lv_color_hex(0x2F2E2D),LV_STATE_DEFAULT); 
	lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xEBEBEB), LV_STATE_DEFAULT); // 设置背景色
	// lv_obj_set_style_bg_opa(close_btn, 0, LV_PART_ITEMS); /* 设置按钮背景透明度 */
	// lv_obj_set_style_shadow_width(close_btn, 0, LV_PART_ITEMS); /* 去除按钮阴影 */
	// lv_obj_set_style_text_font(close_btn, &lv_font_montserrat_20, LV_PART_ITEMS);

    /* 创建密码输入框 */
    wifi_ui.pwd_ta = lv_textarea_create(wifi_ui.conn_box);
    if (wifi_ui.pwd_ta == NULL)
    {
        ESP_LOGE(TAG, "Create password textarea failed");
        lv_msgbox_close(wifi_ui.conn_box);
        return;
    }
    lv_obj_add_style(wifi_ui.pwd_ta, &ta_style, 0);
    lv_obj_set_size(wifi_ui.pwd_ta, 240, 48);
    lv_obj_align(wifi_ui.pwd_ta, LV_ALIGN_TOP_MID, 0, 50);
    lv_textarea_set_password_mode(wifi_ui.pwd_ta, false);

    char saved_password[MAX_PASSWORD_LEN + 1] = {0};
    if (wifi_saved_find_password((const char *)ap->ssid, saved_password, sizeof(saved_password)))
    {
        lv_textarea_set_text(wifi_ui.pwd_ta, saved_password);
    }
    
    /* 复制AP信息 */
    wifi_ap_record_t *conn_ap = malloc(sizeof(wifi_ap_record_t));
    if (!conn_ap)
    {
        ESP_LOGE(TAG, "Connection AP malloc failed");
        lv_msgbox_close(wifi_ui.conn_box);
        return;
    }
    memcpy(conn_ap, ap, sizeof(wifi_ap_record_t));
    
    /* 绑定事件 */
    lv_obj_add_event_cb(lv_msgbox_get_btns(wifi_ui.conn_box), conn_btn_event, LV_EVENT_CLICKED, conn_ap);
    lv_obj_add_event_cb(wifi_ui.conn_box, ap_free_event, LV_EVENT_DELETE, conn_ap);

    /* 配置输入框 */
    lv_obj_add_event_cb(wifi_ui.pwd_ta, ta_event_cb, LV_EVENT_ALL, NULL);
    //lv_textarea_set_placeholder_text(wifi_ui.pwd_ta, "Enter password...");
	lv_obj_set_style_text_font(wifi_ui.pwd_ta, &Font10WIFI, 0);  // 修改为Font10WIFI
	lv_textarea_set_placeholder_text(wifi_ui.pwd_ta, "输入密码...");  

    // 确保弹窗不覆盖返回按钮
    lv_obj_move_foreground(back_btn);
}

/**
 * @brief   连接确认按钮事件处理函数
 * @note    处理用户点击连接弹窗的确认/取消操作
 *          包含密码验证、WiFi配置、连接状态提示和超时检测
 * @param   e LVGL事件对象
 * @retval  无
 */
static void conn_btn_event(lv_event_t *e) 
{
    lv_obj_t *btnm = lv_event_get_target(e);                    /* 获取触发事件的按钮矩阵对象 */
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(btnm);      /* 获取被点击按钮的ID */
    const char *txt = lv_btnmatrix_get_btn_text(btnm, btn_id);  /* 获取按钮文本 */
    
    /* 检查是否点击了取消按钮 */
    if (strcmp(txt, "Connect") != 0) 
	{            /* 非"Connect"按钮视为取消操作 */
        lv_msgbox_close(wifi_ui.conn_box);        /* 关闭消息弹窗 */
        return;
    }
    
    /* 获取用户选择的AP信息 */
    wifi_ap_record_t *ap = (wifi_ap_record_t *)lv_event_get_user_data(e);
    if (!ap) 
	{
        LV_LOG_ERROR("Invalid AP data in connection"); /* 无效AP数据记录 */
        return;
    }
    
    /* 验证密码输入 */
    if (wifi_connect_wait_running)
    {
        return;
    }

    const char *pwd = lv_textarea_get_text(wifi_ui.pwd_ta);
    if (!pwd || strlen(pwd) == 0) 
	{               /* 空密码检查 */
        LV_LOG_WARN("No password provided");
        return;
    }
    
    /* 配置WiFi连接参数 */
    wifi_connect_wait_t *wait = calloc(1, sizeof(wifi_connect_wait_t));
    if (wait == NULL)
    {
        LV_LOG_ERROR("Connection wait malloc failed");
        return;
    }
    strncpy(wait->ssid, (const char *)ap->ssid, sizeof(wait->ssid) - 1);
    strncpy(wait->password, pwd, sizeof(wait->password) - 1);
    wait->authmode = ap->authmode;

    if (!wifi_start_sta())
    {
        free(wait);
        lv_msgbox_close(wifi_ui.conn_box);
        return;
    }

    wifi_config_t wifi_config = {0};
    
    /* 安全拷贝SSID（确保字符串终止符） */
    strncpy((char*)wifi_config.sta.ssid, 
           (char*)ap->ssid, 
           sizeof(wifi_config.sta.ssid)-1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid)-1] = '\0'; /* 强制添加终止符 */
    
    /* 安全拷贝密码（确保字符串终止符） */
    strncpy((char*)wifi_config.sta.password, 
           pwd, 
           sizeof(wifi_config.sta.password)-1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password)-1] = '\0';
    
    /* 设置认证模式 */
    wifi_config.sta.threshold.authmode = ap->authmode;
    
    /* 应用WiFi配置 */
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) 
	{
        LV_LOG_ERROR("Set config failed: %s", esp_err_to_name(ret));
        free(wait);
        lv_msgbox_close(wifi_ui.conn_box);        /* 关闭消息弹窗 */
        return;
    }

    if (wifi_event_group)
    {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }
    
    /* 发起连接请求 */
    ret = esp_wifi_connect();
    if (ret != ESP_OK) 
	{
        LV_LOG_ERROR("Connect failed: %s", esp_err_to_name(ret));
        free(wait);
        lv_msgbox_close(wifi_ui.conn_box);        /* 关闭消息弹窗 */
        return;
    }

    /* 更新连接状态提示 */
    lv_label_set_text(lv_msgbox_get_title(wifi_ui.conn_box), "Connecting...");
    lv_obj_add_state(lv_msgbox_get_btns(wifi_ui.conn_box), LV_STATE_DISABLED);

    wifi_connect_wait_running = true;
    BaseType_t task_ret = xTaskCreatePinnedToCore(wifi_connect_wait_task,
                                                  "wifi_conn",
                                                  WIFI_WORK_TASK_STACK,
                                                  wait,
                                                  WIFI_WORK_TASK_PRIORITY,
                                                  NULL,
                                                  0);
    if (task_ret != pdPASS)
    {
        wifi_connect_wait_running = false;
        free(wait);
        show_connect_result(false, (const char *)ap->ssid);
        lv_msgbox_close(wifi_ui.conn_box);
    }
    return;
}

/**
 * @brief   扫描按钮事件处理函数
 * @note    触发WiFi扫描并更新界面列表
 * @param   e LVGL事件对象
 * @retval  无
 */
static void scan_btn_event(lv_event_t *e)
{
    (void)e;
    wifi_scan_handler();
    return;
}

/**
 * @brief       获取信号强度对应图标
 */
static const void* get_wifi_icon(int rssi)
{
	if (rssi >= -50) return &wifi3;       /* 极强信号 */ 
    else if (rssi >= -60) return &wifi3;  /* 强信号 */ 
    else if (rssi >= -70) return &wifi2;  /* 中等信号 */ 
    else if (rssi >= -80) return &wifi1;  /* 弱信号 */ 
    else return &wifi0;                   /* 极弱信号 */ 
} 

/**
 * @brief       异步更新状态标签
 * @param       text 显示的文本内容
 * @note        通过LVGL异步调用安全更新状态标签
 */
static void async_update_status_label(const char *text) 
{
    if (status_label == NULL)
    {
        return;
    }

    char *text_copy = strdup(text ? text : "");
    if (text_copy == NULL)
    {
        return;
    }

    if (lv_async_call(async_update_status_label_cb, text_copy) != LV_RES_OK)
    {
        free(text_copy);
    }
}

/**
 * @brief       异步更新状态标签的回调函数
 * @param       arg 文本内容指针
 * @note        实际执行更新状态标签的操作
 */
static void async_update_status_label_cb(void *arg) 
{
    char *text = (char *)arg;
    if (text == NULL)
    {
        return;
    }

    bool is_connect_failed = strstr(text, "Connect Failed") != NULL;
    if (status_label && lv_obj_is_valid(status_label)) 
	{
        lv_label_set_text(status_label, text);
		lv_obj_set_style_text_font(status_label, &Font10WIFI, 0);
    }
    free(arg);

    // 如果是连接失败的消息，则设置标志
    if (is_connect_failed) 
	{
        status_shown = true;
    }
}

/**
 * @brief       清除状态标签的回调函数
 * @param       xTimer 定时器句柄
 * @note        实际执行清除状态标签的操作
 */
static void clear_status_label(TimerHandle_t xTimer) 
{
    if (status_label && lv_obj_is_valid(status_label)) 
	{
        lv_label_set_text(status_label, "");
    }
    status_shown = false;  // 重置标志
}

/**
 * @brief       显示连接结果
 * @param       success 连接结果(true/false)
 * @param       ssid    WiFi名称
 * @note        异步显示连接成功/失败提示框
 */
static void show_connect_result(bool success, const char *ssid) 
{
    /* 有效性检查 */
    if(!wifi_ui_active || !lv_obj_is_valid(wifi_ui.wifi_main_ui)) 
	{
        return;
    }

    char msg[100];
    if (success) {
		snprintf(msg, sizeof(msg), "连接 %s 成功", ssid);
    } else {
		snprintf(msg, sizeof(msg), "连接 %s 失败", ssid);
    }

    if (success || !status_shown)  // 只在成功或未显示失败消息时更新
	{
        async_update_status_label(msg);
    }
}

/**
 * @brief       输入框事件回调
 * @param       e 事件对象
 * @note        处理输入框聚焦/失焦事件，管理键盘显示
 */
static void ta_event_cb(lv_event_t *e) 
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    static int original_y = 0;  /* 保存原始Y坐标 */
    
    if(code == LV_EVENT_FOCUSED) 
	{
        /* 创建键盘并调整位置 */
        if(kb == NULL) 
		{
            kb = lv_keyboard_create(lv_scr_act());
            lv_obj_set_size(kb, 320, 160);
            lv_obj_set_style_radius(kb, 12, 0);
            lv_obj_set_style_bg_color(kb, lv_color_hex(0xEFEFF4), 0);
        }
        original_y = lv_obj_get_y(wifi_ui.conn_box);
        int new_y = (480 - 160 - lv_obj_get_height(wifi_ui.conn_box) - 20);
        lv_obj_set_y(wifi_ui.conn_box, new_y);
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    else if(code == LV_EVENT_DEFOCUSED) 
	{
        /* 恢复原始位置并隐藏键盘 */
        lv_obj_set_y(wifi_ui.conn_box, original_y);
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief       WiFi事件处理
 * @param       arg        未使用参数
 * @param       event_base 事件基类型
 * @param       event_id   事件ID
 * @param       event_data 事件数据
 * @note        处理WiFi连接事件和IP获取事件
 */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) 
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (wifi_event_group)
        {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }

        if (wifi_ui_active && !wifi_connect_wait_running && wifi_started)
        {
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        if (wifi_event_group)
        {
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }

        rtc_start_sntp_sync();
        return;
    }

    return;
}

/**
 * @brief       初始化WiFi模块
 * @note        初始化WiFi驱动和事件处理器
 */
void wifi_module_init(void) {
    if (!wifi_initialized) {
        nvs_flash_init();

        esp_netif_init();  
        esp_event_loop_create_default();

        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        esp_wifi_set_storage(WIFI_STORAGE_RAM);

        wifi_initialized = true;
    }

    if (!event_handlers_registered) {
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &event_handler, NULL);
        event_handlers_registered = true;
    }

    if (wifi_event_group == NULL)
    {
        wifi_event_group = xEventGroupCreate();
    }
}

/**
 * @brief       初始化样式
 * @note        创建容器、按钮、列表等控件的样式
 */
static void style_init(void) 
{
    if (styles_initialized)
    {
        return;
    }

    /* 主容器样式 */
    lv_style_init(&box_style);
    lv_style_set_radius(&box_style, 12);
    lv_style_set_bg_color(&box_style, lv_color_hex(0xEBEBEB));
    lv_style_set_pad_all(&box_style, 8);
    lv_style_set_border_width(&box_style, 1);
	lv_style_set_border_color(&box_style, lv_color_hex(0xC6C6C8));

    /* 按钮样式 */
    lv_style_init(&btn_style);
    lv_style_set_radius(&btn_style, 8);
    lv_style_set_bg_color(&btn_style, lv_color_hex(0xF7F7F7));
    lv_style_set_text_color(&btn_style, lv_color_white());
    lv_style_set_pad_hor(&btn_style, 20);
    lv_style_set_pad_ver(&btn_style, 10);

    /* 列表样式 */
    lv_style_init(&list_style);
    lv_style_set_radius(&list_style, 12);
    lv_style_set_bg_color(&list_style, lv_color_hex(0xF5F5F5 ));
	lv_style_set_bg_opa(&list_style, LV_OPA_70); /* 设置背景透明度 */

    /* 标题样式 */
    lv_style_init(&title_style);
    lv_style_set_text_font(&title_style, &lv_font_montserrat_20);
    lv_style_set_text_color(&title_style, lv_color_hex(0x1C1C1E));

    /* 输入框样式 */
    lv_style_init(&ta_style);
    lv_style_set_radius(&ta_style, 8);
    lv_style_set_bg_color(&ta_style, lv_color_hex(0xF4F4F4));
	lv_style_set_bg_opa(&ta_style, LV_OPA_100); /* 设置背景透明度 */
    lv_style_set_border_width(&ta_style, 1);
    lv_style_set_border_color(&ta_style, lv_color_hex(0xC6C6C8));
    styles_initialized = true;
}
/**
 * @brief   初始化WiFi应用界面
 * @note    创建UI元素、初始化硬件、注册事件处理器
 *          采用单例模式，重复调用会先清理旧实例
 * @param   无
 * @retval  无
 */
void wifi_app_init(void) 
{
	style_init();
    
    /* 清理旧实例 */
    if(wifi_ui.wifi_main_ui)
	{
        wifi_app_del();                           /* 调用清理函数 */
    }

    wifi_ui_active = true;                        /* 标记界面为活跃状态 */

    /* 创建主容器 */
    wifi_ui.wifi_main_ui = lv_obj_create(lv_scr_act());
    lv_obj_set_size(wifi_ui.wifi_main_ui, 320, 480);            /* 设置容器尺寸 */
    //lv_obj_add_style(wifi_ui.wifi_main_ui, &box_style, 0);  /* 应用样式 */
	lv_obj_set_style_border_width(wifi_ui.wifi_main_ui, 0, LV_STATE_DEFAULT);  /* 设置边框宽度为 0 */
    lv_obj_set_size(wifi_ui.wifi_main_ui, 320, 480);  /* 设置容器尺寸 */
    lv_obj_align(wifi_ui.wifi_main_ui, LV_ALIGN_TOP_MID, 0, 0);  /* 顶部居中对齐 */
    lv_obj_clear_flag(wifi_ui.wifi_main_ui, LV_OBJ_FLAG_SCROLLABLE); /* 禁用滚动 */

	 /* 创建标题容器 */
	 lv_obj_t *title_container = lv_obj_create(wifi_ui.wifi_main_ui);
	 lv_obj_remove_style_all(title_container); // 移除默认样式
	 lv_obj_set_size(title_container, 200, 30); // 设置容器大小
	 lv_obj_align(title_container, LV_ALIGN_TOP_MID, -50, 10); // 顶部居中
	 lv_obj_set_style_bg_opa(title_container, LV_OPA_TRANSP, 0); // 透明背景
	 lv_obj_set_style_border_width(title_container, 0, 0); // 无边框
	 lv_obj_set_flex_flow(title_container, LV_FLEX_FLOW_ROW); // 水平排列
	 lv_obj_set_flex_align(title_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // 居中对齐

	/* 添加WIFI图标 */
	lv_obj_t *wifi_icon = lv_img_create(title_container);
	lv_img_set_src(wifi_icon, &wifi3);
	lv_obj_set_size(wifi_icon, 24, 24); // 设置图标大小

	/* 添加标题文本 */
    lv_obj_t *title = lv_label_create(title_container);
    lv_label_set_text(title, "无线局域网");
    lv_obj_set_style_text_font(title, &Font12WIFI, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_pad_left(title, 10, 0); // 图标和文本间距

	lv_obj_t *scan_btn = lv_btn_create(wifi_ui.wifi_main_ui);
	lv_obj_add_style(scan_btn, &btn_style, 0);
	lv_obj_set_size(scan_btn, 60, 30);
	//lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -20, 10);
	//lv_obj_align_to(scan_btn, title, LV_ALIGN_OUT_LEFT_MID, 20, 0);
	lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0xA1DDEB), LV_STATE_DEFAULT);  /* 设置按钮背景颜色（默认） */
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x0F76BB), LV_STATE_PRESSED);  /* 设置按钮背景颜色（按下） */
	lv_obj_add_event_cb(scan_btn, scan_btn_event, LV_EVENT_RELEASED, NULL);
	lv_obj_align_to(scan_btn, title_container, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

	lv_obj_t *btn_label = lv_label_create(scan_btn);
	lv_label_set_text(btn_label, "扫描");
	lv_obj_set_style_text_font(btn_label, &Font11chn, 0);
	lv_obj_center(btn_label);    

    /* 创建AP列表 */
    wifi_ui.list = lv_list_create(wifi_ui.wifi_main_ui);
    lv_obj_set_size(wifi_ui.list, 300, 360);                 /* 设置列表尺寸 */
    lv_obj_align(wifi_ui.list, LV_ALIGN_BOTTOM_MID, 0, -30); /* 底部居中 */
    lv_obj_add_style(wifi_ui.list, &list_style, 0);      /* 应用列表样式 */
	lv_obj_set_style_border_width(wifi_ui.list, 2, LV_STATE_DEFAULT);  /* 设置边框宽度为 2 */
    
    /* 创建状态标签 */
    status_label = lv_label_create(wifi_ui.wifi_main_ui);
    wifi_module_init();
    lv_label_set_text(status_label, "");                      /* 初始为空 */
    lv_obj_add_style(status_label, &title_style, 0);      /* 应用标题样式 */
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 40);      /* 屏幕顶部中间 */
    
    wifi_start_sta();
    
    lv_hidden_box();                              /* 隐藏主界面（自定义函数）*/
    
    /* 注册全局应用对象 */
    app_obj_general.del_parent = wifi_ui.wifi_main_ui;  /* 设置父对象 */
    app_obj_general.APP_Function = wifi_app_del;        /* 设置清理回调 */
    app_obj_general.app_state = NOT_DEL_STATE;          /* 设置应用状态 */
    lv_obj_move_foreground(back_btn);                   /* 将返回按钮置顶 */
    
    // 添加返回按钮点击事件
    if (!wifi_back_event_registered)
    {
        lv_obj_add_event_cb(back_btn, back_button_event, LV_EVENT_CLICKED, NULL);
        wifi_back_event_registered = true;
    }

    wifi_start_scan();
}

/**
 * @brief   返回按钮事件处理函数
 * @note    处理返回按钮点击事件，关闭弹窗并返回主界面
 * @param   e LVGL事件对象
 * @retval  无
 */
static void back_button_event(lv_event_t *e) 
{
    if (wifi_ui.conn_box && lv_obj_is_valid(wifi_ui.conn_box)) 
	{
        lv_msgbox_close(wifi_ui.conn_box);        /* 关闭消息弹窗 */
    }
    wifi_app_del();                                 /* 清理WiFi应用资源 */
}

/**
 * @brief   清理WiFi应用资源
 * @note    删除UI元素、释放内存、停止WiFi连接
 *          恢复系统主界面显示
 * @param   无
 * @retval  无
 */
void wifi_app_del(void) 
{
    if (wifi_ui.conn_box && lv_obj_is_valid(wifi_ui.conn_box))
    {
        lv_msgbox_close(wifi_ui.conn_box);
    }

    wifi_ui_active = false;                       /* 标记界面为非活跃状态 */

    /* 解除全局应用对象绑定 */
    app_obj_general.APP_Function = NULL;          /* 清除回调函数指针 */
    app_obj_general.del_parent = NULL;            /* 清除父对象指针 */

    /* 删除主容器 */
    if (wifi_ui.wifi_main_ui && lv_obj_is_valid(wifi_ui.wifi_main_ui)) 
	{
        lv_obj_del(wifi_ui.wifi_main_ui);         /* 删除LVGL对象 */
        wifi_ui.wifi_main_ui = NULL;              /* 置空指针 */
    }

    /* 删除键盘对象 */
    status_label = NULL;

    if (kb) 
	{
        lv_obj_del(kb);                          /* 删除LVGL键盘 */
        kb = NULL;                               /* 置空指针 */
    }

    /* 停止网络连接 */
    if (wifi_started)
    {
        esp_wifi_disconnect();
        esp_wifi_stop();
        wifi_started = false;
    }

    /* 恢复主界面显示 */
    lv_display_box();                             /* 显示主界面（自定义函数） */

    /* 重置LVGL焦点组 */
    lv_group_t *group = lv_group_get_default();   /* 获取默认焦点组 */
    if(group) lv_group_remove_all_objs(group);    /* 移除所有焦点对象 */

    /* 清空状态标签 */
    async_update_status_label("");

    // 删除定时器
    if (status_timer != NULL) 
	{
        xTimerDelete(status_timer, portMAX_DELAY);
        status_timer = NULL;
    }

    if (wifi_event_group != NULL)
    {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }

    if (wifi_back_event_registered && back_btn != NULL && lv_obj_is_valid(back_btn))
    {
        lv_obj_remove_event_cb(back_btn, back_button_event);
        wifi_back_event_registered = false;
    }
}



