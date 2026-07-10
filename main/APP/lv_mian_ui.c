/**
 ******************************************************************************
 * @file        lv_main_ui.c
 * @version     V1.0
 * @brief       LVGL 综合实验
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "lv_main_ui.h"
#include "esp_log.h"

/* 声明WKS LOGO */
LV_IMG_DECLARE(bg)
LV_IMG_DECLARE(camera)
LV_IMG_DECLARE(lv_video)
LV_IMG_DECLARE(music)
LV_IMG_DECLARE(Photo)
LV_IMG_DECLARE(app_calendar)
LV_IMG_DECLARE(brush)
LV_IMG_DECLARE(calculator)
LV_IMG_DECLARE(l_file)
LV_IMG_DECLARE(l_wifi)
LV_IMG_DECLARE(usb)
LV_IMG_DECLARE(game)
LV_IMG_DECLARE(lv_wifi)
LV_IMG_DECLARE(lv_tf)
LV_IMG_DECLARE(lv_usb)
LV_IMG_DECLARE(novoice)
LV_IMG_DECLARE(cell)

/* 声明字体 */
LV_FONT_DECLARE(Font11chn)          /* 声明Font11chn字体,Font11chn.c需要存放于"LVGL\src\font"文件夹下 */
LV_FONT_DECLARE(Font48)

extern lv_indev_t *indev_keypad;    /* 按键组 */
extern uint32_t back_act_key;       /* 返回按键 */
lv_group_t *ctrl_g;

lv_main_ui_t main_ui;
lv_m_general_t app_obj_general;
lv_obj_t * back_btn;
unsigned int  app_readly_list[32];
uint8_t lv_trigger_bit = 0;
static lv_obj_t *sd_toast_obj = NULL;
static lv_timer_t *sd_toast_timer = NULL;

/**
 * @brief       添加小图标的状态
 * @param       state:状态值
 * @retval      无
 */
void lv_smail_icon_add_state(small_icon_state_t state)
{
    main_ui.small_icon_state |= state;
}

/**
 * @brief       清除小图标的状态
 * @param       state:状态值
 * @retval      无
 */
void lv_smail_icon_clear_state(small_icon_state_t state)
{
    main_ui.small_icon_state &= ~(state);
}

/**
 * @brief       获取小图标的状态
 * @param       state:状态值
 * @retval      返回判断值
 */
uint8_t lv_smail_icon_get_state(small_icon_state_t state)
{
    if (main_ui.small_icon_state & state)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void lv_mem_log(void)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI("LVGL_MEM", "free:%lu biggest:%lu used:%d%% frag:%d%%",
             (unsigned long)mon.free_size, (unsigned long)mon.free_biggest_size,
             mon.used_pct, mon.frag_pct);
}

/**
 * @brief       消息提示删除
 * @param       无
 * @retval      无
 */
void lv_focus_restore(void)
{
    if (indev_keypad == NULL || ctrl_g == NULL)
    {
        return;
    }

    lv_group_remove_all_objs(ctrl_g);
    for (int i = 0; i < main_ic0_num; i++)
    {
        if (main_ui.mian_inter.ico[i] != NULL && lv_obj_is_valid(main_ui.mian_inter.ico[i]))
        {
            lv_group_add_obj(ctrl_g, main_ui.mian_inter.ico[i]);
        }
    }
    if (main_ui.mian_inter.ico[0] != NULL && lv_obj_is_valid(main_ui.mian_inter.ico[0]))
    {
        lv_group_focus_obj(main_ui.mian_inter.ico[0]);
    }
}

static void lv_close_active_app(void)
{
    lv_obj_t *parent = app_obj_general.del_parent;
    void (*close_fn)(void) = app_obj_general.APP_Function;

    if (parent == NULL || close_fn == NULL)
    {
        return;
    }

    close_fn();

    if (app_obj_general.del_parent == parent && !lv_obj_is_valid(parent))
    {
        app_obj_general.del_parent = NULL;
        app_obj_general.APP_Function = NULL;
        app_obj_general.app_state = NOT_DEL_STATE;
        app_obj_general.requires_sd = 0;
    }
}

