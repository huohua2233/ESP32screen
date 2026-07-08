/**
 ******************************************************************************
 * @file        lv_video_ui.c
 * @version     V1.0
 * @brief       LVGL 视频 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "lv_video_ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdint.h>
#include <stdio.h>


// const char* TAG1 = "main";
LV_IMG_DECLARE(Vectorback_next)
LV_IMG_DECLARE(Vectorback_pro)
lv_video_ui_t video_cfig;
extern lv_obj_t * back_btn;
AVI_INFO g_avix;                                        /* avi文件相关信息 */
char *const AVI_VIDS_FLAG_TBL[2] = {"00dc", "01dc"};    /* 视频编码标志字符串,00dc/01dc */
char *const AVI_AUDS_FLAG_TBL[2] = {"00wb", "01wb"};    /* 音频编码标志字符串,00wb/01wb */

uint16_t video_curindex;
FILINFO *vfileinfo;
uint8_t *video_pname;
uint16_t totavinum;
uint8_t *framebuf;
uint32_t *voffsettbl;
FF_DIR vdir;
FIL *video_favi;


/* VIDEO 任务 配置
 * 包括: 任务句柄 任务优先级 堆栈大小 创建任务
 */
#define VIDEO_PRIO      2                               /* 任务优先级 */
#define VIDEO_STK_SIZE  5 * 1024                        /* 任务堆栈大小 */
TaskHandle_t            VIDEOTask_Handler;              /* 任务句柄 */
extern TaskHandle_t PICTask_Handler;
void video(void *pvParameters);                         /* 任务函数 */

static const char *TAG = "lv_video_ui";
static uint8_t video_prev_dir = 0;
static uint8_t video_dir_changed = 0;
static uint8_t video_img_ready = 0;
static uint32_t video_img_w = 0;
static uint32_t video_img_h = 0;
static uint32_t video_buf_w = 0;
static uint32_t video_buf_h = 0;
static lv_obj_t *video_back_btn = NULL;
static uint8_t video_closing = 0;
static volatile video_stat_t video_pending_key = VIDEO_NULL;
#define VIDEO_DIRECT_LCD_DRAW 0
#define VIDEO_LANDSCAPE_FLIPPED 1
#define VIDEO_READ_RETRY_MAX 3
#define VIDEO_READ_RETRY_DELAY_MS 3

static void video_sd_lost(void)
{
    lv_smail_icon_clear_state(TF_STATE);
    video_cfig.video_start = 0;

    if (app_obj_general.del_parent != NULL && app_obj_general.APP_Function != NULL)
    {
        app_obj_general.app_state = DEL_STATE;
    }
}

static uint8_t video_file_read(FIL *fp, void *buf, UINT btr, UINT *br)
{
    uint8_t res = FR_OK;
    UINT local_br = 0;
    UINT *read_len = (br != NULL) ? br : &local_br;
    FSIZE_t start_pos = f_tell(fp);

    for (uint8_t retry = 0; retry < VIDEO_READ_RETRY_MAX; retry++)
    {
        *read_len = 0;
        SD_CS(0);
        if (retry > 0)
        {
            res = (uint8_t)f_lseek(fp, start_pos);
            if (res != FR_OK)
            {
                SD_CS(1);
                return res;
            }
        }
        res = (uint8_t)f_read(fp, buf, btr, read_len);
        SD_CS(1);

        if (res == FR_OK)
        {
            return FR_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(VIDEO_READ_RETRY_DELAY_MS));
    }

    return res;
}

static void video_apply_key(uint8_t key)
{
    if (totavinum == 0)
    {
        video_curindex = 0;
        return;
    }

    if (key == VIDEO_PREV)
    {
        if (video_curindex)
        {
            video_curindex--;
        }
        else
        {
            video_curindex = totavinum - 1;
        }
    }
    else if (key == VIDEO_NEXT)
    {
        video_curindex++;

        if (video_curindex >= totavinum)
        {
            video_curindex = 0;
        }
    }
}

static void video_set_display_dir(uint8_t dir, uint8_t flipped)
{
    lcd_display_dir(dir);
#if VIDEO_LANDSCAPE_FLIPPED
    if ((dir == 1) && flipped)
    {
        esp_lcd_panel_mirror(panel_handle, true, true);
    }
#else
    (void)flipped;
#endif
    disp_drv.hor_res = lcd_dev.width;
    disp_drv.ver_res = lcd_dev.height;
    lv_disp_drv_update(lv_disp_get_default(), &disp_drv);

    tp_dev.touchtype &= (uint8_t)~0x01;
    tp_dev.touchtype |= lcd_dev.dir & 0x01;
}

static void video_disable_scroll(lv_obj_t *obj)
{
    if (obj == NULL)
    {
        return;
    }

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void video_back_event_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_LONG_PRESSED && video_closing == 0)
    {
        video_closing = 1;
        lv_video_del();
    }
}

static void video_page_event_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_LONG_PRESSED && video_closing == 0 && video_pending_key == VIDEO_NULL)
    {
        video_stat_t key = (video_stat_t)(uintptr_t)lv_event_get_user_data(event);

        if (key == VIDEO_PREV || key == VIDEO_NEXT)
        {
            video_pending_key = key;
        }
    }
}

