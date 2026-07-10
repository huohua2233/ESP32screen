/**
 ******************************************************************************
 * @file        xl9555.c
 * @version     V1.0
 * @brief       XL9555驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "xl9555.h"


const char *xl9555_tag = "xl9555";
i2c_master_dev_handle_t xl9555_handle = NULL;
static SemaphoreHandle_t xl9555_mutex = NULL;
static uint16_t xl9555_output_shadow = 0;
static bool xl9555_output_shadow_valid = false;

#define XL9555_LOCK_TIMEOUT_MS 100
#define XL9555_I2C_TIMEOUT_MS 100
#define XL9555_CONFIG_RETRY_COUNT 3
#define XL9555_CONFIG_RETRY_DELAY_MS 20

static bool xl9555_lock(void)
{
    return xl9555_mutex != NULL &&
           xSemaphoreTakeRecursive(xl9555_mutex, pdMS_TO_TICKS(XL9555_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void xl9555_unlock(void)
{
    xSemaphoreGiveRecursive(xl9555_mutex);
}

static esp_err_t xl9555_read_register(uint8_t reg, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (xl9555_handle == NULL || xl9555_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!xl9555_lock())
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = i2c_master_transmit_receive(xl9555_handle, &reg, 1, data, len,
                                                XL9555_I2C_TIMEOUT_MS);
    if (ret == ESP_OK && reg == XL9555_OUTPUT_PORT0_REG && len >= 2)
    {
        xl9555_output_shadow = ((uint16_t)data[1] << 8) | data[0];
        xl9555_output_shadow_valid = true;
    }

    xl9555_unlock();
    return ret;
}

/**
 * @brief       读取XL9555的IO值
 * @param       data:读取数据的存储区
 * @param       len:读取数据的大小
 * @retval      ESP_OK:读取成功; 其他:读取失败
 */
esp_err_t xl9555_read_byte(uint8_t *data, size_t len)
{
    return xl9555_read_register(XL9555_OUTPUT_PORT0_REG, data, len);
}

/**
 * @brief       向XL9555寄存器写入数据
 * @param       reg:寄存器地址
 * @param       data:要写入数据的存储区
 * @param       len:要写入数据的大小
 * @retval      ESP_OK:读取成功; 其他:读取失败
 */
