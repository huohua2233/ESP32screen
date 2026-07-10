/**
 ******************************************************************************
 * @file        app_file.c
 * @version     V1.0
 * @brief       LVGL 文件 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */
 
#include "app_file.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "lcd.h"


LV_FONT_DECLARE(myFont24)
LV_FONT_DECLARE(myFont18)
LV_FONT_DECLARE(myFont14)

lv_file_struct lv_flie;
extern lv_obj_t * back_btn;
volatile uint8_t app_file_sd_busy = 0;
static char app_file_path_buf[FILE_PATH_SIZE];
static char app_file_current_path_buf[FILE_PATH_SIZE];
static char app_file_name_buf[FILE_PATH_SIZE];
static char app_file_prev_paths[FILE_PATH_DEPTH][FILE_PATH_SIZE];
/* 文件的后缀名，可以在这个数组添加未知的后缀 */
char *lv_suffix [] ={".txt", ".avi", ".png", "jpeg", ".jpg", ".bin", ".gif", ".bmp", ".FON", ".dat", ".sif", ".BIN", ".xbf", ".ttf"};
#define LV_SUFFIX(x)    (int)(sizeof(x)/sizeof(x[0])) /* 计算lv_suffix数组的大小 */

uint16_t lv_scan_files (const char *path, lv_obj_t *parent);
void lv_del_list(lv_obj_t *parent);
void lv_create_list(lv_obj_t *parent);
void lv_mainstart(void);
void list_init(lv_obj_t *parent);
lv_obj_t *lv_create_page(lv_obj_t *parent);
static char *lv_pash_joint(void);
static void app_file_prepare_path_buffers(void);

long lv_tell(lv_fs_file_t *fd)
{
    uint32_t pos = 0;
    lv_fs_tell(fd, &pos);
    printf("\nlv_tcur pos is: %ld\n", pos);
    return pos;
}
/**
 * @brief  读取文件内容
 * @param  path：文件路径
 * @return LV_FS_RES_OK：读取成功
 */
lv_fs_res_t lv_file_read(const char *path)
{
    uint32_t rsize = 0;
    lv_fs_file_t fd;
    lv_fs_res_t res;

    if (!lv_smail_icon_get_state(TF_STATE) || !sd_card_is_mounted())
    {
        return LV_FS_RES_UNKNOWN;
    }

    app_file_sd_busy = 1;
    res = lv_fs_open(&fd, path, LV_FS_MODE_RD);
    
    if (res != LV_FS_RES_OK)
    {
        app_file_sd_busy = 0;
        printf("open %s ERROR\n",path);
        return LV_FS_RES_UNKNOWN;
    }

    lv_tell(&fd);
    lv_fs_seek(&fd,0,LV_FS_SEEK_SET);
    lv_tell(&fd);
    memset(lv_flie.rbuf, 0, sizeof(lv_flie.rbuf));
    res = lv_fs_read(&fd, lv_flie.rbuf, FILE_SEZE - 1, &rsize);
    if (rsize >= FILE_SEZE)
    {
        rsize = FILE_SEZE - 1;
    }
    lv_flie.rbuf[rsize] = '\0';

    if (res != LV_FS_RES_OK)
    {
        lv_fs_close(&fd);
        app_file_sd_busy = 0;
        printf("read %s ERROR\n",path);
        return LV_FS_RES_UNKNOWN;
    }

    lv_tell(&fd);
    lv_fs_close(&fd);
    app_file_sd_busy = 0;
    
    return LV_FS_RES_OK;
}

/**
  * @brief  页面返回按键回调函数
  * @param  obj  : 对象
  * @param  event: 事件
  * @retval 无
  */
void lv_btn_close_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_RELEASED)
    {
        if (lv_flie.lv_image_read != NULL && lv_obj_is_valid(lv_flie.lv_image_read))
        {
            lv_obj_del(lv_flie.lv_image_read);
        }
        lv_flie.lv_image_read = NULL;
        
        if (lv_flie.lv_page_cont != NULL && lv_obj_is_valid(lv_flie.lv_page_cont))
        {
            lv_obj_del(lv_flie.lv_page_cont);
        }
        lv_flie.lv_page_cont = NULL;
        lv_flie.lv_return_page = NULL;
    }
}