static lv_obj_t *video_create_back_hotspot(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, uint8_t visible)
{
    lv_obj_t *btn = lv_btn_create(video_cfig.video_box);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, visible ? 25 : 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_make(255,255,255), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, visible ? LV_OPA_50 : LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, LV_OPA_0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn, visible ? 2 : 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(btn, lv_color_make(34,177,76), LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(btn, visible ? LV_OPA_50 : LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_0, LV_STATE_DEFAULT);
    video_disable_scroll(btn);
    lv_obj_add_event_cb(btn, video_back_event_handler, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_move_foreground(btn);
    return btn;
}

static lv_obj_t *video_create_page_hotspot(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, video_stat_t key)
{
    lv_obj_t *btn = lv_btn_create(video_cfig.video_box);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, LV_OPA_0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_0, LV_STATE_DEFAULT);
    video_disable_scroll(btn);
    lv_obj_add_event_cb(btn, video_page_event_handler, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)key);

    return btn;
}

#if VIDEO_DIRECT_LCD_DRAW
static uint8_t video_direct_draw(uint32_t w, uint32_t h, uint8_t *video_buf)
{
    int x = 0;
    int y = 0;

    if ((video_buf == NULL) || (w == 0) || (h == 0) || (w > lcd_dev.width) || (h > lcd_dev.height))
    {
        return 0;
    }

    if (lcd_dev.width > w)
    {
        x = (lcd_dev.width - w) / 2;
    }

    if (lcd_dev.height > h)
    {
        y = (lcd_dev.height - h) / 2;
    }

    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + w, y + h, video_buf);

    return 1;
}
#else
static uint8_t video_direct_draw(uint32_t w, uint32_t h, uint8_t *video_buf)
{
    (void)w;
    (void)h;
    (void)video_buf;
    return 0;
}
#endif

/**
 * @brief       avi解码初始化
 * @param       buf  : 输入缓冲区
 * @param       size : 缓冲区大小
 * @retval      res
 *    @arg      OK,avi文件解析成功
 *    @arg      其他,错误代码
 */