static void lv_msgbox_delete_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    if (obj == app_obj_general.current_parent)
    {
        app_obj_general.current_parent = NULL;
        app_obj_general.Function = NULL;
    }
}

void lv_msgbox_del(void)
{
    if (app_obj_general.current_parent != NULL && lv_obj_is_valid(app_obj_general.current_parent))
    {
        lv_msgbox_close(app_obj_general.current_parent);
    }
    app_obj_general.current_parent = NULL;
    app_obj_general.Function = NULL;

    if (app_obj_general.del_parent == NULL)
    {
        lv_focus_restore();
    }
}

/**
 * @brief       hidden box
 * @param       None
 * @retval      None
 */
void lv_hidden_box(void)
{
    if (main_ui.mian_box != NULL && lv_obj_is_valid(main_ui.mian_box))
    {
        lv_obj_add_flag(main_ui.mian_box, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief       display box
 * @param       None
 * @retval      None
 */
void lv_display_box(void)
{
    if (main_ui.mian_box != NULL && lv_obj_is_valid(main_ui.mian_box))
    {
        lv_obj_clear_flag(main_ui.mian_box, LV_OBJ_FLAG_HIDDEN);
    }
    lv_focus_restore();
}

/**
 * @brief       消息提示
 * @param       name : 消息内容
 * @retval      无
 */
void lv_msgbox(char *name)
{
    if (app_obj_general.current_parent != NULL)
    {
        if (lv_obj_is_valid(app_obj_general.current_parent))
        {
            lv_msgbox_close(app_obj_general.current_parent);
        }
        app_obj_general.current_parent = NULL;
        app_obj_general.Function = NULL;
    }

    lv_obj_t *msgbox = lv_msgbox_create(NULL,LV_SYMBOL_WARNING "Notice",name, NULL,true);
    lv_obj_set_size(msgbox, lv_obj_get_width(lv_scr_act()) - 20, lv_obj_get_height(lv_scr_act()) / 3);
    lv_obj_center(msgbox);
    lv_obj_set_style_border_width(msgbox, 0, 0);
    lv_obj_set_style_shadow_width(msgbox, 20, 0);
    lv_obj_set_style_shadow_color(msgbox, lv_color_hex(0xa9a9a9),LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(msgbox,18,LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(msgbox,20,LV_STATE_DEFAULT);

    lv_obj_t *title = lv_msgbox_get_title(msgbox);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14,LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xff0000),LV_STATE_DEFAULT);

    lv_obj_t *content = lv_msgbox_get_content(msgbox);
    lv_obj_set_style_text_font(content, &lv_font_montserrat_14,LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(content, lv_color_hex(0x6c6c6c),LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(content,15,LV_STATE_DEFAULT);
    app_obj_general.current_parent = msgbox;
    app_obj_general.Function = lv_msgbox_del;
    lv_obj_add_event_cb(msgbox, lv_msgbox_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_move_foreground(back_btn);
}

static void lv_sd_toast_timer_cb(lv_timer_t *timer)
{
    if (sd_toast_obj != NULL)
    {
        lv_obj_del(sd_toast_obj);
        sd_toast_obj = NULL;
    }

    sd_toast_timer = NULL;
    lv_timer_del(timer);
}

void lv_sd_toast(char *name)
{
    if (!lvgl_mux_lock(100))
    {
        return;
    }

    if (sd_toast_timer != NULL)
    {
        lv_timer_del(sd_toast_timer);
        sd_toast_timer = NULL;
    }

    if (sd_toast_obj != NULL)
    {
        lv_obj_del(sd_toast_obj);
        sd_toast_obj = NULL;
    }

    sd_toast_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sd_toast_obj, 190, 44);
    lv_obj_align(sd_toast_obj, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_radius(sd_toast_obj, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sd_toast_obj, lv_color_hex(0x20242a), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(sd_toast_obj, LV_OPA_80, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(sd_toast_obj, LV_OPA_0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(sd_toast_obj, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(sd_toast_obj, LV_OPA_30, LV_STATE_DEFAULT);
    lv_obj_clear_flag(sd_toast_obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(sd_toast_obj);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_STATE_DEFAULT);
    lv_label_set_text_fmt(label, LV_SYMBOL_SD_CARD " %s", name);
    lv_obj_center(label);

    lv_obj_move_foreground(sd_toast_obj);
    sd_toast_timer = lv_timer_create(lv_sd_toast_timer_cb, 2000, NULL);
    lvgl_mux_unlock();
}

/**
  * @brief  计算前导置零
  * @param  app_readly_list: 就绪表
  * @retval 返回就绪位
  */
int lv_clz(unsigned int  app_readly_list[])
{
    int bit = 0;

    for (int i = 0; i < 32; i++)
    {
        if (app_readly_list[i] == 1)
        {
            break;
        }

        bit ++ ;
    }

    return bit;
}

/**
  * @brief  返回按键回调函数
  * @param  event : 事件
  * @retval 无
  */
static void lv_backbtn_control_event_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t * obj = lv_event_get_target(event);

    if (code == LV_EVENT_PRESSING)
    {
        lv_indev_t * indev = lv_indev_get_act();
        if(indev == NULL)  return;

        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);
        lv_coord_t x = lv_obj_get_x(obj) + vect.x;
        lv_coord_t y = lv_obj_get_y(obj) + vect.y;

        if (x > 0 && y > 0 && x < lv_obj_get_width(lv_scr_act()) - 50 && y < lv_obj_get_height(lv_scr_act()) - 50)
        {
            lv_obj_set_pos(obj, x, y);
        }
    }
    else if (code == LV_EVENT_CLICKED)
    {
        if (app_obj_general.current_parent != NULL)
        {
            if (app_obj_general.Function != NULL)
            {
                app_obj_general.Function();
            }
            return;
        }

        if (app_obj_general.del_parent != NULL && app_obj_general.APP_Function != NULL)
        {
            lv_close_active_app();
        }
    }
}

/**
  * @brief  按键回调函数
  * @param  event : 事件
  * @retval None
  */
static void lv_imgbtn_control_event_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t * obj = lv_event_get_target(event);

    if (code == LV_EVENT_RELEASED)
    {
        for (int i = 0;i < main_ic0_num;i ++)
        {
            if (obj == main_ui.mian_inter.ico[i])
            {
                app_readly_list[i] = 1 ;
            }
        }

        lv_trigger_bit = ((unsigned int)lv_clz((app_readly_list)));
        app_readly_list[lv_trigger_bit] = 0;
        lv_mem_log();
        app_obj_general.requires_sd = 0;

        if (indev_keypad != NULL)
        {
            for (int i = 0; i <= 8;i++)
            {
                lv_group_remove_obj(main_ui.mian_inter.ico[i]);
            }
        }

		switch(lv_trigger_bit)
        {
            case 0: /* calculator app */
			    lv_app_calculator_init();			
                break;
            
            case 1: /*  brush app */
			    lv_app_brush_init();
                break;
            
            case 2: /* calendar app */
				app_calendar_ui_init();
                break;

		    case 3: /* photo app */
			    lv_photo_ui();
                break;
            
            case 4: /*  video app */
			    lv_video_ui();
                break;
            
            case 5: /* file app */
			    app_file_init();
                break;

		    case 6: /* wifi app */
			    wifi_app_init();
                break;
            
            case 7: /*  usb app */
			    app_usb_ui_init();
                break;
            
            case 8: /* game app */		
			    xiaoxiaole();
                break;
            default:
                break;
        }

        lv_mem_log();

        if (app_obj_general.del_parent == NULL && app_obj_general.current_parent == NULL)
        {
            lv_display_box();
        }
    }
}

/**
  * @brief  Obtain RTC time information
  * @param  None
  * @retval None
  */
static void lv_rtc_timer(lv_timer_t* timer)
{
    rtc_get_time();
    main_ui.rtc.year = calendar.year; 
    main_ui.rtc.month = calendar.month; 
    main_ui.rtc.date = calendar.date; 
    main_ui.rtc.hour = calendar.hour; 
    main_ui.rtc.minute = calendar.min; 
    main_ui.rtc.second = calendar.sec; 
    main_ui.rtc.week = calendar.week; 

    //lv_label_set_text_fmt(main_ui.mini_box.time,"%02d : %02d : %02d", main_ui.rtc.hour, main_ui.rtc.minute,main_ui.rtc.second);
    lv_label_set_text_fmt(main_ui.mian_inter.mian_time_text,"%02d : %02d", main_ui.rtc.hour, main_ui.rtc.minute);
    if (app_obj_general.del_parent != NULL && app_obj_general.app_state == DEL_STATE && app_obj_general.APP_Function != NULL)
    {
        lv_close_active_app();
    }

    // if (lv_smail_icon_get_state(TF_STATE))
    // {
    //     lv_obj_set_style_img_recolor(main_ui.mini_box.tf,lv_color_make(0,250,0),LV_STATE_DEFAULT);
    //     lv_obj_set_style_img_recolor_opa(main_ui.mini_box.tf,LV_OPA_50,LV_STATE_DEFAULT);

	// 	/* 选中SD卡 */
	// 	SD_CS(0);
    //      if (sdmmc_get_status(card) != ESP_OK)
    //      {
    //         lv_smail_icon_clear_state(TF_STATE);
    //      }
	// 	 /* 取消选中SD卡 */
	// 	 SD_CS(1);
    // }
    // else
    // {
    //     lv_obj_set_style_img_recolor(main_ui.mini_box.tf,lv_color_make(255,250,255),LV_STATE_DEFAULT);
    //     lv_obj_set_style_img_recolor_opa(main_ui.mini_box.tf,LV_OPA_100,LV_STATE_DEFAULT);

    //     if (sd_spi_init() == ESP_OK)
    //     {
	// 		/* 选中SD卡 */
	// 	    SD_CS(0);
    //          if (sdmmc_get_status(card) == ESP_OK)
    //          {
    //             lv_smail_icon_add_state(TF_STATE);
    //          }
	// 		 /* 取消选中SD卡 */
	// 	     SD_CS(1);
    //     }
    // }

    // if (app_obj_general.current_parent != NULL)
    // {
    //     app_obj_general.Function();
    // }

    // /* 处理过程中，TF卡被拔出。目的是返回主界面 */
    // if (app_obj_general.del_parent != NULL && app_obj_general.app_state == DEL_STATE)
    // {
    //     app_obj_general.APP_Function();
    //     app_obj_general.del_parent = NULL;
    //     app_obj_general.app_state = NOT_DEL_STATE;
    // }

    // if (lv_smail_icon_get_state(USB_STATE))
    // {
    //     lv_obj_set_style_img_recolor(main_ui.mini_box.usb,lv_color_make(0,250,0),LV_STATE_DEFAULT);
    //     lv_obj_set_style_img_recolor_opa(main_ui.mini_box.usb,LV_OPA_50,LV_STATE_DEFAULT);
    // }
    // else
    // {
    //     lv_obj_set_style_img_recolor(main_ui.mini_box.usb,lv_color_make(255,250,255),LV_STATE_DEFAULT);
    //     lv_obj_set_style_img_recolor_opa(main_ui.mini_box.usb,LV_OPA_100,LV_STATE_DEFAULT);
    // }
}

/*
*  主界面设计
*/
void lv_mian_ui(void)
{
    uint8_t ico_num = 0;
	const int icon_size = 70;
	const int cols = 3;
	const int rows = 3;
	const int start_y = 480 * 1/4; 
	const int area_height = 480 - start_y;
	const int margin_x = (320 - cols * icon_size) / (cols + 1);
	const int margin_y = (area_height - rows * icon_size) / (rows + 1);

    app_obj_general.current_parent = NULL;
    app_obj_general.hidden_parent = NULL;
    app_obj_general.del_parent = NULL;
    app_obj_general.app_state = NOT_DEL_STATE;
    app_obj_general.Function = NULL;
    app_obj_general.APP_Function = NULL;

    if (indev_keypad != NULL)
    {
        ctrl_g = lv_group_create();
        /* 创建按键组，用来控制 */
        lv_group_set_default(ctrl_g);
        lv_indev_set_group(indev_keypad, ctrl_g);
    }

    /* 主界面的总容器 */
    main_ui.mian_box = lv_obj_create(lv_scr_act());
    lv_obj_set_size(main_ui.mian_box,lv_obj_get_width(lv_scr_act()),lv_obj_get_height(lv_scr_act()));
    lv_obj_set_style_radius(main_ui.mian_box, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(main_ui.mian_box, LV_OPA_0, LV_STATE_DEFAULT);
    lv_obj_set_pos(main_ui.mian_box, 0, 0);
    lv_obj_clear_flag(main_ui.mian_box, LV_OBJ_FLAG_SCROLLABLE);

    // /* 主界面的顶层容器 */
    // main_ui.mian_inter.main_mini_obx = lv_obj_create(main_ui.mian_box);
    // lv_obj_set_size(main_ui.mian_inter.main_mini_obx,lv_obj_get_width(lv_scr_act()),20);
    // lv_obj_set_pos(main_ui.mian_inter.main_mini_obx, -15, -15);
    // lv_obj_set_style_bg_color(main_ui.mian_inter.main_mini_obx,lv_color_hex(0x000000),LV_STATE_DEFAULT);
    // lv_obj_set_style_radius(main_ui.mian_inter.main_mini_obx, 0, LV_STATE_DEFAULT);
    // lv_obj_set_style_border_opa(main_ui.mian_inter.main_mini_obx,LV_OPA_0,LV_STATE_DEFAULT);
    // lv_obj_clear_flag(main_ui.mian_inter.main_mini_obx, LV_OBJ_FLAG_SCROLLABLE);

    /* 主图背景 */
    // main_ui.mian_inter.mian_imagebg_obx = lv_img_create(main_ui.mian_box);
    // lv_img_set_src(main_ui.mian_inter.mian_imagebg_obx,&bg);
    // lv_obj_set_pos(main_ui.mian_inter.mian_imagebg_obx, -15, -15);
    // lv_obj_set_size(main_ui.mian_inter.mian_imagebg_obx, bg.header.w, bg.header.h);
	// app_obj_general.hidden_parent = main_ui.mian_inter.mian_imagebg_obx;

	/* 设置屏幕背景为黑色 */ 
	lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);

	/* 创建渐变对象 */ 
	main_ui.mian_inter.mian_imagebg_obx = lv_obj_create(main_ui.mian_box);
	lv_obj_set_pos(main_ui.mian_inter.mian_imagebg_obx, -15, -15);
	lv_obj_set_size(main_ui.mian_inter.mian_imagebg_obx, 320, 480);
	lv_obj_set_style_radius(main_ui.mian_inter.mian_imagebg_obx, 0, 0);            /* 无圆角 */ 
	lv_obj_set_style_border_width(main_ui.mian_inter.mian_imagebg_obx, 0, 0);      /* 无边框 */ 
	lv_obj_set_style_pad_all(main_ui.mian_inter.mian_imagebg_obx, 0, 0);           /* 无内边距 */ 
	lv_obj_set_style_bg_opa(main_ui.mian_inter.mian_imagebg_obx, LV_OPA_COVER, 0); /* 完全不透明 */ 

	/* 设置垂直渐变背景 */ 
	lv_obj_set_style_bg_grad_dir(main_ui.mian_inter.mian_imagebg_obx, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_color(main_ui.mian_inter.mian_imagebg_obx, lv_color_hex(0x55AB55), 0);      /* 顶部颜色 */ 
	lv_obj_set_style_bg_grad_color(main_ui.mian_inter.mian_imagebg_obx, lv_color_hex(0x55ABDA), 0); /* 底部颜色 */ 

    app_obj_general.hidden_parent = main_ui.mian_inter.mian_imagebg_obx;

    /* 时间 */
    main_ui.mian_inter.mian_time_text = lv_label_create(main_ui.mian_box);
    lv_label_set_text(main_ui.mian_inter.mian_time_text," ");
    lv_obj_set_style_text_color(main_ui.mian_inter.mian_time_text,lv_color_hex(0xFFFFFF),LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(main_ui.mian_inter.mian_time_text,&Font48,LV_STATE_DEFAULT);
    lv_obj_align_to(main_ui.mian_inter.mian_time_text,main_ui.mian_inter.mian_imagebg_obx,LV_ALIGN_TOP_LEFT,57,30);

	/* 定义应用信息数组 */
	const app_info_t app_info[] = {
		{0, "计算器", &calculator},
		{1, "画笔", &brush},
		{2, "日历",  &app_calendar},
		{3, "相册",  &Photo},
		{4, "视频", &lv_video},
		{5, "文件", &l_file},
		{6, "wifi", &l_wifi},
		{7, "usb", &usb},
		{8, "游戏", &game},
	};

	/* 定义图标资源数组 */
	const lv_img_dsc_t* icon_resources[main_ic0_num] = {
		&calculator, &brush, &app_calendar, &Photo, &lv_video, &l_file, &l_wifi, &usb, &game
	};

	for (int row = 0; row < rows; row++) 
	{
		for (int col = 0; col < cols; col++) 
		{
			/* 计算坐标 */
			int x = margin_x + col * (icon_size + margin_x);
			int y = start_y + margin_y + row * (icon_size + margin_y);
			
			/* 创建图标按钮 */
			main_ui.mian_inter.ico[ico_num] = lv_imgbtn_create(main_ui.mian_inter.mian_imagebg_obx);
			
			/* 设置图标资源 */
			lv_imgbtn_set_src(main_ui.mian_inter.ico[ico_num], LV_IMGBTN_STATE_RELEASED, NULL, icon_resources[ico_num], 
							NULL);
			
			lv_obj_set_size(main_ui.mian_inter.ico[ico_num], icon_size, icon_size);
			lv_obj_set_pos(main_ui.mian_inter.ico[ico_num], x, y);
			lv_obj_set_style_bg_opa(main_ui.mian_inter.ico[ico_num], LV_OPA_TRANSP, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_width(main_ui.mian_inter.ico[ico_num], 0, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_opa(main_ui.mian_inter.ico[ico_num], LV_OPA_TRANSP, LV_STATE_FOCUSED);
			lv_obj_set_style_border_width(main_ui.mian_inter.ico[ico_num], 0, LV_STATE_FOCUSED);
			lv_obj_set_style_shadow_width(main_ui.mian_inter.ico[ico_num], 0, LV_STATE_FOCUSED);
			lv_obj_set_style_bg_opa(main_ui.mian_inter.ico[ico_num], LV_OPA_TRANSP, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
			lv_obj_set_style_outline_width(main_ui.mian_inter.ico[ico_num], 0, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
			lv_obj_set_style_outline_opa(main_ui.mian_inter.ico[ico_num], LV_OPA_TRANSP, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
			lv_obj_set_style_border_width(main_ui.mian_inter.ico[ico_num], 0, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
			lv_obj_set_style_shadow_width(main_ui.mian_inter.ico[ico_num], 0, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
			lv_obj_add_event_cb(main_ui.mian_inter.ico[ico_num], lv_imgbtn_control_event_handler, LV_EVENT_ALL, NULL);
			
			/* 创建应用名称标签 */
			lv_obj_t *app_label = lv_label_create(main_ui.mian_inter.mian_imagebg_obx);
			lv_label_set_text(app_label, app_info[ico_num].app_text_en);
			lv_obj_set_style_text_color(app_label,lv_color_hex(0xFFFFFF),LV_STATE_DEFAULT);
			lv_obj_set_style_text_font(app_label, &Font11chn, 0);
			lv_obj_set_style_text_align(app_label, LV_TEXT_ALIGN_CENTER, 0);
			
			/* 将标签对齐到图标下方中间位置 */
			lv_obj_align_to(app_label, main_ui.mian_inter.ico[ico_num], LV_ALIGN_OUT_BOTTOM_MID, 0, 5); 
			
			/* 添加到控制组 */
			if (ctrl_g) 
			{
				lv_group_add_obj(ctrl_g, main_ui.mian_inter.ico[ico_num]);
			}
			
			ico_num++;
		}
	}

    if (indev_keypad != NULL)
    {
        lv_group_focus_obj(main_ui.mian_inter.ico[0]);  /* 聚焦第一个APP */
    }

    // main_ui.mini_box.wifi = lv_img_create(main_ui.mian_inter.main_mini_obx);
    // lv_img_set_src(main_ui.mini_box.wifi,&lv_wifi);
    // lv_obj_align(main_ui.mini_box.wifi,LV_ALIGN_LEFT_MID,-5,-2);
    // lv_obj_set_size(main_ui.mini_box.wifi, lv_wifi.header.w, lv_wifi.header.h);

    // main_ui.mini_box.tf = lv_img_create(main_ui.mian_inter.main_mini_obx);
    // lv_img_set_src(main_ui.mini_box.tf,&lv_tf);
    // lv_obj_align_to(main_ui.mini_box.tf,main_ui.mini_box.wifi,LV_ALIGN_OUT_RIGHT_MID,10,0);
    // lv_obj_set_size(main_ui.mini_box.tf, lv_tf.header.w, lv_tf.header.h);

    // main_ui.mini_box.usb = lv_img_create(main_ui.mian_inter.main_mini_obx);
    // lv_img_set_src(main_ui.mini_box.usb,&lv_usb);
    // lv_obj_align_to(main_ui.mini_box.usb,main_ui.mini_box.tf,LV_ALIGN_OUT_RIGHT_MID,10,0);
    // lv_obj_set_size(main_ui.mini_box.usb, lv_usb.header.w, lv_usb.header.h);

    // main_ui.mini_box.cell = lv_img_create(main_ui.mian_inter.main_mini_obx);
    // lv_img_set_src(main_ui.mini_box.cell,&cell);
    // lv_obj_align(main_ui.mini_box.cell,LV_ALIGN_RIGHT_MID,5,-2);
    // lv_obj_set_size(main_ui.mini_box.cell, cell.header.w, cell.header.h);
    // lv_obj_set_style_img_recolor(main_ui.mini_box.cell,lv_color_make(0,250,0),LV_STATE_DEFAULT);
    // lv_obj_set_style_img_recolor_opa(main_ui.mini_box.cell,LV_OPA_50,LV_STATE_DEFAULT);

    // main_ui.mini_box.vioce = lv_img_create(main_ui.mian_inter.main_mini_obx);
    // lv_img_set_src(main_ui.mini_box.vioce,&novoice);
    // lv_obj_align_to(main_ui.mini_box.vioce,main_ui.mini_box.cell,LV_ALIGN_OUT_LEFT_MID,-10,0);
    // lv_obj_set_size(main_ui.mini_box.vioce, novoice.header.w, novoice.header.h);

    /* 时间 */
    // main_ui.mini_box.time = lv_label_create(main_ui.mian_inter.main_mini_obx);
    // lv_label_set_text(main_ui.mini_box.time," ");
    // lv_obj_set_style_text_color(main_ui.mini_box.time,lv_color_hex(0xFFFFFF),LV_STATE_DEFAULT);
    // lv_obj_set_style_text_font(main_ui.mini_box.time,&lv_font_montserrat_14,LV_STATE_DEFAULT);
    // lv_obj_align(main_ui.mini_box.time,LV_ALIGN_CENTER,0,-2);

    main_ui.rtc.lv_rtc_timer = lv_timer_create(lv_rtc_timer, 1000, NULL);

    back_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(back_btn,50,50);
    lv_obj_set_style_bg_color(back_btn,lv_color_make(255,255,255),LV_STATE_DEFAULT);
    lv_obj_set_style_radius(back_btn,25,LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(back_btn,2,LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(back_btn,lv_color_make(34,177,76),LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(back_btn,LV_OPA_50,LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(back_btn,LV_OPA_50,LV_STATE_DEFAULT);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_move_foreground(back_btn);
    lv_obj_set_size(back_btn,50,50);
    lv_obj_add_event_cb(back_btn, lv_backbtn_control_event_handler, LV_EVENT_ALL, NULL);

    if (indev_keypad != NULL)
    {
        if (back_btn != NULL)
        {
            lv_group_remove_obj(back_btn);
        }
    }
}