/**
  * @brief  创建page页面
  * @param  parent:父类
  * @retval 无
  */
lv_obj_t *lv_create_page(lv_obj_t *parent)
{
    lv_flie.lv_page_cont = lv_obj_create(parent);                           /* 创建容器 */
    lv_obj_set_size(lv_flie.lv_page_cont, lcd_dev.width, lcd_dev.height);
    lv_obj_set_style_radius(lv_flie.lv_page_cont, 0, LV_STATE_DEFAULT);       /* 设置圆半径为0 */
    lv_obj_clear_flag(lv_flie.lv_page_cont, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(lv_flie.lv_page_cont, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_align_to(lv_flie.lv_page_cont, parent, LV_ALIGN_CENTER, 0, 0);
  
    lv_obj_t *lv_page_obj = lv_obj_create(lv_flie.lv_page_cont);    /* 创建返回按键的区域 */
    lv_obj_set_style_bg_color(lv_page_obj, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_DEFAULT);
    lv_obj_align(lv_page_obj, LV_ALIGN_BOTTOM_MID, 0, 10);
    lv_obj_set_size(lv_page_obj, lcd_dev.width, myFont24.line_height);

    lv_obj_t *lv_page_back_btn = lv_label_create(lv_page_obj);      /* 创建lable作为返回的对象 */
    lv_obj_set_style_text_font(lv_page_back_btn, &myFont24, LV_STATE_DEFAULT);
    lv_label_set_text(lv_page_back_btn, "返回");
    lv_obj_align_to(lv_page_back_btn, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(lv_page_back_btn, LV_OBJ_FLAG_CLICKABLE);       /* 设置标签可点击 */
    lv_obj_add_event_cb(lv_page_back_btn, lv_btn_close_event, LV_EVENT_ALL, NULL); /* 设置回调函数 */

    return lv_flie.lv_page_cont;
}

/**
  * @brief  显示.txt文件
  * @param  parent:父类
  * @retval 无
  */
void lv_show_filetxt(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_STATE_DEFAULT);
    lv_obj_set_width(label, lv_obj_get_width(parent));
    lv_label_set_text(label, (char *)lv_flie.rbuf); /* 显示读取的数据 */
    memset(lv_flie.rbuf, 0, sizeof(lv_flie.rbuf));
}

/**
  * @brief  显示.bin图片
  * @param  parent:父类
  * @param  path:路径
  * @retval 无
  */
void lv_show_imgbin(lv_obj_t *parent, const char *path)
{
    lv_flie.lv_image_read = lv_img_create(parent);                          /* 创建 image 控件 */   
    lv_img_set_src(lv_flie.lv_image_read,path);                             /* 为控件设置图片 */
//    lv_img_set_auto_size(lv_flie.lv_image_read, true);                    /* 自动设置大小 */
    lv_obj_align_to(lv_flie.lv_image_read, parent, LV_ALIGN_CENTER, 0, 0);  /* 设置控件的对齐方式,相对坐标 */
}

/**
  * @brief  文件路径拼接
  * @param  无
  * @retval 无
  */
static void app_file_prepare_path_buffers(void)
{
    memset(app_file_path_buf, 0, sizeof(app_file_path_buf));
    memset(app_file_current_path_buf, 0, sizeof(app_file_current_path_buf));
    memset(app_file_name_buf, 0, sizeof(app_file_name_buf));
    memset(app_file_prev_paths, 0, sizeof(app_file_prev_paths));

    lv_flie.pname = app_file_path_buf;
    lv_flie.lv_pname = app_file_name_buf;
    snprintf(app_file_current_path_buf, sizeof(app_file_current_path_buf), "%s", "0:");
    lv_flie.lv_pash = app_file_current_path_buf;

    for (int i = 0; i < FILE_PATH_DEPTH; i++)
    {
        lv_flie.lv_prev_file[i] = app_file_prev_paths[i];
    }

    snprintf(lv_flie.lv_prev_file[0], FILE_PATH_SIZE, "%s", "0:");
}

static char *lv_pash_joint(void)
{
    uint8_t save_prev = (lv_flie.lv_suffix_flag == 1) ? 1 : 0;
    char base_path[FILE_PATH_SIZE];
    char full_path[FILE_PATH_SIZE];
    size_t base_length;
    int written;

    if (lv_flie.lv_pash == NULL || lv_flie.lv_pname == NULL ||
        lv_flie.pname == NULL || lv_flie.lv_pname[0] == '\0')
    {
        return NULL;
    }

    written = snprintf(base_path, sizeof(base_path), "%s", lv_flie.lv_pash);
    if (written < 0 || written >= (int)sizeof(base_path))
    {
        return NULL;
    }
    base_length = (size_t)written;

    written = snprintf(full_path, sizeof(full_path), "%s/%s", base_path, lv_flie.lv_pname);
    if (written < 0 || written >= (int)sizeof(full_path))
    {
        return NULL;
    }

    if (save_prev)
    {
        if (lv_flie.lv_prev_file_flag < 0 || lv_flie.lv_prev_file_flag >= FILE_PATH_DEPTH ||
            lv_flie.lv_prev_file[lv_flie.lv_prev_file_flag] == NULL)
        {
            return NULL;
        }

        memcpy(lv_flie.lv_prev_file[lv_flie.lv_prev_file_flag], base_path, base_length + 1);
        lv_flie.lv_prev_file_flag++;
    }

    memcpy(lv_flie.pname, full_path, (size_t)written + 1);
    return lv_flie.pname;
}

#if 0

    lv_flie.lv_prev_file[lv_flie.lv_prev_file_flag] = (char *)lv_flie.lv_pash;/* 把上一个路径保存到这个数组里 */
    lv_flie.lv_prev_file_flag ++;                              /* 前一个路径数量标志位加一 */
  
    strcpy((char *)lv_flie.pname, lv_flie.lv_pash);            /* 复制路径(目录) */ 
    strcat((char *)lv_flie.pname, "/");                        /* 复制路径(目录) */ 
    strcat((char *)lv_flie.pname, (char *)lv_flie.lv_pname);   /* 将文件名接在后面 */
    return lv_flie.pname;
}

/**
  * @brief  列表按键回调函数
  * @param  event: 事件
  * @retval 无
  */
#endif

#if 0
static void lv_list_btn_event_legacy(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *obj = lv_event_get_target(event);
  
    if(code == LV_EVENT_CLICKED)
    {
        for (int i = 0; i <= lv_flie.list_flie_nuber ;i++)  /* 轮询列表项 */
        {
            if (obj == lv_flie.list_btn[i]) /* 判断列表项的那个按键按下 */
            {   
                lv_flie.lv_pname = malloc(255);         /* 获取文件名分配内存 */
                lv_flie.pname = malloc(255);               /* 为带路径的文件名分配内存 */

                lv_flie.lv_pname = (char *)lv_list_get_btn_text(lv_flie.list, lv_flie.list_btn[i]);  /* 获取列表项的值 */
                for (int suffix = 0; suffix < LV_SUFFIX(lv_suffix); suffix ++)      /* 轮询文件后缀 */
                {
                    if (strstr(lv_flie.lv_pname, lv_suffix[suffix]) != NULL)        /* 如果不是文件夹 */
                    {
                        lv_flie.lv_suffix_flag = 0;                                 /* 设置后缀的标志位为0 */
                        break;
                    }
                }

                if (lv_flie.lv_suffix_flag == 1)                                  /* 该标志位不为0就是文件夹操作 */
                {   
                    lv_flie.lv_pash = lv_pash_joint();                            /* 把文件路径转递给lv_pash参数 */
                    lv_del_list(lv_flie.list);                                    /* 删除列表 */
                    lv_scan_files(lv_flie.pname, lv_scr_act());                   /* 重新创建文件列表 */
                }
                else
                {
                    if (strstr(lv_flie.lv_pname, ".txt") != NULL)                 /* 如果不是文件夹且是txt文件 */
                    {
                        if (lv_file_read(lv_pash_joint()) == LV_FS_RES_OK)        /* 判断读取txt文件是否成功 */
                        {
                            lv_flie.lv_return_page = lv_create_page(lv_scr_act());/* 创建页面 */
                            lv_show_filetxt(lv_flie.lv_return_page);              /* 在页面显示txt文件数据 */
                        }
                    }
                    else if (strstr(lv_flie.lv_pname, ".bin") != NULL)            /* 如果不是文件夹且是bin文件 */
                    {
                        lv_flie.lv_return_page = lv_create_page(lv_scr_act());    /* 创建页面 */
                        lv_show_imgbin(lv_flie.lv_return_page, lv_pash_joint());  /* 显示bin图片 */
                    }

                    lv_flie.lv_suffix_flag = 1; /* 恢复文件夹点击 */
                }
            }
        }
    }
}

/**
  * @brief  读取文件名
  * @param  char* path: 要扫描的文件路径
  * @retval FR_OK：成功，否则失败
  */
#endif

static void lv_list_btn_event_safe(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *obj = lv_event_get_target(event);

    if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    for (int i = 0; i < lv_flie.list_flie_nuber; i++)
    {
        if (obj != lv_flie.list_btn[i])
        {
            continue;
        }

        const char *selected_name = lv_list_get_btn_text(lv_flie.list, lv_flie.list_btn[i]);
        int written = selected_name ? snprintf(lv_flie.lv_pname, FILE_PATH_SIZE, "%s", selected_name) : -1;
        if (written < 0 || written >= FILE_PATH_SIZE)
        {
            return;
        }

        lv_flie.lv_suffix_flag = (lv_flie.list_attr[i] & AM_DIR) ? 1 : 0;

        char *full_path = lv_pash_joint();
        if (full_path == NULL)
        {
            lv_flie.lv_suffix_flag = 1;
            return;
        }

        if (lv_flie.lv_suffix_flag == 1)
        {
            if (snprintf(app_file_current_path_buf, sizeof(app_file_current_path_buf), "%s", full_path) >= (int)sizeof(app_file_current_path_buf))
            {
                lv_flie.lv_suffix_flag = 1;
                return;
            }
            lv_flie.lv_pash = app_file_current_path_buf;
            lv_del_list(lv_flie.list);
            lv_scan_files(lv_flie.lv_pash, lv_scr_act());
        }
        else
        {
            if (strstr(lv_flie.lv_pname, ".txt") != NULL)
            {
                if (lv_file_read(full_path) == LV_FS_RES_OK)
                {
                    lv_flie.lv_return_page = lv_create_page(lv_scr_act());
                    lv_show_filetxt(lv_flie.lv_return_page);
                }
            }
            else if (strstr(lv_flie.lv_pname, ".bin") != NULL)
            {
                lv_flie.lv_return_page = lv_create_page(lv_scr_act());
                lv_show_imgbin(lv_flie.lv_return_page, full_path);
            }

            lv_flie.lv_suffix_flag = 1;
        }

        return;
    }
}

uint16_t lv_scan_files (const char *path, lv_obj_t *parent)
{
    if (lv_flie.pname != app_file_path_buf ||
        lv_flie.lv_pname != app_file_name_buf ||
        lv_flie.lv_prev_file[0] != app_file_prev_paths[0])
    {
        app_file_prepare_path_buffers();
    }

    if (!lv_smail_icon_get_state(TF_STATE))
    {
        return FR_NOT_READY;
    }

    app_file_sd_busy = 1;
    lv_flie.fr = sd_f_opendir(&lv_flie.lv_dir, path);

    memset(lv_flie.list_btn, 0, sizeof(lv_flie.list_btn));
    memset(lv_flie.list_attr, 0, sizeof(lv_flie.list_attr));
    lv_flie.list_flie_nuber = 0;
    lv_create_list(parent);

    if (lv_flie.fr == FR_OK)
    {
        while(1)
        {
            if (!lv_smail_icon_get_state(TF_STATE))
            {
                lv_flie.fr = FR_NOT_READY;
                break;
            }

            lv_flie.fr = sd_f_readdir(&lv_flie.lv_dir, &lv_flie.SD_fno);

            if ((lv_flie.fr) || lv_flie.SD_fno.fname[0] == 0) break;
            if (lv_flie.list_flie_nuber >= LIST_SIZE) break;

            if (lv_flie.SD_fno.fattrib & AM_DIR)
            {
                lv_flie.list_btn[lv_flie.list_flie_nuber] = lv_list_add_btn(lv_flie.list, LV_SYMBOL_DIRECTORY, lv_flie.SD_fno.fname);
            }
            else
            {
                if (  strstr(lv_flie.SD_fno.fname,".png")  != NULL
                    ||strstr(lv_flie.SD_fno.fname,".jpeg") != NULL
                    ||strstr(lv_flie.SD_fno.fname,".jpg")  != NULL
                    ||strstr(lv_flie.SD_fno.fname,".bmp")  != NULL
                    ||strstr(lv_flie.SD_fno.fname,".gif")  != NULL
                    ||strstr(lv_flie.SD_fno.fname,".avi")  != NULL)
                {
                    lv_flie.image_scr = LV_SYMBOL_IMAGE;
                }
                else
                {
                    lv_flie.image_scr = LV_SYMBOL_FILE;
                }
                
                lv_flie.list_btn[lv_flie.list_flie_nuber] = lv_list_add_btn(lv_flie.list, lv_flie.image_scr, lv_flie.SD_fno.fname);
            }
            
            if (lv_flie.list_btn[lv_flie.list_flie_nuber] == NULL) break;

            lv_flie.list_attr[lv_flie.list_flie_nuber] = lv_flie.SD_fno.fattrib;
            lv_obj_set_style_pad_left(lv_flie.list_btn[lv_flie.list_flie_nuber], 5, LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(lv_flie.list_btn[lv_flie.list_flie_nuber], 5, LV_STATE_DEFAULT);
            lv_obj_add_event_cb(lv_flie.list_btn[lv_flie.list_flie_nuber], lv_list_btn_event_safe, LV_EVENT_ALL, NULL);
            lv_flie.list_flie_nuber++;
        }
        
        sd_f_closedir(&lv_flie.lv_dir);
    }

    app_file_sd_busy = 0;
    
    return lv_flie.fr;
}

/**
  * @brief  删除列表
  * @param  parent: 父类
  * @retval 无
  */
void lv_del_list(lv_obj_t *parent)
{
    if (parent != NULL && lv_obj_is_valid(parent))
    {
        lv_obj_del(parent);
    }

    if (parent == lv_flie.list)
    {
        lv_flie.list = NULL;
    }
}

static void lv_animation_set_x(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, (lv_coord_t)value);
}

/**
  * @brief  设置列表动画
  * @param  parent: 父类
  * @retval 无
  */
void lv_animation(lv_obj_t *parent)
{
    lv_anim_t a;                                                /* 第一步：定义一个动画 */
    lv_anim_init(&a);                                           /* 第二步初始化动画 */
    lv_anim_set_var(&a, parent);                                /* 第三步：设置实施动画效果的控件 */
    lv_anim_set_exec_cb(&a, lv_animation_set_x);                /* 设置动画功能 var */
    int32_t start = lv_obj_get_width(lv_scr_act());
    int32_t end = 0;
    lv_anim_set_values(&a, start, end);                         /* 设置开始值和结束值。例如0,150 */
    lv_anim_set_time(&a, 300);                                  /* 动画的长度[ms]设置300mS */
    lv_anim_start(&a);                                          /* 第四步：创建动画 */
}

/**
  * @brief  创建列表
  * @param  parent: 父类
  * @retval 无
  */
void lv_create_list(lv_obj_t *parent)
{
    lv_flie.list = lv_list_create(parent);  /* 创建列表 */
//    lv_animation(lv_flie.list);
    lv_obj_set_size(lv_flie.list, lcd_dev.width, lcd_dev.height - myFont24.line_height * 2 - 10);   /* 设置列表的大小 */
    lv_obj_align_to(lv_flie.list, lv_flie.lv_page_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);             /* 设置列表的对齐模式 */
    lv_obj_set_style_text_font(lv_flie.list, &lv_font_montserrat_24, LV_STATE_DEFAULT);             /* 设置字体 */
    lv_obj_set_style_radius(lv_flie.list, 0, LV_STATE_DEFAULT);                                     /* 设置圆半径为0 */
}

/**
  * @brief  创建页面标题
  * @param  parent: 父类
  * @retval 无
  */
void lv_page_tile(lv_obj_t *parent)
{
    lv_flie.lv_page_obj = lv_obj_create(parent);
    lv_obj_set_size(lv_flie.lv_page_obj, lcd_dev.width, myFont24.line_height);
    lv_obj_set_style_bg_color(lv_flie.lv_page_obj, lv_palette_main(LV_PALETTE_GREY), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(lv_flie.lv_page_obj, 0, LV_STATE_DEFAULT);          /* 设置圆半径为0 */
    lv_obj_align_to(lv_flie.lv_page_obj, parent, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t *lv_page_label = lv_label_create(lv_flie.lv_page_obj);
    lv_label_set_text(lv_page_label, "文件管理系统");
    lv_obj_set_style_text_color(lv_page_label, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lv_page_label, &myFont24,LV_STATE_DEFAULT);
    lv_obj_align_to(lv_page_label, lv_flie.lv_page_obj, LV_ALIGN_CENTER, 0, 0);
}

/**
  * @brief  返回按键回调函数
  * @param  obj:对象
  * @param  event:事件
  * @retval 无
  */
void lv_back_btn_event_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *obj = lv_event_get_target(event);
  
    if(code == LV_EVENT_SHORT_CLICKED)
    {
        if (obj == lv_flie.lv_back_btn)
        {
            lv_file_del();
            return;
        }

        if (obj == lv_flie.lv_prev_btn)
        {
            if (!lv_smail_icon_get_state(TF_STATE))
            {
                app_obj_general.app_state = DEL_STATE;
                return;
            }

            lv_flie.lv_prev_file_flag--;

            if (lv_flie.lv_prev_file_flag <= 0)
            {
                lv_flie.lv_prev_file_flag = 0;
            }
          
            lv_del_list(lv_flie.list);
            snprintf(app_file_current_path_buf, sizeof(app_file_current_path_buf), "%s", lv_flie.lv_prev_file[lv_flie.lv_prev_file_flag]);
            lv_flie.lv_pash = app_file_current_path_buf;
            lv_scan_files(lv_flie.lv_pash, lv_scr_act());
        }
    }
}

/**
  * @brief  返回按键
  * @param  parent:父类
  * @retval 无
  */
void lv_general_win_create(lv_obj_t *parent)
{
    lv_flie.lv_back_btn = lv_label_create(parent);
    lv_obj_set_style_text_font(lv_flie.lv_back_btn, &myFont24, LV_STATE_DEFAULT);   /* 设置字体 */
    
    lv_label_set_text(lv_flie.lv_back_btn, "菜单");
    lv_obj_align_to(lv_flie.lv_back_btn, parent, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_flag(lv_flie.lv_back_btn, LV_OBJ_FLAG_CLICKABLE);                    /* 设置标签可点击 */
    lv_obj_add_event_cb(lv_flie.lv_back_btn, lv_back_btn_event_handler, LV_EVENT_ALL, NULL); /* 设置回调函数 */
    lv_flie.lv_prev_btn = lv_label_create(parent);
    lv_obj_set_style_text_font(lv_flie.lv_prev_btn, &myFont24, LV_STATE_DEFAULT);
    lv_label_set_text(lv_flie.lv_prev_btn, "返回");
    lv_obj_align_to(lv_flie.lv_prev_btn, parent, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_flag(lv_flie.lv_prev_btn, LV_OBJ_FLAG_CLICKABLE);        /* 设置标签可点击 */
    lv_obj_add_event_cb(lv_flie.lv_prev_btn, lv_back_btn_event_handler, LV_EVENT_ALL, NULL);
}

/**
  * @brief  文件系统返回/菜单按键的区域
  * @param  parent: 父类
  * @retval 无
  */
void lv_page_back(lv_obj_t *parent)
{
    lv_flie.lv_back_obj = lv_obj_create(parent);                                                            /* 创建文件返回对象区域 */
    lv_obj_set_size(lv_flie.lv_back_obj, lcd_dev.width, myFont24.line_height + 10);                              /* 设置改区域的大小 */
    lv_obj_set_style_bg_color(lv_flie.lv_back_obj, lv_palette_main(LV_PALETTE_GREY), LV_STATE_DEFAULT);       /* 设置该区域的颜色为灰色 */
    lv_obj_set_style_radius(lv_flie.lv_back_obj, 0, LV_STATE_DEFAULT);                                        /* 设置圆半径为0 */
    lv_obj_align_to(lv_flie.lv_back_obj, parent, LV_ALIGN_BOTTOM_MID, 0, 0);                                    /* 设置对齐模式 */
    lv_general_win_create(lv_flie.lv_back_obj);                                                             /* 创建返回和菜单按键 */
}

/**
  * @brief  列表初始化
  * @param  parent: 父类
  * @retval 无
  */
void list_init(lv_obj_t *parent)
{
    lv_flie.lv_pash = "0:";                                 /* 初始路径 */
    lv_flie.lv_prev_file_flag = 0;                          /* 上一个文件路径标志位清0 */
    lv_flie.lv_prev_file[lv_flie.lv_prev_file_flag] = "0:"; /* 初始上个文件夹路径 */
    lv_flie.list_flie_nuber = 0;                            /* 初始文件数量 */
    lv_flie.lv_suffix_flag = 1;                             /* 文件后缀标志位 */
    lv_scan_files(lv_flie.lv_pash,parent);                  /* 读取文件路径 */
}

/**
  * @brief  控件测试函数
  * @param  无
  * @retval 无
  */
void app_file_init(void)
{
    if (!lv_smail_icon_get_state(TF_STATE) || !sd_card_is_mounted())
    {
        lv_msgbox("SD device not detected");
        return ;
    }

    lv_page_tile(lv_scr_act());
    lv_page_back(lv_scr_act());
    list_init(lv_scr_act());

    app_obj_general.del_parent = lv_flie.lv_page_obj;
    app_obj_general.APP_Function = lv_file_del;
    app_obj_general.app_state = NOT_DEL_STATE;
    app_obj_general.requires_sd = 1;
}

/**
 * @brief       文件系统界面退出
 * @param       无
 * @retval      无
 */
void lv_file_del(void)
{
    if (lv_flie.lv_page_cont != NULL && lv_obj_is_valid(lv_flie.lv_page_cont))
    {
        lv_obj_del(lv_flie.lv_page_cont);
    }

    if (lv_flie.list != NULL && lv_obj_is_valid(lv_flie.list))
    {
        lv_obj_del(lv_flie.list);
    }

    if (lv_flie.lv_page_obj != NULL && lv_obj_is_valid(lv_flie.lv_page_obj))
    {
        lv_obj_del(lv_flie.lv_page_obj);
    }

    if (lv_flie.lv_back_obj != NULL && lv_obj_is_valid(lv_flie.lv_back_obj))
    {
        lv_obj_del(lv_flie.lv_back_obj);
    }

    lv_flie.list = NULL;
    lv_flie.lv_page_obj = NULL;
    lv_flie.lv_back_obj = NULL;
    lv_flie.lv_prev_btn = NULL;
    lv_flie.lv_back_btn = NULL;
    lv_flie.lv_page_cont = NULL;
    lv_flie.lv_return_page = NULL;
    lv_flie.lv_image_read = NULL;
    lv_flie.pname = NULL;
    lv_flie.lv_pname = NULL;
    lv_flie.lv_pash = NULL;
    lv_flie.lv_prev_file_flag = 0;
    lv_flie.list_flie_nuber = 0;
    lv_flie.lv_suffix_flag = 1;

    app_obj_general.APP_Function = NULL;
    app_obj_general.del_parent = NULL;
    app_obj_general.app_state = NOT_DEL_STATE;
    app_obj_general.requires_sd = 0;
    lv_display_box();
}
