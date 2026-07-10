/**
 ******************************************************************************
 * @file        xiaoxiaole.c
 * @version     V1.0
 * @brief       消消乐游戏 APP
 ******************************************************************************
 */

 #include "lvgl.h"
 #include <stdint.h>
 #include "stdlib.h"
 #include "xiaoxiaole.h"

 
 /* 静态全局变量声明 */
 static const unsigned int color_lib[7] = {red, green, blue, gblue, yellow, rblue, orange}; /* 颜色库 */
 static game_obj_type game_obj[8][8] = {0};          /* 8x8游戏对象数组 */
 static int score;                                    /* 游戏得分 */
 static lv_obj_t *screen1, *game_window, *refs_btn;  /* 屏幕和游戏窗口相关对象 */
 static lv_obj_t *bgmap, *next_btn, *pre_btn;        /* 背景地图和按钮 */
 static lv_obj_t *step_lable, *exit_btn, *coin;      /* 标签和退出按钮 */
 static lv_obj_t *score_lable;                       /* 分数标签 */
 static float screen_ratio;                          /* 屏幕比例系数 */
 static bool initialized = false;
 static bool game_closing = false;
 static bool game_busy = false;                    /* 游戏初始化标志位 */
 typedef enum {
     GAME_ANIM_NONE,
     GAME_ANIM_EXCHANGE,
     GAME_ANIM_FLASH,
     GAME_ANIM_COIN,
     GAME_ANIM_FALL
 } game_anim_phase_t;
 static game_anim_phase_t game_anim_phase = GAME_ANIM_NONE;
 static uint32_t game_anim_pending = 0;
 static uintptr_t game_anim_generation = 0;
 static bool game_anim_launching = false;
 static lv_point_t game_touch_start;
 static bool game_touch_active = false;
 extern lv_obj_t * back_btn;
 
 
 /* 函数前置声明 */
 static void game_init(void);
 static void exchange_obj(game_obj_type *obj1, game_obj_type *obj2);
 static void move_obj_cb(lv_event_t *e);
 static void x_move_cb(void *var, int32_t v);
 static void y_move_cb(void *var, int32_t v);
 static void coin_move_cb(void *var, int32_t v);
 static void exchange_done_cb(lv_anim_t *a);
 static void same_color_move_to_coin(void);
 static int same_color_check(void);
 static void obj_move_down(void);
 static void set_obj_userdata(void);
 static void move_deleted_cb(lv_anim_t *a);
 static bool map_is_full(void);
 static bool has_same_color(void);
 static void map_del_all(void);
 static void map_refs(lv_event_t *e);
 static void exit_game_cb(lv_event_t *e);
 static void clear_all_clickable(void);
 static void add_all_clickable(void);
 static void move_to_coin_end_cb(lv_anim_t *a);
 static void flash_end_cb(lv_anim_t *a);
 static void flash_cb(void *var, int32_t v);
 static void same_color_flash(void);
 static void game_anim_advance(game_anim_phase_t phase);

 static void game_anim_next_generation(void)
 {
     if(game_anim_generation >= (UINTPTR_MAX >> 8))
     {
         game_anim_generation = 1;
     }
     else
     {
         game_anim_generation++;
     }
 }

 static void game_anim_invalidate(void)
 {
     game_anim_next_generation();
     game_anim_phase = GAME_ANIM_NONE;
     game_anim_pending = 0;
     game_anim_launching = false;
     game_touch_active = false;
 }

 static void game_anim_begin(game_anim_phase_t phase)
 {
     game_anim_next_generation();
     game_anim_phase = phase;
     game_anim_pending = 0;
     game_anim_launching = true;
     game_busy = true;
     game_touch_active = false;
 }

 static bool game_anim_start_item(lv_anim_t *a, game_anim_phase_t phase, lv_anim_ready_cb_t ready_cb)
 {
     if(game_closing || game_anim_phase != phase)
     {
         return false;
     }

     uintptr_t token = (game_anim_generation << 8) | (uintptr_t)phase;
     lv_anim_set_user_data(a, (void *)token);
     lv_anim_set_ready_cb(a, ready_cb);
     game_anim_pending++;
     if(lv_anim_start(a) == NULL)
     {
         game_anim_pending--;
         return false;
     }

     return true;
 }

 static bool game_anim_is_current(lv_anim_t *a, game_anim_phase_t phase)
 {
     if(a == NULL || game_closing || game_anim_phase != phase)
     {
         return false;
     }

     uintptr_t token = (uintptr_t)lv_anim_get_user_data(a);
     uintptr_t expected = (game_anim_generation << 8) | (uintptr_t)phase;
     return token == expected;
 }

 static void game_anim_try_finish(void)
 {
     if(game_closing || game_anim_launching || game_anim_phase == GAME_ANIM_NONE || game_anim_pending != 0)
     {
         return;
     }

     game_anim_phase_t phase = game_anim_phase;
     game_anim_phase = GAME_ANIM_NONE;
     game_anim_advance(phase);
 }

 static void game_anim_end_launch(void)
 {
     game_anim_launching = false;
     game_anim_try_finish();
 }

 static void game_anim_complete_item(void)
 {
     if(game_anim_pending == 0)
     {
         return;
     }

     game_anim_pending--;
     game_anim_try_finish();
 }

 static void game_finish_turn(void)
 {
     if(game_closing)
     {
         return;
     }

     game_busy = false;
     game_touch_active = false;
     if(lv_obj_is_valid(refs_btn))
     {
         lv_obj_add_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
     }
     add_all_clickable();
 }

 static void game_anim_advance(game_anim_phase_t phase)
 {
     if(game_closing)
     {
         return;
     }

     switch(phase)
     {
         case GAME_ANIM_EXCHANGE:
             if(same_color_check())
             {
                 same_color_flash();
             }
             else
             {
                 game_finish_turn();
             }
             break;

         case GAME_ANIM_FLASH:
             same_color_move_to_coin();
             break;

         case GAME_ANIM_COIN:
             if(lv_obj_is_valid(score_lable))
             {
                 lv_label_set_text_fmt(score_lable, "SCORE:%d", score);
             }
             obj_move_down();
             break;

         case GAME_ANIM_FALL:
             if(!map_is_full())
             {
                 obj_move_down();
             }
             else
             {
                 set_obj_userdata();
                 if(same_color_check())
                 {
                     same_color_flash();
                 }
                 else
                 {
                     game_finish_turn();
                 }
             }
             break;

         default:
             game_finish_turn();
             break;
     }
 }
 
 /* 图片资源声明 */
 LV_IMG_DECLARE(xiaoxiaole_bg_img)
 LV_IMG_DECLARE(refs_btn_img)
 LV_IMG_DECLARE(coin_img)
 
 /**
  * @brief   消消乐游戏主入口函数
  * @note    初始化游戏界面和核心组件
  */
 void xiaoxiaole()
 {
	 screen_ratio = 1;
 
	 if(initialized)
	 {
		 lv_game_del();  // 关键点1：确保返回按钮置顶

		 return;
	 }
	 
	 /* 清除屏幕滚动标志 */
	 lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
 
	 /* 创建主屏幕 */
	 screen1 = lv_tileview_create(lv_scr_act());
	 lv_obj_set_style_bg_color(screen1, lv_color_hex(0x000000), LV_PART_MAIN);
	 lv_obj_clear_flag(screen1, LV_OBJ_FLAG_SCROLLABLE);
 
	 /* 创建背景地图 */
	 bgmap = lv_img_create(screen1);
	 lv_img_set_src(bgmap, &xiaoxiaole_bg_img);
	 lv_img_set_pivot(bgmap, 0, 0);
	 //lv_img_set_zoom(bgmap, 256 * screen_ratio * 1.2);
	 lv_obj_clear_flag(bgmap, LV_OBJ_FLAG_SCROLLABLE);
 
	 /* 创建游戏主窗口 */
	 game_window = lv_tileview_create(screen1);
	 lv_obj_set_style_bg_color(game_window, lv_color_hex(0x000000), LV_PART_MAIN);
	 lv_obj_set_style_bg_opa(game_window, LV_OPA_COVER, LV_PART_MAIN);
	 lv_obj_clear_flag(game_window, LV_OBJ_FLAG_SCROLLABLE);
	 lv_obj_set_style_outline_width(game_window, 6, LV_PART_MAIN);
	 lv_obj_set_style_outline_color(game_window, lv_color_hex(0xbb7700), LV_PART_MAIN);
	 lv_obj_center(game_window);
	 lv_obj_set_size(game_window, 280 * screen_ratio, 280 * screen_ratio);
	 
	 /* 创建刷新按钮 */
	 refs_btn = lv_img_create(screen1);
	 lv_img_set_src(refs_btn, &refs_btn_img);
	 lv_obj_set_align(refs_btn, LV_ALIGN_TOP_RIGHT);
	 lv_obj_add_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
	 lv_obj_add_event_cb(refs_btn, map_refs, LV_EVENT_CLICKED, 0);
 
	 /* 创建金币图标 */
	 coin = lv_img_create(screen1);
	 lv_img_set_src(coin, &coin_img);
	 
	 /* 初始化得分系统 */
	 score = 0;
	 score_lable = lv_label_create(screen1);
	 lv_label_set_text_fmt(score_lable,"SCORE:%d",score);
	 lv_obj_set_style_text_font(score_lable, &lv_font_montserrat_22, 0);
	 lv_obj_set_y(score_lable, 90);
	 lv_obj_align_to(score_lable, coin, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
	 lv_obj_set_style_text_color(score_lable, lv_color_hex(0x00aaff), LV_PART_MAIN);
	 
	 /* 初始化游戏对象 */
	 game_init();
 
	 /* 界面管理配置 */
	 lv_hidden_box();
	 lv_obj_move_foreground(back_btn);
	 app_obj_general.del_parent = screen1;
	 app_obj_general.APP_Function = lv_game_del;
	 app_obj_general.app_state = NOT_DEL_STATE;
 
	 initialized = true; // 标记已初始化
 }
 
 void lv_game_del(void)
 {
     game_closing = true;
     game_busy = true;
     game_anim_invalidate();

	 if(screen1 != NULL && lv_obj_is_valid(screen1))
	 {
		 lv_obj_del(screen1);
	 }

	 screen1 = NULL;
	 game_window = NULL;
	 bgmap = NULL;
	 refs_btn = NULL;
	 coin = NULL;
	 score_lable = NULL;

	 for(int j = 0; j < 8; j++)
	 {
		 for(int i = 0; i < 8; i++)
		 {
			 game_obj[j][i].obj = NULL;
			 game_obj[j][i].alive = 1;
		 }
	 }

	 initialized = false;
	 game_closing = false;
	 game_busy = false;
	 score = 0;

	 app_obj_general.APP_Function = NULL;
	 app_obj_general.del_parent = NULL;
	 app_obj_general.requires_sd = 0;

	 lv_display_box();
 }
 
 /**
  * @brief   游戏初始化
  * @note    创建8x8游戏对象矩阵并初始化属性
  */
 static void game_init(void)
 {
     int i, j;
     game_anim_invalidate();
     score = 0;
     if(lv_obj_is_valid(score_lable))
     {
         lv_label_set_text_fmt(score_lable, "SCORE:%d", score);
     }
     game_busy = true;
	 lv_obj_refr_size(game_window);
 
	 /* 删除旧对象（如果存在） */
	 for(j = 0; j < 8; j++) 
	 {
		 for(i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 lv_obj_del(game_obj[j][i].obj);
			 }
		 }
	 }
	 
	 /* 初始化8x8游戏对象矩阵 */
	 for(j = 0; j < 8; j++) 
	 {
		 for(i = 0; i < 8; i++) 
		 {
			 /* 初始化对象属性 */
			 game_obj[j][i].x = i;
			 game_obj[j][i].y = j;
			 game_obj[j][i].alive = 1;
			 game_obj[j][i].color_index = rand() % 7;
			 
			 /* 创建按钮对象 */
			 game_obj[j][i].obj = lv_btn_create(game_window);
			 lv_obj_set_pos(game_obj[j][i].obj, i * 35 * screen_ratio + 1, j * 35 * screen_ratio + 1);
			 lv_obj_set_size(game_obj[j][i].obj, 35 * screen_ratio - 2, 35 * screen_ratio - 2);
			 lv_obj_set_style_bg_color(game_obj[j][i].obj, lv_color_hex(color_lib[game_obj[j][i].color_index]), 0);
			 /* 设置用户数据和事件回调 */
			 game_obj[j][i].obj->user_data = &game_obj[j][i];
             lv_obj_add_event_cb(game_obj[j][i].obj, move_obj_cb, LV_EVENT_PRESSED, 0);
             lv_obj_add_event_cb(game_obj[j][i].obj, move_obj_cb, LV_EVENT_RELEASED, 0);
		 }
	 }
 
	 /* 检查初始棋盘有效性 */
	 if(map_is_full() && same_color_check()) 
	 {
		 same_color_flash();
		 lv_obj_clear_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
	 }
     else
     {
         game_finish_turn();
     }
 }
 
 /**
  * @brief       交换两个游戏对象
  * @param obj1  对象1指针
  * @param obj2  对象2指针
  */
 static void exchange_obj(game_obj_type *obj1, game_obj_type *obj2)
 {
     game_obj_type temp;

     if(game_closing || obj1 == NULL || obj2 == NULL ||
        !lv_obj_is_valid(obj1->obj) || !lv_obj_is_valid(obj2->obj))
     {
         return;
     }

     game_busy = true;
     game_touch_active = false;
     lv_obj_clear_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
     clear_all_clickable();

     /* 保存临时颜色信息 */
     temp.color_index = obj1->color_index;
     temp.obj = obj1->obj;
 
     /* 交换颜色索引 */
     obj1->color_index = obj2->color_index;
     obj2->color_index = temp.color_index;

     bool valid_exchange = has_same_color();
     int32_t obj1_x = obj1->x * 35 * screen_ratio + 1;
     int32_t obj1_y = obj1->y * 35 * screen_ratio + 1;
     int32_t obj2_x = obj2->x * 35 * screen_ratio + 1;
     int32_t obj2_y = obj2->y * 35 * screen_ratio + 1;
     game_anim_begin(GAME_ANIM_EXCHANGE);

     /* 创建X轴移动动画 */
     lv_anim_t a1;
     lv_anim_init(&a1);
     lv_anim_set_var(&a1, obj1->obj);
     lv_anim_set_exec_cb(&a1, x_move_cb);
     lv_anim_set_time(&a1, 200);
     if(!valid_exchange) { lv_anim_set_playback_time(&a1, 200); }
     lv_anim_set_values(&a1, obj1_x, obj2_x);
     lv_anim_set_path_cb(&a1, lv_anim_path_ease_out);
     if(!game_anim_start_item(&a1, GAME_ANIM_EXCHANGE, exchange_done_cb))
     {
         lv_obj_set_x(obj1->obj, valid_exchange ? obj2_x : obj1_x);
     }

     /* 创建Y轴移动动画 */
     lv_anim_t a2;
     lv_anim_init(&a2);
     lv_anim_set_var(&a2, obj1->obj);
     lv_anim_set_exec_cb(&a2, y_move_cb);
     lv_anim_set_time(&a2, 200);
     if(!valid_exchange) { lv_anim_set_playback_time(&a2, 200); }
     lv_anim_set_values(&a2, obj1_y, obj2_y);
     lv_anim_set_path_cb(&a2, lv_anim_path_ease_out);
     if(!game_anim_start_item(&a2, GAME_ANIM_EXCHANGE, exchange_done_cb))
     {
         lv_obj_set_y(obj1->obj, valid_exchange ? obj2_y : obj1_y);
     }

     /* 创建第二个对象X轴动画 */
     lv_anim_t a3;
     lv_anim_init(&a3);
     lv_anim_set_var(&a3, obj2->obj);
     lv_anim_set_exec_cb(&a3, x_move_cb);
     lv_anim_set_time(&a3, 200);
     if(!valid_exchange) { lv_anim_set_playback_time(&a3, 200); }
     lv_anim_set_values(&a3, obj2_x, obj1_x);
     lv_anim_set_path_cb(&a3, lv_anim_path_ease_out);
     if(!game_anim_start_item(&a3, GAME_ANIM_EXCHANGE, exchange_done_cb))
     {
         lv_obj_set_x(obj2->obj, valid_exchange ? obj1_x : obj2_x);
    }

    /* 创建第二个对象Y轴动画（带完成回调） */
     lv_anim_t a4;
     lv_anim_init(&a4);
     lv_anim_set_var(&a4, obj2->obj);
     lv_anim_set_exec_cb(&a4, y_move_cb);
     lv_anim_set_time(&a4, 200);
     if(!valid_exchange) { lv_anim_set_playback_time(&a4, 200); }
     lv_anim_set_values(&a4, obj2_y, obj1_y);
     lv_anim_set_path_cb(&a4, lv_anim_path_ease_out);
     if(!game_anim_start_item(&a4, GAME_ANIM_EXCHANGE, exchange_done_cb))
     {
         lv_obj_set_y(obj2->obj, valid_exchange ? obj1_y : obj2_y);
     }

     /* 根据匹配结果处理对象交换 */
     if(valid_exchange)
     {
         /* 有效交换：交换对象指针 */
         obj1->obj = obj2->obj;
		 obj2->obj = temp.obj;
		 obj1->obj->user_data = obj1;
		 obj2->obj->user_data = obj2;
	 } 
	 else 
	 {
		 /* 无效交换：恢复颜色 */
         obj2->color_index = obj1->color_index;
         obj1->color_index = temp.color_index;
     }

     game_anim_end_launch();
 }
 
 /**
  * @brief       对象移动事件回调
  * @param e     事件指针
  */
 static void move_obj_cb(lv_event_t *e)
 {
     lv_event_code_t code = lv_event_get_code(e);
     lv_obj_t *target = lv_event_get_target(e);

     if(game_closing || game_busy)
     {
         game_touch_active = false;
         return;
     }

     lv_indev_t *indev = lv_indev_get_act();
     if(indev == NULL)
     {
         game_touch_active = false;
         return;
     }

     if(code == LV_EVENT_PRESSED)
     {
         lv_indev_get_point(indev, &game_touch_start);
         game_touch_active = true;
         return;
     }

     if(code != LV_EVENT_RELEASED || !game_touch_active)
     {
         return;
     }

     game_touch_active = false;
     game_obj_type *stage_data = (game_obj_type *)lv_obj_get_user_data(target);
     if(stage_data == NULL)
     {
         return;
     }

     lv_point_t release_point;
     int movex, movey;
     direction_type_enum direction;

     lv_obj_move_foreground(target);
     lv_indev_get_point(indev, &release_point);

     /* 计算移动向量 */
     movex = release_point.x - game_touch_start.x;
     movey = release_point.y - game_touch_start.y;

     /* 过滤无效移动 */
     int abs_movex = movex < 0 ? -movex : movex;
     int abs_movey = movey < 0 ? -movey : movey;
     if(abs_movex < 10 && abs_movey < 10) return;

     if(abs_movex >= abs_movey)
     {
         direction = movex < 0 ? left : right;
     }
     else
     {
         direction = movey < 0 ? up : down;
     }

     /* 处理边界情况并执行交换 */
     switch(direction)
     {
         case up:
             if(stage_data->y != 0)
                 exchange_obj(stage_data, &game_obj[stage_data->y-1][stage_data->x]);
             break;
         case down:
             if(stage_data->y != 7)
                 exchange_obj(stage_data, &game_obj[stage_data->y+1][stage_data->x]);
             break;
         case left:
             if(stage_data->x != 0)
                 exchange_obj(stage_data, &game_obj[stage_data->y][stage_data->x-1]);
             break;
         case right:
             if(stage_data->x != 7)
                 exchange_obj(stage_data, &game_obj[stage_data->y][stage_data->x+1]);
             break;
     }
 }
 
 /**
  * @brief       X轴移动动画回调
  * @param var   动画变量
  * @param v     目标值
  */
 static void x_move_cb(void *var, int32_t v)
 {
	 lv_obj_t *xxx = (lv_obj_t *)var;
	 lv_obj_set_x(xxx, v);    
 }
 
 /**
  * @brief       Y轴移动动画回调
  * @param var   动画变量
  * @param v     目标值
  */
 static void y_move_cb(void *var, int32_t v)
 {
	 lv_obj_t *xxx = (lv_obj_t *)var;
	 lv_obj_set_y(xxx, v);    
 }

 static void coin_move_cb(void *var, int32_t v)
 {
     lv_obj_t *obj = (lv_obj_t *)var;
     if(!lv_obj_is_valid(obj) || !lv_obj_is_valid(game_window))
     {
         return;
     }

     game_obj_type *stage_data = (game_obj_type *)lv_obj_get_user_data(obj);
     if(stage_data == NULL)
     {
         return;
     }

     int32_t start_x = stage_data->x * 35 * screen_ratio + 1 + lv_obj_get_x(game_window);
     int32_t start_y = stage_data->y * 35 * screen_ratio + 1 + lv_obj_get_y(game_window);
     int32_t remaining = 1024 - v;
     if(remaining < 0)
     {
         remaining = 0;
     }
     else if(remaining > 1024)
     {
         remaining = 1024;
     }

     lv_obj_set_pos(obj, start_x * remaining / 1024, start_y * remaining / 1024);
 }
 
 /**
  * @brief       闪烁动画回调
  * @param var   动画变量
  * @param v     透明度值
  */
 static void flash_cb(void *var, int32_t v)
 {
	 lv_obj_t *xxx = (lv_obj_t *)var;
	 lv_obj_set_style_bg_opa(xxx, 255 * (v % 2), 0);    
 }
 
 /**
  * @brief       交换完成回调
  * @param a     动画指针
  */
 static void exchange_done_cb(lv_anim_t *a)
 {
     if(!game_anim_is_current(a, GAME_ANIM_EXCHANGE))
     {
         return;
     }

     game_anim_complete_item();
 }
 
 /**
  * @brief   检查是否存在可消除颜色
  * @retval  true:存在 false:不存在
  */
 static bool has_same_color(void)
 {
	 /* 垂直方向检查 */
	 for(int i = 0; i < 8; i++) 
	 {
		 for(int j = 0; j < 6; j++) 
		 {
			 if(game_obj[j][i].color_index == game_obj[j+1][i].color_index && game_obj[j][i].color_index == game_obj[j+2][i].color_index) 
			 {
				 return true;
			 }    
		 }
	 }
	 
	 /* 水平方向检查 */
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 6; i++) 
		 {
			 if(game_obj[j][i].color_index == game_obj[j][i+1].color_index && game_obj[j][i].color_index == game_obj[j][i+2].color_index) 
			 {
				 return true;
			 }            
		 }    
	 }    
	 return false;
 }
 
 /**
  * @brief       执行颜色消除检查
  * @retval      消除的匹配组数
  */
 static int same_color_check(void)
 {
	 int m = 0;
	 /* 垂直方向消除 */
	 for(int i = 0; i < 8; i++) 
	 {
		 for(int j = 0; j < 6; j++) 
		 {
			 if(game_obj[j][i].color_index == game_obj[j+1][i].color_index && game_obj[j][i].color_index == game_obj[j+2][i].color_index) 
			 {
				 game_obj[j][i].alive = 0;
				 game_obj[j+1][i].alive = 0;
				 game_obj[j+2][i].alive = 0;
				 m++;
			 }            
		 }    
	 }
	 
	 /* 水平方向消除 */
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 6; i++) 
		 {
			 if(game_obj[j][i].color_index == game_obj[j][i+1].color_index && game_obj[j][i].color_index == game_obj[j][i+2].color_index) 
			 {
				 game_obj[j][i].alive = 0;
				 game_obj[j][i+1].alive = 0;
				 game_obj[j][i+2].alive = 0;
				 m++;
			 }            
		 }    
	 }    
	 return m;
 }
 
 /**
  * @brief   将被消除对象移动到金币位置
  */
 static void same_color_move_to_coin(void)
 {
     game_anim_begin(GAME_ANIM_COIN);

     for(int j = 0; j < 8; j++)
     {
         for(int i = 0; i < 8; i++)
         {
             if(game_obj[j][i].alive == 0 && lv_obj_is_valid(game_obj[j][i].obj))
             {
                 /* 移动对象到屏幕层 */
                 lv_obj_set_parent(game_obj[j][i].obj, screen1);

                 lv_anim_t a;
                 lv_anim_init(&a);
                 lv_anim_set_var(&a, game_obj[j][i].obj);
                 lv_anim_set_exec_cb(&a, coin_move_cb);
                 lv_anim_set_time(&a, 600);
                 lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
                 lv_anim_set_values(&a, 0, 1024);
                 if(!game_anim_start_item(&a, GAME_ANIM_COIN, move_to_coin_end_cb))
                 {
                     lv_obj_set_pos(game_obj[j][i].obj, 0, 0);
                     lv_obj_del(game_obj[j][i].obj);
                     score += 10;
                 }
             }
         }
     }

     game_anim_end_launch();
 }
 
 /**
  * @brief   执行闪烁效果
  */
 static void same_color_flash(void)
 {
     game_anim_begin(GAME_ANIM_FLASH);
     if(lv_obj_is_valid(refs_btn))
     {
         lv_obj_clear_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
     }
     clear_all_clickable();

     for(int j = 0; j < 8; j++)
     {
         for(int i = 0; i < 8; i++)
         {
             if(game_obj[j][i].alive == 0 && lv_obj_is_valid(game_obj[j][i].obj))
             {
                 /* 创建闪烁动画 */
                 lv_anim_t a2;
				 lv_anim_init(&a2);
                 lv_anim_set_var(&a2, game_obj[j][i].obj);
                 lv_anim_set_exec_cb(&a2, flash_cb);
                 lv_anim_set_time(&a2, 600);
                 lv_anim_set_values(&a2, 1, 7);
                 if(!game_anim_start_item(&a2, GAME_ANIM_FLASH, flash_end_cb))
                 {
                     lv_obj_set_style_bg_opa(game_obj[j][i].obj, LV_OPA_COVER, 0);
                 }
             }
         }
     }

     game_anim_end_launch();
 }
 
 /**
  * @brief   对象下落逻辑
  */
 static void obj_move_down(void)
 {
     game_anim_begin(GAME_ANIM_FALL);

     for(int i = 0; i < 8; i++)
	 {
		 for(int j = 7; j > 0; j--) 
		 {
			 if(game_obj[j][i].alive == 0) 
			 {
				 /* 向下填充空位 */
				 for(int k = j; k > 0; k--) 
				 {
					 game_obj[k][i].alive = game_obj[k-1][i].alive;
					 game_obj[k][i].obj = game_obj[k-1][i].obj;
					 game_obj[k][i].color_index = game_obj[k-1][i].color_index;
					 
                     if(game_obj[k][i].alive && lv_obj_is_valid(game_obj[k][i].obj))
                     {
                         game_obj[k][i].obj->user_data = &game_obj[k][i];
						 /* 创建下落动画 */
						 lv_anim_t a1;
						 lv_anim_init(&a1);
                         lv_anim_set_var(&a1, game_obj[k][i].obj);
                         lv_anim_set_exec_cb(&a1, y_move_cb);
                         lv_anim_set_time(&a1, 150);
                         lv_anim_set_values(&a1, (k-1)*35*screen_ratio+1, k*35*screen_ratio+1);
                         if(!game_anim_start_item(&a1, GAME_ANIM_FALL, move_deleted_cb))
                         {
                             lv_obj_set_y(game_obj[k][i].obj, k*35*screen_ratio+1);
                         }
                     }
                     else if(game_obj[k][i].alive)
                     {
                         game_obj[k][i].alive = 0;
                     }
				 }
				 
				 /* 生成新对象 */
				 game_obj[0][i].x = i;
				 game_obj[0][i].y = 0;
				 game_obj[0][i].alive = 1;
				 game_obj[0][i].color_index = rand()%7;
				 game_obj[0][i].obj = lv_btn_create(game_window);
				 lv_obj_set_pos(game_obj[0][i].obj, i*35*screen_ratio+1, -1*35*screen_ratio+1);
				 lv_obj_set_size(game_obj[0][i].obj, 35*screen_ratio-2, 35*screen_ratio-2);
				 lv_obj_set_style_bg_color(game_obj[0][i].obj, lv_color_hex(color_lib[game_obj[0][i].color_index]), 0);
				 game_obj[0][i].obj->user_data = &game_obj[0][i];
                 lv_obj_add_event_cb(game_obj[0][i].obj, move_obj_cb, LV_EVENT_PRESSED, 0);
                 lv_obj_add_event_cb(game_obj[0][i].obj, move_obj_cb, LV_EVENT_RELEASED, 0);
				 
				 /* 新对象下落动画 */
				 lv_anim_t a;
				 lv_anim_init(&a);
                 lv_anim_set_var(&a, game_obj[0][i].obj);
                 lv_anim_set_exec_cb(&a, y_move_cb);
                 lv_anim_set_time(&a, 150);
                 lv_anim_set_values(&a, (-1)*35*screen_ratio+1, 0*35*screen_ratio+1);
                 if(!game_anim_start_item(&a, GAME_ANIM_FALL, move_deleted_cb))
                 {
                     lv_obj_set_y(game_obj[0][i].obj, 1);
                 }
                 break;
			 }
		 }
		 
		 /* 处理首行空位 */
		 if(game_obj[0][i].alive == 0) 
		 {
			 game_obj[0][i].x = i;
			 game_obj[0][i].y = 0;
			 game_obj[0][i].alive = 1;
			 game_obj[0][i].color_index = rand()%7;
			 game_obj[0][i].obj = lv_btn_create(game_window);
			 lv_obj_set_pos(game_obj[0][i].obj, i*35*screen_ratio+1, -1*35*screen_ratio+1);
			 lv_obj_set_size(game_obj[0][i].obj, 35*screen_ratio-2, 35*screen_ratio-2);
			 lv_obj_set_style_bg_color(game_obj[0][i].obj, lv_color_hex(color_lib[game_obj[0][i].color_index]), 0);
			 game_obj[0][i].obj->user_data = &game_obj[0][i];
             lv_obj_add_event_cb(game_obj[0][i].obj, move_obj_cb, LV_EVENT_PRESSED, 0);
             lv_obj_add_event_cb(game_obj[0][i].obj, move_obj_cb, LV_EVENT_RELEASED, 0);
			 
			 /* 新对象下落动画 */
			 lv_anim_t a2;
			 lv_anim_init(&a2);
             lv_anim_set_var(&a2, game_obj[0][i].obj);
             lv_anim_set_exec_cb(&a2, y_move_cb);
             lv_anim_set_time(&a2, 150);
             lv_anim_set_values(&a2, (-1)*35*screen_ratio+1, 0*35*screen_ratio+1);
             if(!game_anim_start_item(&a2, GAME_ANIM_FALL, move_deleted_cb))
             {
                 lv_obj_set_y(game_obj[0][i].obj, 1);
             }
         }
     }

     game_anim_end_launch();
 }    
 
 /**
  * @brief       移动完成回调
  * @param a     动画指针
  */
 static void move_deleted_cb(lv_anim_t *a)
 {
     if(!game_anim_is_current(a, GAME_ANIM_FALL))
     {
         return;
     }

     game_anim_complete_item();
 }
 
 /**
  * @brief   更新对象用户数据
  */
 static void set_obj_userdata(void)
 {    
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 game_obj[j][i].obj->user_data = &game_obj[j][i];
			 }
		 }
	 }    
 }
 
 /**
  * @brief   检查棋盘是否填满
  * @retval  true:已满 false:未满
  */
 static bool map_is_full(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(game_obj[j][i].alive == 0) return false;
		 }
	 }    
	 return true;
 }    
 
 /**
  * @brief   删除所有对象
  */
 static void map_del_all(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 lv_obj_del(game_obj[j][i].obj);
			 }
			 game_obj[j][i].obj = NULL;
			 game_obj[j][i].alive = 0;
		 }
	 }    
 }    
 
 /**
  * @brief       刷新地图事件回调
  * @param e     事件指针
  */
 static void map_refs(lv_event_t *e)
 {
	 if(game_busy || game_closing) return;
	 lv_obj_clear_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
	 map_del_all();
	 game_init();
 }    
 
 /**
  * @brief   禁用所有点击事件
  */
 static void clear_all_clickable(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 lv_obj_clear_flag(game_obj[j][i].obj, LV_OBJ_FLAG_CLICKABLE);
			 }
		 }
	 }                
 }
 
 /**
  * @brief   启用所有点击事件
  */
 static void add_all_clickable(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 lv_obj_add_flag(game_obj[j][i].obj, LV_OBJ_FLAG_CLICKABLE);
			 }
		 }
	 }                
 }
 
 /**
  * @brief       金币移动完成回调
  * @param a     动画指针
  */
 static void move_to_coin_end_cb(lv_anim_t *a)
 {
     if(!game_anim_is_current(a, GAME_ANIM_COIN))
     {
         return;
     }

     lv_obj_t *xxx = (lv_obj_t *)a->var;
     if(lv_obj_is_valid(xxx))
     {
         lv_obj_del(xxx);
     }
     score += 10;
     game_anim_complete_item();
 }
 
 /**
  * @brief       闪烁完成回调
  * @param a     动画指针
  */
 static void flash_end_cb(lv_anim_t *a)
 {
     if(!game_anim_is_current(a, GAME_ANIM_FLASH))
     {
         return;
     }

     game_anim_complete_item();
 }
 
 /**
  * @brief       退出游戏回调
  * @param e     事件指针
  */
 static void exit_game_cb(lv_event_t *e)
 {   
	 /* 只执行隐藏操作 */
	 lv_game_del(); 
 }
 