AVISTATUS avi_init(uint8_t *buf, uint32_t size)
{
    uint16_t offset;
    uint8_t *tbuf;
    AVISTATUS res = AVI_OK;
    AVI_HEADER *aviheader;
    LIST_HEADER *listheader;
    AVIH_HEADER *avihheader;
    STRH_HEADER *strhheader;

    STRF_BMPHEADER *bmpheader;
    STRF_WAVHEADER *wavheader;

    tbuf = buf;
    aviheader = (AVI_HEADER *)buf;
    if (aviheader->RiffID != AVI_RIFF_ID)
    {
        return AVI_RIFF_ERR;        /* RIFF ID错误 */
    }

    if (aviheader->AviID != AVI_AVI_ID)
    {
        return AVI_AVI_ERR;         /* AVI ID错误 */
    }

    buf += sizeof(AVI_HEADER);      /* 偏移 */
    listheader = (LIST_HEADER *)(buf);
    if (listheader->ListID != AVI_LIST_ID)
    {
        return AVI_LIST_ERR;        /* LIST ID错误 */
    }

    if (listheader->ListType != AVI_HDRL_ID)
    {
        return AVI_HDRL_ERR;        /* HDRL ID错误 */
    }

    buf += sizeof(LIST_HEADER);     /* 偏移 */
    avihheader = (AVIH_HEADER *)(buf);
    if (avihheader->BlockID != AVI_AVIH_ID)
    {
        return AVI_AVIH_ERR;        /* AVIH ID错误 */
    }

    g_avix.SecPerFrame = avihheader->SecPerFrame;   /* 得到帧间隔时间 */
    g_avix.TotalFrame = avihheader->TotalFrame;     /* 得到总帧数 */
    buf += avihheader->BlockSize + 8;               /* 偏移 */
    listheader = (LIST_HEADER *)(buf);
    if (listheader->ListID != AVI_LIST_ID)
    {
        return AVI_LIST_ERR;        /* LIST ID错误 */
    }

    if (listheader->ListType != AVI_STRL_ID)
    {
        return AVI_STRL_ERR;        /* STRL ID错误 */
    }

    strhheader = (STRH_HEADER *)(buf + 12);
    if (strhheader->BlockID != AVI_STRH_ID)
    {
        return AVI_STRH_ERR;        /* STRH ID错误 */
    }

    if (strhheader->StreamType == AVI_VIDS_STREAM)  /* 视频帧在前 */
    {
        if (strhheader->Handler != AVI_FORMAT_MJPG) /* 非MJPG视频流,不支持 */
        {
            return AVI_FORMAT_ERR;
        }

        g_avix.VideoFLAG = AVI_VIDS_FLAG_TBL[0];    /* 视频流标记  "00dc" */
        g_avix.AudioFLAG = AVI_AUDS_FLAG_TBL[1];    /* 音频流标记  "01wb" */
        bmpheader = (STRF_BMPHEADER *)(buf + 12 + strhheader->BlockSize + 8);   /* strf */
        if (bmpheader->BlockID != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;    /* STRF ID错误 */
        }

        g_avix.Width = bmpheader->bmiHeader.Width;
        g_avix.Height = bmpheader->bmiHeader.Height;
        buf += listheader->BlockSize + 8;       /* 偏移 */
        listheader = (LIST_HEADER *)(buf);
        if (listheader->ListID != AVI_LIST_ID)  /* 是不含有音频帧的视频文件 */
        {
            g_avix.SampleRate = 0;              /* 音频采样率 */
            g_avix.Channels = 0;                /* 音频通道数 */
            g_avix.AudioType = 0;               /* 音频格式 */
        }
        else
        {
            if (listheader->ListType != AVI_STRL_ID)
            {
                return AVI_STRL_ERR;    /* STRL ID错误 */
            }

            strhheader = (STRH_HEADER *)(buf + 12);
            if (strhheader->BlockID != AVI_STRH_ID)
            {
                return AVI_STRH_ERR;    /* STRH ID错误 */
            }

            if (strhheader->StreamType != AVI_AUDS_STREAM)
            {
                return AVI_FORMAT_ERR;  /* 格式错误 */
            }

            wavheader = (STRF_WAVHEADER *)(buf + 12 + strhheader->BlockSize + 8);   /* strf */
            if (wavheader->BlockID != AVI_STRF_ID)
            {
                return AVI_STRF_ERR;    /* STRF ID错误 */
            }

            g_avix.SampleRate = wavheader->SampleRate;      /* 音频采样率 */
            g_avix.Channels = wavheader->Channels;          /* 音频通道数 */
            g_avix.AudioType = wavheader->FormatTag;        /* 音频格式 */
        }
    }
    else if (strhheader->StreamType == AVI_AUDS_STREAM)     /* 音频帧在前 */
    { 
        g_avix.VideoFLAG = AVI_VIDS_FLAG_TBL[1];            /* 视频流标记  "01dc" */
        g_avix.AudioFLAG = AVI_AUDS_FLAG_TBL[0];            /* 音频流标记  "00wb" */
        wavheader = (STRF_WAVHEADER *)(buf + 12 + strhheader->BlockSize + 8);   /* strf */
        if (wavheader->BlockID != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;                            /* STRF ID错误 */
        }
 
        g_avix.SampleRate = wavheader->SampleRate;          /* 音频采样率 */
        g_avix.Channels = wavheader->Channels;              /* 音频通道数 */
        g_avix.AudioType = wavheader->FormatTag;            /* 音频格式 */
        buf += listheader->BlockSize + 8;                   /* 偏移 */
        listheader = (LIST_HEADER *)(buf);
        if (listheader->ListID != AVI_LIST_ID)
        {
            return AVI_LIST_ERR;    /* LIST ID错误 */
        }

        if (listheader->ListType != AVI_STRL_ID)
        {
            return AVI_STRL_ERR;    /* STRL ID错误 */
        }

        strhheader = (STRH_HEADER *)(buf + 12);
        if (strhheader->BlockID != AVI_STRH_ID)
        {
            return AVI_STRH_ERR;    /* STRH ID错误 */
        }

        if (strhheader->StreamType != AVI_VIDS_STREAM)
        {
            return AVI_FORMAT_ERR;  /* 格式错误 */
        }

        bmpheader = (STRF_BMPHEADER *)(buf + 12 + strhheader->BlockSize + 8);   /* strf */
        if (bmpheader->BlockID != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;    /* STRF ID错误 */
        }

        if (bmpheader->bmiHeader.Compression != AVI_FORMAT_MJPG)
        {
            return AVI_FORMAT_ERR;  /* 格式错误 */
        }

        g_avix.Width = bmpheader->bmiHeader.Width;
        g_avix.Height = bmpheader->bmiHeader.Height;
    }

    offset = avi_srarch_id(tbuf, size, "movi");     /* 查找movi ID */
    if (offset == 0)
    {
        return AVI_MOVI_ERR;        /* MOVI ID错误 */
    }

    if (g_avix.SampleRate)          /* 有音频流,才查找 */
    {
        tbuf += offset;
        offset = avi_srarch_id(tbuf, size, g_avix.AudioFLAG);   /* 查找音频流标记 */
        if (offset == 0)
        {
            return AVI_STREAM_ERR;
        }
        tbuf += offset + 4;
        g_avix.AudioBufSize = *((uint16_t *)tbuf);              /* 得到音频流buf大小. */
    }

    ESP_LOGI(TAG, "avi init ok\r\n");
    ESP_LOGI(TAG, "g_avix.SecPerFrame:%ld\r\n", g_avix.SecPerFrame);
    ESP_LOGI(TAG, "g_avix.TotalFrame:%ld\r\n", g_avix.TotalFrame);
    ESP_LOGI(TAG, "g_avix.Width:%ld\r\n", g_avix.Width);
    ESP_LOGI(TAG, "g_avix.Height:%ld\r\n", g_avix.Height);
    ESP_LOGI(TAG, "g_avix.AudioType:%d\r\n", g_avix.AudioType);
    ESP_LOGI(TAG, "g_avix.SampleRate:%ld\r\n", g_avix.SampleRate);
    ESP_LOGI(TAG, "g_avix.Channels:%d\r\n", g_avix.Channels);
    ESP_LOGI(TAG, "g_avix.AudioBufSize:%d\r\n", g_avix.AudioBufSize);
    ESP_LOGI(TAG, "g_avix.VideoFLAG:%s\r\n", g_avix.VideoFLAG);
    ESP_LOGI(TAG, "g_avix.AudioFLAG:%s\r\n", g_avix.AudioFLAG);

    return res;
}

/**
 * @brief       查找 ID
 * @param       buf  : 输入缓冲区
 * @param       size : 缓冲区大小
 * @param       id   : 要查找的id, 必须是4字节长度
 * @retval      执行结果
 *   @arg       0     , 没找到
 *   @arg       其他  , movi ID偏移量
 */
