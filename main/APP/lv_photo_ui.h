/**
 ******************************************************************************
 * @file        lv_photo_ui.h
 * @version     V1.0
 * @brief       LVGL 照片 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __LV_PHOTO_UI_H
#define __LV_PHOTO_UI_H

#include "lvgl.h"
#include "ff.h"
#include "exfuns.h"
#include "bmp.h"
#include "jpeg.h"
#include "png.h"
#include "lv_main_ui.h"
#include "lvgl_demo.h"

typedef enum
{
    PIC_NULL,
    PIC_PAUSE,
    PIC_PLAY,
    PIC_NEXT,
    PIC_PREV
}pic_stat_t;

typedef struct
{
    FF_DIR picdir;
    FILINFO *pic_picfileinfo;
    char *pic_pname;
    uint16_t pic_totpicnum;
    uint16_t pic_curindex;
    uint32_t *pic_picoffsettbl;
    pic_stat_t pic_state;
    lv_obj_t *photo_box;
    uint8_t pic_start;
    
    struct
    {
        lv_obj_t *photo_img;
        lv_obj_t *photo_number;
        lv_obj_t *photo_next;
        lv_obj_t *photo_pre;
    }photo_obj_t;
}lv_photo_ui_t;


/* 函数声明 */
void lv_photo_ui(void);
void lv_pic_del(void);
void lv_photo_diag_record_boot(void);
uint16_t pic_get_tnum(char *path);
#endif