esp_err_t xl9555_write_byte(uint8_t reg, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (xl9555_handle == NULL || xl9555_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!xl9555_lock())
    {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t *buf = malloc(1 + len);
    if (buf == NULL)
    {
        ESP_LOGE(xl9555_tag, "%s memory failed", __func__);
        xl9555_unlock();
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    buf[0] = reg;                   /* 0号元素为寄存器数值 */
    memcpy(buf + 1, data, len);     /* 拷贝数据至存储区中 */

    esp_err_t ret = i2c_master_transmit(xl9555_handle, buf, len + 1,
                                        XL9555_I2C_TIMEOUT_MS);

    free(buf);                      /* 发送完成释放内存 */

    if (ret == ESP_OK && reg == XL9555_OUTPUT_PORT0_REG && len >= 2)
    {
        xl9555_output_shadow = ((uint16_t)data[1] << 8) | data[0];
        xl9555_output_shadow_valid = true;
    }

    xl9555_unlock();
    return ret;
}

/**
 * @brief       控制某个IO的电平
 * @param       pin     : 控制的IO
 * @param       val     : 电平
 * @retval      返回所有IO状态
 */
uint16_t xl9555_pin_write(uint16_t pin, int val)
{
    if (!xl9555_lock())
    {
        return xl9555_output_shadow;
    }

    if (!xl9555_output_shadow_valid)
    {
        uint8_t current[2];
        if (xl9555_read_byte(current, sizeof(current)) != ESP_OK)
        {
            xl9555_unlock();
            return xl9555_output_shadow;
        }
    }

    uint16_t next = xl9555_output_shadow;
    if (val)
    {
        next |= pin;
    }
    else
    {
        next &= (uint16_t)~pin;
    }

    uint8_t output[2] = {(uint8_t)next, (uint8_t)(next >> 8)};
    if (xl9555_write_byte(XL9555_OUTPUT_PORT0_REG, output, sizeof(output)) != ESP_OK)
    {
        next = xl9555_output_shadow;
    }

    xl9555_unlock();
    return next;
}

/**
 * @brief       获取某个IO状态
 * @param       pin : 要获取状态的IO
 * @retval      此IO口的值(状态, 0/1)
 */
int xl9555_pin_read(uint16_t pin)
{
    uint8_t input[2];
    esp_err_t ret = xl9555_read_register(XL9555_INPUT_PORT0_REG, input, sizeof(input));
    if (ret != ESP_OK)
    {
        return -1;
    }

    uint16_t input_value = ((uint16_t)input[1] << 8) | input[0];
    return (input_value & pin) ? 1 : 0;
}

/**
 * @brief       XL9555的IO配置
 * @param       config_value：IO配置输入或者输出
 * @retval      返回设置的数值
 */
static esp_err_t xl9555_ioconfig(uint16_t config_value)
{
    /* 从机地址 + CMD + data1(P0) + data2(P1) */
    /* P10、P11、P12、P13和P14为输入，其他引脚为输出 -->0001 1111 0000 0000 注意：0为输出，1为输入*/
    uint8_t data[2];
    esp_err_t err;

    data[0] = (uint8_t)(0xFF & config_value);
    data[1] = (uint8_t)(0xFF & (config_value >> 8));

    for (int attempt = 0; attempt < XL9555_CONFIG_RETRY_COUNT; attempt++)
    {
        err = xl9555_write_byte(XL9555_CONFIG_PORT0_REG, data, 2);
        if (err == ESP_OK)
        {
            return ESP_OK;
        }

        ESP_LOGE(xl9555_tag, "%s configure %X failed, ret: %d", __func__, config_value, err);
        if (attempt + 1 < XL9555_CONFIG_RETRY_COUNT)
        {
            vTaskDelay(pdMS_TO_TICKS(XL9555_CONFIG_RETRY_DELAY_MS));
        }
    }

    return err;
}

/**
 * @brief       初始化XL9555
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t xl9555_init(void)
{
    uint8_t r_data[2];
    esp_err_t ret;

    if (xl9555_mutex == NULL)
    {
        xl9555_mutex = xSemaphoreCreateRecursiveMutex();
        if (xl9555_mutex == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    /* 未调用myiic_init初始化IIC */
    if (bus_handle == NULL)
    {
        ret = myiic_init();
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    i2c_device_config_t xl9555_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,  /* 从机地址长度 */
        .scl_speed_hz    = IIC_SPEED_CLK,       /* 传输速率 */
        .device_address  = XL9555_ADDR,         /* 从机7位的地址 */
    };
    if (xl9555_handle == NULL)
    {
        ret = i2c_master_bus_add_device(bus_handle, &xl9555_i2c_dev_conf, &xl9555_handle);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    /* 上电先读取一次清除中断标志 */
    ret = xl9555_read_register(XL9555_INPUT_PORT0_REG, r_data, 2);
    if (ret != ESP_OK)
    {
        return ret;
    }
    /* 配置那些扩展管脚为输入输出模式 */
    ret = xl9555_ioconfig(0xF003);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief       翻转某个io的电平
 * @param       pin : 要翻转的io
 * @retval      ESP_OK:成功; 其他:失败
 */
esp_err_t xl9555_pin_toggle(uint16_t pin)
{
    if (!xl9555_lock())
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!xl9555_output_shadow_valid)
    {
        uint8_t current[2];
        esp_err_t ret = xl9555_read_byte(current, sizeof(current));
        if (ret != ESP_OK)
        {
            xl9555_unlock();
            return ret;
        }
    }

    uint16_t next = xl9555_output_shadow ^ pin;
    uint8_t output[2] = {(uint8_t)next, (uint8_t)(next >> 8)};
    esp_err_t ret = xl9555_write_byte(XL9555_OUTPUT_PORT0_REG, output, sizeof(output));

    xl9555_unlock();
    return ret;
}