uint32_t avi_srarch_id(uint8_t *buf, uint32_t size, char *id)
{
    uint32_t i;
    uint32_t idsize = 0;
    size -= 4;
    for (i = 0; i < size; i++)
    {
        if ((buf[i] == id[0]) &&
            (buf[i + 1] == id[1]) &&
            (buf[i + 2] == id[2]) &&
            (buf[i + 3] == id[3]))
        {
            idsize = MAKEDWORD(buf + i + 4);    /* 得到帧大小,必须大于16字节,才返回,否则不是有效数据 */

            if (idsize > 0X10)return i;         /* 找到"id"所在的位置 */
        }
    }

    return 0;
}

/**
 * @brief       得到stream流信息
 * @param       buf  : 流开始地址(必须是01wb/00wb/01dc/00dc开头)
 * @retval      执行结果
 *   @arg       AVI_OK, AVI文件解析成功
 *   @arg       其他  , 错误代码
 */
AVISTATUS avi_get_streaminfo(uint8_t *buf)
{
    g_avix.StreamID = MAKEWORD(buf + 2);    /* 得到流类型 */
    g_avix.StreamSize = MAKEDWORD(buf + 4); /* 得到流大小 */

    if (g_avix.StreamSize > (AVI_MAX_FRAME_SIZE - 8)) /* 帧大小太大了,直接返回错误 */
    {
        printf("FRAME SIZE OVER:%ld\r\n", g_avix.StreamSize);
        g_avix.StreamSize = 0;
        return AVI_STREAM_ERR;
    }
    
    if (g_avix.StreamSize % 2)
    {
        g_avix.StreamSize++;    /* 奇数加1(g_avix.StreamSize,必须是偶数) */
    }

    if (g_avix.StreamID == AVI_VIDS_FLAG || g_avix.StreamID == AVI_AUDS_FLAG)
    {
        return AVI_OK;
    }

    return AVI_STREAM_ERR;
}

/**
 * @brief       显示当前播放时间
 * @param       favi   : 当前播放的视频文件
 * @param       aviinfo: avi控制结构体
 * @retval      无
 */
void video_time_show(FIL *favi, AVI_INFO *aviinfo)
{
    static uint32_t oldsec;                                         /* 上一次的播放时间 */
    
    uint32_t totsec = 0;                                            /* video文件总时间 */
    uint32_t cursec;                                                /* 当前播放时间 */
    
    totsec = (aviinfo->SecPerFrame / 1000) * aviinfo->TotalFrame;   /* 歌曲总长度(单位:ms) */
    totsec /= 1000;                                                 /* 秒钟数. */
    cursec = ((double)favi->fptr / favi->obj.objsize) * totsec;     /* 获取当前播放到多少秒 */
    
    if (oldsec != cursec)                                           /* 需要更新显示时间 */
    {
        oldsec = cursec;
    }

}

/**
 * @brief       转换
 * @param       fs:文件系统对象
 * @param       clst:转换
 * @retval      =0:扇区号，0:失败
 */
static LBA_t atk_clst2sect(FATFS *fs, DWORD clst)
{
    clst -= 2;  /* Cluster number is origin from 2 */

    if (clst >= fs->n_fatent - 2)
    {
        return 0;   /* Is it invalid cluster number? */
    }

    return fs->database + (LBA_t)fs->csize * clst;  /* Start sector number of the cluster */
}

/**
 * @brief       偏移
 * @param       dp:指向目录对象
 * @param       Offset:目录表的偏移量
 * @retval      FR_OK(0):成功，!=0:错误
 */
FRESULT atk_video_dir_sdi(FF_DIR *dp, DWORD ofs)
{
    DWORD clst;
    FATFS *fs = dp->obj.fs;

    if (ofs >= (DWORD)((FF_FS_EXFAT && fs->fs_type == FS_EXFAT) ? 0x10000000 : 0x200000) || ofs % 32)
    {
        /* Check range of offset and alignment */
        return FR_INT_ERR;
    }

    dp->dptr = ofs;         /* Set current offset */
    clst = dp->obj.sclust;  /* Table start cluster (0:root) */

    if (clst == 0 && fs->fs_type >= FS_FAT32)
    {	/* Replace cluster# 0 with root cluster# */
        clst = (DWORD)fs->dirbase;

        if (FF_FS_EXFAT)
        {
            dp->obj.stat = 0;
        }
        /* exFAT: Root dir has an FAT chain */
    }

    if (clst == 0)
    {	/* Static table (root-directory on the FAT volume) */
        if (ofs / 32 >= fs->n_rootdir)
        {
            return FR_INT_ERR;  /* Is index out of range? */
        }

        dp->sect = fs->dirbase;

    }
    else
    {   /* Dynamic table (sub-directory or root-directory on the FAT32/exFAT volume) */
        dp->sect = atk_clst2sect(fs, clst);
    }

    dp->clust = clst;   /* Current cluster# */

    if (dp->sect == 0)
    {
        return FR_INT_ERR;
    }

    dp->sect += ofs / fs->ssize;             /* Sector# of the directory entry */
    dp->dir = fs->win + (ofs % fs->ssize);   /* Pointer to the entry in the win[] */

    return FR_OK;
}

static uint8_t video_dir_open_retry(FF_DIR *dir, const TCHAR *path)
{
    uint8_t res = FR_OK;

    for (uint8_t retry = 0; retry < VIDEO_READ_RETRY_MAX; retry++)
    {
        SD_CS(0);
        res = (uint8_t)f_opendir(dir, path);
        SD_CS(1);

        if (res == FR_OK)
        {
            return FR_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(VIDEO_READ_RETRY_DELAY_MS));
    }

    return res;
}

static uint8_t video_dir_seek_read_retry(FF_DIR *dir, DWORD ofs, FILINFO *info)
{
    uint8_t res = FR_OK;

    for (uint8_t retry = 0; retry < VIDEO_READ_RETRY_MAX; retry++)
    {
        SD_CS(0);
        res = (uint8_t)atk_video_dir_sdi(dir, ofs);
        if (res == FR_OK)
        {
            res = (uint8_t)f_readdir(dir, info);
        }
        SD_CS(1);

        if (res == FR_OK)
        {
            return FR_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(VIDEO_READ_RETRY_DELAY_MS));
    }

    return res;
}

static uint8_t video_file_open_retry(FIL *fp, const TCHAR *path)
{
    uint8_t res = FR_OK;

    for (uint8_t retry = 0; retry < VIDEO_READ_RETRY_MAX; retry++)
    {
        SD_CS(0);
        res = (uint8_t)f_open(fp, path, FA_READ);
        SD_CS(1);

        if (res == FR_OK)
        {
            return FR_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(VIDEO_READ_RETRY_DELAY_MS));
    }

    return res;
}

static uint8_t video_file_lseek_retry(FIL *fp, FSIZE_t ofs)
{
    uint8_t res = FR_OK;

    for (uint8_t retry = 0; retry < VIDEO_READ_RETRY_MAX; retry++)
    {
        SD_CS(0);
        res = (uint8_t)f_lseek(fp, ofs);
        SD_CS(1);

        if (res == FR_OK)
        {
            return FR_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(VIDEO_READ_RETRY_DELAY_MS));
    }

    return res;
}

lv_img_dsc_t video_img_dsc = {
    .header.always_zero = 0,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};

/**
 * @brief       video显示视频
 * @param       无
 * @retval      无
 */
void video_show(uint32_t w,uint32_t h,uint8_t * video_buf)
{
    if (lvgl_mux_lock(10))
    {
        if (video_direct_draw(w, h, video_buf) == 0)
        {
            video_img_dsc.header.w = w;
            video_img_dsc.header.h = h;
            video_img_dsc.data_size = w * h * 2;
            video_img_dsc.data = (const uint8_t *)video_buf;

            if ((video_img_ready == 0) || (video_img_w != w) || (video_img_h != h))
            {
                lv_img_set_src(video_cfig.video_obj_t.video_img,&video_img_dsc);
                lv_obj_center(video_cfig.video_obj_t.video_img);
                video_img_w = w;
                video_img_h = h;
                video_img_ready = 1;
            }
            else
            {
                lv_obj_invalidate(video_cfig.video_obj_t.video_img);
            }
        }
        lvgl_mux_unlock();
    }
    //video_time_show(video_favi, &g_avix);
}

/**
 * @brief       video task
 * @param       pvParameters : 传入参数(未用到)
 * @retval      无
 */
void video(void *pvParameters)
{
    pvParameters = pvParameters;
    uint8_t res = 0;
    uint8_t key = 0;
    DWORD temp = 0;
    uint8_t *pbuf = NULL;
    uint32_t nr = 0;
    uint16_t offset = 0;
    uint8_t vdir_open = 0;
    uint8_t video_file_open = 0;
    uint8_t mjpeg_open = 0;
    uint8_t read_fail_count = 0;

    res = video_dir_open_retry(&vdir, "0:/VIDEO");          /* 打开目录 */

    if (res != FR_OK)
    {
        video_sd_lost();
    }

    if (res == FR_OK)
    {
        vdir_open = 1;
    }

    if (res == FR_OK)
    {
        video_curindex = 0;

        while (video_cfig.video_start)
        {
            temp = vdir.dptr;                               /* 记录当前dptr偏移 */
            res = video_dir_seek_read_retry(&vdir, temp, vfileinfo);     /* 读取下一个文件 */
            if ((res != 0) || (vfileinfo->fname[0] == 0))   /* 错误或到末尾，退出 */
            {
                if (res != FR_OK)
                {
                    video_sd_lost();
                }
                break;
            }
            
            res = exfuns_file_type(vfileinfo->fname);
            
            if ((res & 0xF0) == 0x60)                       /* 是视频文件 */
            {
                if (video_curindex < totavinum)
                {
                    voffsettbl[video_curindex] = temp;          /* 记录索引 */
                    video_curindex++;
                }
            }
        }

        if (vdir_open)
        {
            SD_CS(0);
            f_closedir(&vdir);
            vdir_open = 0;
            SD_CS(1);
        }

        totavinum = video_curindex;
        if (totavinum == 0)
        {
            video_sd_lost();
        }

        video_curindex = 0;
        if (video_cfig.video_start)
        {
            res = video_dir_open_retry(&vdir, "0:/VIDEO");  /* 打开目录 */
            if (res == FR_OK)
            {
                vdir_open = 1;
            }
            else
            {
                video_sd_lost();
            }
        }

        while (video_cfig.video_start && res == FR_OK)
        {
            key = 0;
            res = video_dir_seek_read_retry(&vdir, voffsettbl[video_curindex], vfileinfo);   /* 改变当前目录索引 */

            if ((res != 0) || (vfileinfo->fname[0] == 0))           /* 错误或到末尾，退出 */
            {
                if (res != FR_OK)
                {
                    video_sd_lost();
                }
                printf("err\r\n");
                break;
            }
            
            strcpy((char *)video_pname, "0:/VIDEO/");                       /* 复制路径（目录） */
            strcat((char *)video_pname, (const char *)vfileinfo->fname);    /* 将文件名接在后面 */
            printf("%s\n",video_pname);
            memset(framebuf, 0, AVI_MAX_FRAME_SIZE);
            res = video_file_open_retry(video_favi, (const TCHAR *)video_pname);

            if (res != FR_OK)
            {
                video_sd_lost();
                break;
            }

            if (res == FR_OK)
            {
                video_file_open = 1;
                pbuf = framebuf;
                res = video_file_read(video_favi, pbuf, AVI_MAX_FRAME_SIZE, (UINT*)&nr);    /* 开始读取 */
                if (res != 0)
                {
                    printf("fread error:%d\r\n", res);
                    video_sd_lost();
                    goto video_file_cleanup;
                }

                res = avi_init(pbuf, AVI_MAX_FRAME_SIZE);                           /* AVI解析 */

                if (res != 0)
                {
                    printf("avi error:%d\r\n", res);
                    key = VIDEO_NEXT;
                    goto video_file_cleanup;
                }

                offset = avi_srarch_id(pbuf, AVI_MAX_FRAME_SIZE, "movi");   /* 寻找movi ID */
                if (avi_get_streaminfo(pbuf + offset + 4) != 0)             /* 获取流信息 */
                {
                    key = VIDEO_NEXT;
                    goto video_file_cleanup;
                }
                res = video_file_lseek_retry(video_favi, offset + 12);      /* 跳过标志ID，读地址偏移到流数据开始处 */
                if (res != FR_OK)
                {
                    video_sd_lost();
                    goto video_file_cleanup;
                }
                res = mjpegdec_init(0,0);                                   /* 初始化JPG解码 */

                if (res != 0)
                {
                    printf("mjpegdec Fail\r\n");
                    key = VIDEO_NEXT;
                    goto video_file_cleanup;
                }
                mjpeg_open = 1;

                {
                    /* 定义图像的宽高 */
                    Windows_Width = g_avix.Width;
                    Windows_Height = g_avix.Height;

                    if ((video_buf == NULL) || (video_buf_w != g_avix.Width) || (video_buf_h != g_avix.Height))
                    {
                        if (video_buf != NULL)
                        {
                            mjpegdec_video_free();
                            video_buf_w = 0;
                            video_buf_h = 0;
                        }

                        mjpegdec_malloc();
                        if (video_buf != NULL)
                        {
                            video_buf_w = g_avix.Width;
                            video_buf_h = g_avix.Height;
                        }
                    }
                }

                if (g_avix.SampleRate)                                                  /* 定义图像的宽高 */
                {

                }

                if (video_buf == NULL)
                {
                    printf("video buf malloc failed\r\n");
                    key = VIDEO_NEXT;
                    goto video_file_cleanup;
                }

                while (video_cfig.video_start)
                {
                    if (video_pending_key == VIDEO_NEXT || video_pending_key == VIDEO_PREV)
                    {
                        key = (uint8_t)video_pending_key;
                        video_pending_key = VIDEO_NULL;
                        break;
                    }

                    if (!lv_smail_icon_get_state(TF_STATE))
                    {
                        video_cfig.video_start = 0;
                        break;
                    }

                    if (g_avix.StreamID == AVI_VIDS_FLAG)                               /* 视频流 */
                    {
                        UINT want_read = (UINT)(g_avix.StreamSize + 8);
                        pbuf = framebuf;
                        res = video_file_read(video_favi, pbuf, want_read, (UINT*)&nr);    /* 读取整帧+下一帧数据流ID信息 */

                        if (res != FR_OK)
                        {
                            read_fail_count++;
                            if (read_fail_count >= VIDEO_READ_RETRY_MAX)
                            {
                                video_sd_lost();
                            }
                            else
                            {
                                res = FR_OK;
                                key = VIDEO_NULL;
                            }
                            break;
                        }

                        read_fail_count = 0;

                        if (nr >= g_avix.StreamSize)
                        {
                            res = mjpegdec_decode(pbuf, g_avix.StreamSize,video_show);
                        
                            if (res != 0)
                            {
                                printf("decode error!\r\n");
                            }
                        }

                        if (nr < want_read)
                        {
                            res = FR_OK;
                            key = VIDEO_NULL;
                            break;
                        }

                    }
                    else
                    {
                        UINT want_read = (UINT)(g_avix.StreamSize + 8);
                        res = video_file_read(video_favi, framebuf, want_read, (UINT*)&nr);       /* 填充psaibuf */

                        if (res != FR_OK)
                        {
                            read_fail_count++;
                            if (read_fail_count >= VIDEO_READ_RETRY_MAX)
                            {
                                video_sd_lost();
                            }
                            else
                            {
                                res = FR_OK;
                                key = VIDEO_NULL;
                            }
                            break;
                        }

                        read_fail_count = 0;

                        if (nr < want_read)
                        {
                            res = FR_OK;
                            key = VIDEO_NULL;
                            break;
                        }

                        pbuf = framebuf;
                    }
                    
                    if (avi_get_streaminfo(pbuf + g_avix.StreamSize) != 0)
                    {
                        key = VIDEO_NULL;
                        break;
                    }

                }

                if (key == VIDEO_PREV)              /* 上一曲 */
                {
                    if (video_curindex)
                    {
                        video_curindex--;
                    }
                    else
                    {
                        video_curindex = totavinum - 1;
                    }
                }
                else if (key == VIDEO_NEXT)         /* 下一曲 */
                {
                    video_curindex++;

                    if (video_curindex >= totavinum)
                    {
                        video_curindex = 0;         /* 到末尾的时候,自动从头开始 */
                    }

                }
                key = 0;
                res = FR_OK;
video_file_cleanup:
                esptim_int_deinit();
                if (mjpeg_open)
                {
                    mjpegdec_free();
                    mjpeg_open = 0;
                }
				/* 选中SD卡 */
	            SD_CS(0);
                f_close(video_favi);
                video_file_open = 0;
                if (key != 0 && video_cfig.video_start)
                {
                    video_apply_key(key);
                    key = 0;
                    res = FR_OK;
                }
				/* 取消选中SD卡 */
	            SD_CS(1);
            }
        }
    }

    if (res != FR_OK && video_cfig.video_start)
    {
        video_sd_lost();
    }

    esptim_int_deinit();

    if (mjpeg_open)
    {
        mjpegdec_free();
        mjpeg_open = 0;
    }

    if (video_file_open)
    {
        SD_CS(0);
        f_close(video_favi);
        video_file_open = 0;
        SD_CS(1);
    }

    if (vdir_open)
    {
        SD_CS(0);
        f_closedir(&vdir);
        vdir_open = 0;
        SD_CS(1);
    }

    VIDEOTask_Handler = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief       获取指定路径下有效视频文件的数量
 * @param       path: 指定路径
 * @retval      有效视频文件的数量
 */
static uint16_t video_get_tnum(char *path)
{
    uint8_t res;
    uint8_t dir_open = 0;
    uint16_t rval = 0;
    FF_DIR tdir;
    FILINFO *tfileinfo;
    DWORD temp = 0;
    
    tfileinfo = (FILINFO *)malloc(sizeof(FILINFO));             /* 申请内存 */
    res = video_dir_open_retry(&tdir, (const TCHAR *)path);     /* 打开目录 */
    if (res == 0)
    {
        dir_open = 1;
    }

    if ((res == 0) && tfileinfo)
    {
        while (1)                                               /* 查询总的有效文件数 */
        {
            temp = tdir.dptr;
            res = video_dir_seek_read_retry(&tdir, temp, tfileinfo);         /* 读取目录下的一个文件 */
            if ((res != 0) || (tfileinfo->fname[0] == 0))       /* 错误或到末尾，退出 */
            {
                break;
            }
            
            res = exfuns_file_type(tfileinfo->fname);
            
            if ((res & 0xF0) == 0x60)                           /* 是视频文件 */
            {
                rval++;
            }
        }
    }

    free(tfileinfo);                                            /* 释放内存 */
    
    if (dir_open)
    {
        SD_CS(0);
        f_closedir(&tdir);
        SD_CS(1);
    }

    return rval;
}

/**
 * @brief  视频演示
 * @param  无
 * @return 无
 */
void lv_video_ui(void)
{
    if (VIDEOTask_Handler != NULL || PICTask_Handler != NULL)
    {
        lv_msgbox("Media closing");
        return ;
    }

    video_curindex = 0;     /* 当前索引 */
    vfileinfo = 0;          /* 文件信息 */
    video_pname = 0;        /* 带路径的文件名 */
    totavinum = 0;          /* 音乐文件总数 */
    framebuf = NULL;
    voffsettbl = 0;
    video_favi = NULL;

    // xl9555_pin_write(SPK_CTRL_IO, 1);   /* 打开喇叭 */

    if (lv_smail_icon_get_state(TF_STATE))
    {		
        if (video_dir_open_retry(&vdir, "0:/VIDEO"))
        {
            lv_msgbox("MUSIC folder error");
            return ;
        }
        
        SD_CS(0);
        f_closedir(&vdir);
        SD_CS(1);

        totavinum = video_get_tnum("0:/VIDEO");

        if (totavinum == 0)
        {
            lv_msgbox("No music files");
            return ;
        }

        vfileinfo = (FILINFO*)malloc(sizeof(FILINFO));
        video_pname = (uint8_t *)malloc(255 * 2 + 1);
        voffsettbl = (uint32_t *)malloc(4 * totavinum);

        if ((vfileinfo == NULL) || (video_pname == NULL) || (voffsettbl == NULL))
        {
            free(vfileinfo);
            free(video_pname);
            free(voffsettbl);
            vfileinfo = NULL;
            video_pname = NULL;
            voffsettbl = NULL;
            lv_msgbox("memory allocation failed");
            return ;
        }

        framebuf = (uint8_t *)heap_caps_malloc(AVI_MAX_FRAME_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (framebuf == NULL)
        {
            framebuf = (uint8_t *)heap_caps_malloc(AVI_MAX_FRAME_SIZE, MALLOC_CAP_8BIT);
        }
        if (framebuf == NULL)
        {
            framebuf = (uint8_t *)malloc(AVI_MAX_FRAME_SIZE);
        }
        video_favi = (FIL *)malloc(sizeof(FIL));

        if ((framebuf == NULL) || (video_favi == NULL))
        {
            free(framebuf);
            free(video_favi);
            free(vfileinfo);
            free(video_pname);
            free(voffsettbl);
            framebuf = NULL;
            video_favi = NULL;
            vfileinfo = NULL;
            video_pname = NULL;
            voffsettbl = NULL;
            lv_msgbox("memory error!");
            return ;
        }

        memset(framebuf, 0, AVI_MAX_FRAME_SIZE);
        memset(video_pname, 0, 255 * 2 + 1);
        memset(voffsettbl, 0, 4 * totavinum);

        /* 隐藏box */
        lv_hidden_box();

        video_prev_dir = lcd_dev.dir;
        video_set_display_dir(1, 1);
        video_dir_changed = 1;
        video_disable_scroll(lv_scr_act());
        lv_obj_scroll_to(lv_scr_act(), 0, 0, LV_ANIM_OFF);

        video_cfig.video_start = 0x01;
        video_closing = 0;
        video_pending_key = VIDEO_NULL;
        video_cfig.video_box = lv_obj_create(lv_scr_act());
        lv_obj_set_size(video_cfig.video_box, lcd_dev.width, lcd_dev.height);
        lv_obj_set_style_bg_color(video_cfig.video_box, lv_color_make(0,0,0), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(video_cfig.video_box,LV_OPA_100,LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(video_cfig.video_box,LV_OPA_0,LV_STATE_DEFAULT);
        lv_obj_set_style_radius(video_cfig.video_box, 0, LV_STATE_DEFAULT);
        lv_obj_set_pos(video_cfig.video_box,0,0);
        video_disable_scroll(video_cfig.video_box);

        app_obj_general.del_parent = video_cfig.video_box;
        app_obj_general.APP_Function = lv_video_del;
        app_obj_general.app_state = NOT_DEL_STATE;

        video_cfig.video_obj_t.video_img = lv_img_create(video_cfig.video_box);
        lv_obj_set_style_bg_color(video_cfig.video_obj_t.video_img, lv_color_make(50,52,67), LV_STATE_DEFAULT);
        lv_obj_center(video_cfig.video_obj_t.video_img);
        lv_obj_clear_flag(video_cfig.video_obj_t.video_img, LV_OBJ_FLAG_CLICKABLE);
        video_disable_scroll(video_cfig.video_obj_t.video_img);

        video_img_ready = 0;
        video_img_w = 0;
        video_img_h = 0;

        if (back_btn != NULL)
        {
            lv_obj_add_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
        }

        video_create_page_hotspot(0, 0, lcd_dev.width, lcd_dev.height / 2, VIDEO_PREV);
        video_create_page_hotspot(0, lcd_dev.height / 2, lcd_dev.width, lcd_dev.height - (lcd_dev.height / 2), VIDEO_NEXT);
        video_back_btn = video_create_back_hotspot(0, 0, 64, 52, 0);
        video_create_back_hotspot(lcd_dev.width - 64, 0, 64, 52, 0);
        video_create_back_hotspot(0, lcd_dev.height - 52, 64, 52, 0);
        video_create_back_hotspot(lcd_dev.width - 64, lcd_dev.height - 52, 64, 52, 0);

        if (VIDEOTask_Handler == NULL)
        {
            xTaskCreatePinnedToCore((TaskFunction_t )video,
                                    (const char*    )"video",
                                    (uint16_t       )VIDEO_STK_SIZE,
                                    (void*          )NULL,
                                    (UBaseType_t    )VIDEO_PRIO,
                                    (TaskHandle_t*  )&VIDEOTask_Handler,
                                    (BaseType_t     ) 1);
        }
    }
    else
    {
        lv_msgbox("SD device not detected");
    }

}

void lv_video_del(void)
{
    video_cfig.video_start = 0;

    if (VIDEOTask_Handler != NULL)
    {
        while (VIDEOTask_Handler != NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    SD_CS(1);

    esptim_int_deinit();
    mjpegdec_free();

    if (video_buf != NULL)
    {
        mjpegdec_video_free();
        video_buf_w = 0;
        video_buf_h = 0;
    }

    if ((framebuf != NULL))
    {
        free(framebuf);
        framebuf = NULL;
    }

    if ((vfileinfo != NULL) || (video_pname != NULL) || (voffsettbl != NULL))
    {
        free(vfileinfo);
        free(video_pname);
        free(voffsettbl);
        vfileinfo = NULL;
        video_pname = NULL;
        voffsettbl = NULL;
    }

    if (video_favi != NULL)
    {
        free(video_favi);
        video_favi = NULL;
    }

    if (app_obj_general.del_parent != NULL && lv_obj_is_valid(app_obj_general.del_parent))
    {
        lv_obj_del(app_obj_general.del_parent);
    }
    app_obj_general.del_parent = NULL;
    app_obj_general.APP_Function = NULL;
    app_obj_general.app_state = NOT_DEL_STATE;
    video_cfig.video_box = NULL;
    video_back_btn = NULL;
    video_img_ready = 0;
    video_img_w = 0;
    video_img_h = 0;
    video_closing = 0;

    if (video_dir_changed)
    {
        video_set_display_dir(video_prev_dir, 0);
        video_dir_changed = 0;
    }

    if (back_btn != NULL)
    {
        lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(back_btn, 0, 0);
        lv_obj_move_foreground(back_btn);
    }

    lv_display_box();
}
