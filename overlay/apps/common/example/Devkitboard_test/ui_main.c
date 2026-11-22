#include "app_config.h"

#ifdef USE_DevKitBoard_TEST_DEMO

#include "device/device.h"//u8
#include "server/video_dec_server.h"//fopen
#include "system/includes.h"//GPIO
#include "lcd_drive.h"
#include "sys_common.h"
#include "yuv_soft_scalling.h"
#include "asm/jpeg_codec.h"
#include "system/timer.h"
#include "get_yuv_data.h"
#include "wifi/wifi_connect.h"

#include "ename.h"
#include "res_config.h"
#include "ui/ui.h"
#include "ui/ui_core.h"
#include "ui/ui_text.h"
#include "ui_api.h"
#include "ui/includes.h"
#include "font/font_all.h"
#include "font/font_textout.h"
#include "font/language_list.h"
#include "lcd_data_driver.h"
//#include "camera.h"

#define UI_MSG_VIDEO_EXIT  0x20

#define POST_TASK_DevKitBoard "DevKitBoard_task"
#ifdef CONFIG_MP3_DEC_ENABLE
extern void post_msg_play_flash_mp3(char *file_name, u8 dec_volume);
extern void post_msg_play_sd_mp3(char *file_name, u8 dec_volume);
extern void post_msg_stop_mp3(void);
#endif
static u8 ui_show_text_flog = 0;

static void ai_chat_show(void)
{
#ifdef CHAT_LAYOUT
    ui_show_main(PAGE_11);
#else
    ui_show_text_flog = 2;
    ui_show_main(PAGE_10);
#endif
}
/*==================开机播提示音已经开机动画==================*/
static void open_animation(void)//开机图片以及开机音乐播放
{
    set_lcd_show_data_mode(UI);

#ifdef CONFIG_MP3_DEC_ENABLE
    post_msg_play_flash_mp3("poweron.mp3", MP3_VOLUME); //开机提示音
#endif
    ui_show_main(PAGE_0);
    msleep(2000);
    ui_hide_main(PAGE_0);
}
/*************************END**********************************/

/*======================PAGE_1================================*/
static lise_show_text(void *priv)
{
    static char str1[] = "功能菜单";
    static char str2[] = "按键测试";
    static char str3[] = "DVP摄像头测试";
    static char str4[] = "SPI摄像头测试";
    static char str5[] = "MIC测试";
    static char str6[] = "音频输入测试(linein)";
    static char str7[] = "网络配置(连WiFi)";
    static char str8[] = "蓝牙播放测试";

    static char str9[] = "SD卡音乐播放";
    static char str10[] = "SD卡视频播放";
    static char str11[] = "AI聊天";
    static char str12[] = "wifi播歌测试";

    ui_text_set_textu_by_id(BASEFORM_5, str1, strlen(str1), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_14, str2, strlen(str2), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_15, str3, strlen(str3), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_16, str4, strlen(str4), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_17, str5, strlen(str5), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_18, str6, strlen(str6), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_19, str7, strlen(str7), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_20, str8, strlen(str8), FONT_DEFAULT);

    ui_text_set_textu_by_id(BASEFORM_54, str9, strlen(str9), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_55, str10, strlen(str10), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_56, str11, strlen(str11), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_57, str12, strlen(str12), FONT_DEFAULT);
}
/*AI聊天：等联网成功后自动进入聊天页*/
static u16 ai_chat_timer;
static volatile u8 ai_chat_pending;   // 1=正在等联网进聊天页
static void ai_chat_cancel_wait(void)
{
    ai_chat_pending = 0;
    if (ai_chat_timer) {
        sys_timer_del(ai_chat_timer);
        ai_chat_timer = 0;
    }
}

static void ai_chat_wait_net(void *priv)
{
    if (wifi_get_sta_connect_state() != WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        return;
    }
    if (!ai_chat_pending || ui_get_current_window_id() != PAGE_9) {
        ai_chat_cancel_wait();
        return;
    }
    printf(">>>>>> [UI] wifi ready, enter AI chat page");
    ui_hide_curr_main();
    ai_chat_show();
    ai_chat_cancel_wait();
}

/*page1 垂直列表初始化*/
static int list_onchange(void *ctr, enum element_change_event event, void *arg)
{
    struct ui_grid *grid = (struct ui_grid *)ctr;
    int row, col;
    switch (event) {
    case ON_CHANGE_INIT:
        row = 11;
        col = 1;
        ui_grid_init_dynamic(grid, &row, &col);//初始化垂直列表的个数用于判断触摸行号使用
        ui_grid_set_slide_direction(grid, SCROLL_DIRECTION_UD);//限制控件滑动方向
        sys_timeout_add(NULL, lise_show_text, 50);

        break;
    }

    return false;
}
/*page1 触摸事件响应*/
static int list_ontouch(void *ctr, struct element_touch_event *e)
{
    int indx;
    int hi_indx;
    static u8 list_touch_index;
    static u8 move_flog;
    struct ui_grid *grid = (struct ui_grid *)ctr;
    struct element *elm = NULL;
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        move_flog = 0;
        indx = ui_grid_cur_item_dynamic(grid);
        list_touch_index = indx;
        /*printf(">>>>>>>>>ELM_EVENT_TOUCH_DOWN_index = %d", list_touch_index);*/
        break;
    case ELM_EVENT_TOUCH_UP:
        indx = ui_grid_cur_item_dynamic(grid);
        if (!move_flog && list_touch_index == indx) { //如果没有移动并且按下和抬起都是同一个地方表示按键按下
            hi_indx = ui_grid_get_hindex(grid);//获取底层控制的高亮行号
            printf("<<<<<<<<<<touch hi_indx=%d", hi_indx);
            switch (hi_indx) { //取消系统高亮
            case 0:
                elm = ui_core_get_element_by_id(LIST_1);
                break;
            case 1:
                elm = ui_core_get_element_by_id(LIST_2);
                break;
            case 2:
                elm = ui_core_get_element_by_id(LIST_3);
                break;
            case 3:
                elm = ui_core_get_element_by_id(LIST_4);
                break;
            case 4:
                elm = ui_core_get_element_by_id(LIST_5);
                break;
            case 5:
                elm = ui_core_get_element_by_id(LIST_6);
                break;
            case 6:
                elm = ui_core_get_element_by_id(LIST_7);
                break;

            case 7:
                elm = ui_core_get_element_by_id(LIST_8);
                break;
            case 8:
                elm = ui_core_get_element_by_id(LIST_9);
                break;
            case 9:
                elm = ui_core_get_element_by_id(LIST_10);
                break;
            case 10:
                elm = ui_core_get_element_by_id(LIST_11);
                break;
            }
            if (!elm) {
                printf(">>>>>> touch: no element for hi_indx=%d", hi_indx);
                break;
            }
            ui_core_highlight_element(elm, 0);//改变控件高亮状态
            /*printf("<<<<<<<<<<touch ok indx=%d", list_touch_index);	*/
            switch (list_touch_index) { //高亮选择
            case 0:
                elm = ui_core_get_element_by_id(LIST_1);
                break;
            case 1:
                elm = ui_core_get_element_by_id(LIST_2);
                break;
            case 2:
                elm = ui_core_get_element_by_id(LIST_3);
                break;
            case 3:
                elm = ui_core_get_element_by_id(LIST_4);
                break;
            case 4:
                elm = ui_core_get_element_by_id(LIST_5);
                break;
            case 5:
                elm = ui_core_get_element_by_id(LIST_6);
                break;
            case 6:
                elm = ui_core_get_element_by_id(LIST_7);
                break;

            case 7:
                elm = ui_core_get_element_by_id(LIST_8);
                break;
            case 8:
                elm = ui_core_get_element_by_id(LIST_9);
                break;
            case 9:
                elm = ui_core_get_element_by_id(LIST_10);
                break;
            case 10:
                elm = ui_core_get_element_by_id(LIST_11);
                break;
            }
            if (!elm) {
                printf(">>>>>> touch: no element for index=%d", list_touch_index);
                break;
            }
            ui_core_highlight_element(elm, 1);
            ui_core_redraw(elm->parent);
            os_time_dly(10);
            ui_hide_curr_main();
            switch (list_touch_index) { //根据选择切界面
            case 0://按键测试
                ui_show_main(PAGE_2);
                break;
            case 1://DVP测试
                ui_show_main(PAGE_3);
                break;
            case 2://spi测试
                ui_show_main(PAGE_10);
                ui_show_text_flog = 1;
                break;
            case 3://MIC测试
                ui_show_main(PAGE_4);
                break;
            case 4://linein测试
                ui_show_main(PAGE_5);
                break;
            case 5://wifi播歌测试
                ui_show_main(PAGE_9); //进入wifi信息显示
                break;
            case 6://wifi播歌测试：先看配网信息，再播放网络音频
                ui_show_main(PAGE_9);
                {
                    extern void wifi_music_start(void);
                    wifi_music_start();
                }
                break;
            case 7://蓝牙播歌测试
                ui_show_main(PAGE_10);
                ui_show_text_flog = 0;
                break;
            case 8://SD卡音乐播放
                ui_show_main(PAGE_8);
                #ifdef CONFIG_MP3_DEC_ENABLE
                post_msg_play_sd_mp3("aibuhuijueji.mp3", MP3_VOLUME);
                #endif
                break;

            case 9://SD卡视频播放
                set_lcd_show_data_mode(CAMERA);
                extern char avi_replay_start(char *path);
                avi_replay_start("storage/sd0/C/yequ.avi");
                break;

            case 10://AI聊天：没联网先去配网，联网了才进聊天页
                if (wifi_get_sta_connect_state() == WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
                    ai_chat_show();
                } else {
                    printf(">>>>>> AI chat: wifi not ready, go to network config first");
                    ai_chat_pending = 1;
                    if (!ai_chat_timer) {
                        ai_chat_timer = sys_timer_add(NULL, ai_chat_wait_net, 1000);
                    }
                    ui_show_text_flog = 0;
                    ui_show_main(PAGE_9);
                }
                break;

        }
        }
        /*printf(">>>>>>>>>ELM_EVENT_TOUCH_UP_index = %d", list_touch_index);*/
        break;
    case ELM_EVENT_TOUCH_MOVE:
        if (!move_flog) {
            printf("<<<<<<<<<<<<<<<<<ELM_EVENT_TOUCH_MOVE");
        }
        move_flog = 1;
        break;
    case ELM_EVENT_TOUCH_R_MOVE:
        break;
    }
    return false;//不接管消息
}
REGISTER_UI_EVENT_HANDLER(LIST)
.onchange = list_onchange,
 .ontouch = list_ontouch,
};
/***********************PAGE_1_END**************************/


/*======================PAGE_2===按键测试==========================*/
static key_show_text(void *priv)
{
    static char str1[] = "返回主界面";
    static char str2[] = "按键测试-请按按键";
    ui_text_set_textu_by_id(BASEFORM_24, str1, strlen(str1), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_7, str2, strlen(str2), FONT_DEFAULT);
}
/*page2 返回按钮初始化*/
static int key_test_onchange(void *ctr, enum element_change_event event, void *arg)
{
    switch (event) {
    case ON_CHANGE_INIT:
        sys_timeout_add(NULL, key_show_text, 50);
        break;
    }
    return false;
}
/*page2 触摸事件响应*/
static int key_test_ontouch(void *ctr, struct element_touch_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctr;
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        ui_hide_curr_main();
        ui_show_main(PAGE_1);
        break;
    }
    return false;
}

REGISTER_UI_EVENT_HANDLER(BASEFORM_24)//返回按钮
.onchange = key_test_onchange,
 .ontouch = key_test_ontouch,
};
/***********************PAGE_2_END*按键测试*************************/

/*======================PAGE_3===DVP摄像头出图测试=================*/
/*page3 初始化*/
extern void set_touch_disable(void);//关闭触摸等摄像头出图再使能触摸
extern void get_yuv(u8 *yuv_buf, u32 len, int yuv_in_w, int yuv_in_h);//YUV数据回调线程
static int dvp_test_onchange(void *ctr, enum element_change_event event, void *arg)
{
    switch (event) {
    case ON_CHANGE_INIT:
        set_touch_disable();
        //get_yuv_init(get_yuv);
        set_lcd_show_data_mode(UI_CAMERA);//图像合成模式
        set_touch_enable();
        break;
    }
    return false;
}
/*page3 触摸事件响应*/
static int dvp_test_ontouch(void *ctr, struct element_touch_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctr;
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        ui_hide_curr_main();
        ui_show_main(PAGE_1);
        set_touch_disable();
        printf(">>>>>>>close_dvp_camere");
        void *video_dev = NULL;
        video_dev = dev_open("video0.0", NULL);
        //dev_ioctl(video_dev, CAMERA_POWER_CTRL, (u8)0); //摄像头硬件复位
        //get_yuv_uninit();
        set_lcd_show_data_mode(UI);//ui模式
        os_time_dly(10);//需要等彻底不使用lbuf才能清除lbuf
        extern void clear_te_lbuf(void);//防止下次摄像头出图申请不到lbuf
        clear_te_lbuf();
        set_touch_enable();
        break;
    }
    return false;
}

REGISTER_UI_EVENT_HANDLER(BASEFORM_8)//返回按钮
.onchange = dvp_test_onchange,
 .ontouch = dvp_test_ontouch,
};

/***********************PAGE_3_END*DVP摄像头出图测试****************/

/*======================PAGE_4===mic测试================*/
/*page4 初始化*/
static int mic_test_onchange(void *ctr, enum element_change_event event, void *arg)
{
    switch (event) {
    case ON_CHANGE_INIT:
        os_taskq_post(POST_TASK_DevKitBoard, 1, ACTION_MSG_MIC_OPEN);
        break;
    }
    return false;
}
/*page4 触摸事件响应*/
static int mic_test_ontouch(void *ctr, struct element_touch_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctr;
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        os_taskq_post(POST_TASK_DevKitBoard, 1, ACTION_MSG_MIC_CLOSE);
        ui_hide_curr_main();
        ui_show_main(PAGE_1);
        break;
    }
    return false;
}

REGISTER_UI_EVENT_HANDLER(BASEFORM_11)//返回按钮
.onchange = mic_test_onchange,
 .ontouch = mic_test_ontouch,
};
/***********************PAGE_4_END*mic测试****************/

/*======================PAGE_5===linein测试===============*/
/*page5 初始化*/
static int linein_test_onchange(void *ctr, enum element_change_event event, void *arg)
{
    switch (event) {
    case ON_CHANGE_INIT:
        os_taskq_post(POST_TASK_DevKitBoard, 1, ACTION_MSG_LINEIN_OPEN);
        break;
    }
    return false;
}
/*page5 触摸事件响应*/
static int linein_test_ontouch(void *ctr, struct element_touch_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctr;
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        os_taskq_post(POST_TASK_DevKitBoard, 1, ACTION_MSG_LINEIN_CLOSE);
        ui_hide_curr_main();
        ui_show_main(PAGE_1);
        break;
    }
    return false;
}
REGISTER_UI_EVENT_HANDLER(BASEFORM_23)//返回按钮
.onchange = linein_test_onchange,
 .ontouch = linein_test_ontouch,
};
/***********************PAGE_5_END*linein测试****************/

/*======================PAGE_9===wifi连接信息显示=================*/
static wifi_list_text(void *priv)
{
    struct wifi_mode_info info;
    static char str1[] = "请使用手机开热点配置为以下信息连接设备会自动连接";
    static char wifi_ssid[64];
    static char wifi_pwd[64];
    struct wifi_store_info *show_info;
    show_info = get_cur_wifi_info();
    printf("<<<<<<<<<<wifi_ssid=%s, wifi_pwd=%s", show_info->ssid, show_info->pwd);

    sprintf((char *)wifi_ssid, "wifi名字: ");
    sprintf((char *)wifi_pwd, "wifi密码: ");

    strncat((char *)wifi_ssid, (const char *)show_info->ssid, sizeof(show_info->ssid));
    strncat((char *)wifi_pwd, (const char *)show_info->pwd, sizeof(show_info->pwd));

    ui_text_set_textu_by_id(BASEFORM_48, str1, strlen(str1), FONT_DEFAULT | FONT_SHOW_MULTI_LINE);

    ui_text_set_textu_by_id(BASEFORM_49, wifi_ssid, strlen(wifi_ssid), FONT_DEFAULT);
    ui_text_set_textu_by_id(BASEFORM_50, wifi_pwd, strlen(wifi_pwd), FONT_DEFAULT);

}
/*page9 初始化*/
static int wifi_get_ssid_onchange(void *ctr, enum element_change_event event, void *arg)
{
    switch (event) {
    case ON_CHANGE_INIT:
        os_taskq_post(POST_TASK_DevKitBoard, 1, ACTION_MSG_GET_WIFI_SSID);
        sys_timeout_add(NULL, wifi_list_text, 50);
        break;
    }
    return false;
}
/*page9 触摸事件响应*/
static int wifi_get_ssid_ontouch(void *ctr, struct element_touch_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctr;
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        ai_chat_cancel_wait();
        os_taskq_post(POST_TASK_DevKitBoard, 1, ACTION_MSG_SCAN_WIFI);
        ui_hide_curr_main();
        ui_show_main(PAGE_1);
        break;
    }
    return false;
}
REGISTER_UI_EVENT_HANDLER(BASEFORM_47)//返回按钮
.onchange = wifi_get_ssid_onchange,
 .ontouch = wifi_get_ssid_ontouch,
};

/***********************PAGE_9_END*连接wifi****************/

//网络播歌需要先扫描wifi连接有网络的wifi输入正确得的密码进行连接wifi 然后会自动播歌

/*======================PAGE_8===播歌界面=================*/
/*page9 触摸事件响应*/
extern void wifi_music_end(void);
static int music_ontouch(void *ctr, struct element_touch_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctr;

    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        ui_hide_curr_main();
        ui_show_main(PAGE_1);
#ifdef CONFIG_MP3_DEC_ENABLE
        post_msg_play_flash_mp3("NetDisc.mp3", MP3_VOLUME);
#endif
        wifi_music_end();
        break;
    }
    return false;
}
REGISTER_UI_EVENT_HANDLER(BASEFORM_43)//返回按钮
.ontouch = music_ontouch,
};

/***********************PAGE_8_END*播歌界面****************/

/*======================PAGE_10====================*/
const char *bt_get_local_name(void);

static void bt_name_text(void *priv)
{
    char *bt_name;
    static u8 cpy_flog = 0;
    static char str1[] = "连接蓝牙";
    static char str2[] = "SPI_camera";

    if (ui_show_text_flog) {
        extern int init_spi_video(void);
        printf("\n [msg] %s -[init_spi_video] %d\n", __FUNCTION__, __LINE__);
        //init_spi_video();
    } else {
        if (!cpy_flog) {
            cpy_flog = 1;
            bt_name = bt_get_local_name();
            printf(">>>>>>>>>bt_name=%s", bt_name);
            strncat((char *)str1, (const char *)bt_name, 64);
        }
        ui_text_set_textu_by_id(BASEFORM_53, str1, strlen(str1), FONT_DEFAULT | FONT_SHOW_MULTI_LINE);
    }
}
/*====================== AI 聊天显示（复用 PAGE_10 的文本控件） ====================*/
/*
 * 注意：杰理这套 UI 的文本控件不支持 '\n' 换行，只能按宽度自动折行。
 * 所以这里自己做"行对齐"：按半角单位统计已占宽度，每条消息结束时补空格填满整行，
 * 下一条消息自然就从新的一行开始。
 */
#ifndef CHAT_LAYOUT
void chat_ui_set_status(const char *status) {}
void chat_ui_begin_turn(void) {}
void chat_ui_append_delta(const char *text, size_t len) {}

#define CHAT_BUF_SIZE        300
#define CHAT_DRAW_MAX        250    /* 文本控件硬限制：一次最多 255 字节 */
#define CHAT_MSG_MAX         5
#define CHAT_MSG_LEN         160
#define CHAT_UNITS_PER_LINE  26     /* 一行约 13 个汉字（318px / 24px） */

static char chat_buf[CHAT_BUF_SIZE];
static u16 chat_len;
static char chat_msgs[CHAT_MSG_MAX][CHAT_MSG_LEN];
static u8 chat_msg_cnt;
static u8 chat_msg_head;
static volatile u8 chat_dirty;
static volatile u8 chat_page_on;
static u16 chat_timer_id;

/* 返回该字符占几个半角单位，并输出 UTF-8 字节数 */
static int chat_char_units(const char *s, int *bytes)
{
    unsigned char c = (unsigned char)s[0];

    if (c < 0x80) {
        *bytes = 1;
        return 1;                       /* ASCII 半角 */
    }
    if ((c & 0xE0) == 0xC0) {
        *bytes = 2;
    } else if ((c & 0xF0) == 0xE0) {
        *bytes = 3;
    } else if ((c & 0xF8) == 0xF0) {
        *bytes = 4;
    } else {
        *bytes = 1;
    }
    return 2;                           /* 汉字/全角 占两个半角宽度 */
}

static void chat_rebuild(void)
{
    int pos;
    int i;
    int units;
    int sum = 0;
    int first;

    chat_len = 0;
    chat_buf[0] = 0;

    if (!chat_msg_cnt) {
        chat_dirty = 1;
        return;
    }

    /* 控件最多只吃 255 字节：至少显示最新一条（太长就截断），还能多放几条就往前加 */
    first = chat_msg_cnt - 1;
    for (i = chat_msg_cnt - 1; i >= 0; i--) {
        int idx = (chat_msg_head + CHAT_MSG_MAX - chat_msg_cnt + i) % CHAT_MSG_MAX;
        int mlen = strlen(chat_msgs[idx]);
        if (sum + mlen + CHAT_UNITS_PER_LINE + 2 > CHAT_DRAW_MAX) {
            break;
        }
        sum += mlen + CHAT_UNITS_PER_LINE + 2;
        first = i;
    }

    for (i = first; i < chat_msg_cnt; i++) {
        int idx = (chat_msg_head + CHAT_MSG_MAX - chat_msg_cnt + i) % CHAT_MSG_MAX;
        const char *p = chat_msgs[idx];

        units = 0;
        while (*p) {
            int nb;
            int u = chat_char_units(p, &nb);
            if (chat_len + nb >= CHAT_DRAW_MAX) {
                break;
            }
            memcpy(chat_buf + chat_len, p, nb);
            chat_len += nb;
            units += u;
            p += nb;
        }
        /* 补空格把当前行填满，保证下一条消息从新的一行开始 */
        pos = CHAT_UNITS_PER_LINE - (units % CHAT_UNITS_PER_LINE);
        if (pos == CHAT_UNITS_PER_LINE) {
            pos = 0;
        }
        while (pos-- > 0 && chat_len < CHAT_DRAW_MAX) {
            chat_buf[chat_len++] = ' ';
        }
    }
    chat_buf[chat_len] = 0;
    chat_dirty = 1;
}

// 供语音模块调用：chat_ui_append("我", text) / chat_ui_append("AI", text)
void chat_ui_append(const char *who, const char *text)
{
    if (!who || !text || !text[0]) {
        return;
    }
    snprintf(chat_msgs[chat_msg_head], CHAT_MSG_LEN, "%s：%s", who, text);
    chat_msg_head = (chat_msg_head + 1) % CHAT_MSG_MAX;
    if (chat_msg_cnt < CHAT_MSG_MAX) {
        chat_msg_cnt++;
    }
    chat_rebuild();
}

static volatile u8 chat_drawing;
static void chat_refresh(void *priv)
{
    if (!chat_page_on || !chat_dirty || chat_drawing) {
        return;
    }
    if (ui_get_current_window_id() != PAGE_10) {
        // 不在聊天页就先不画，留着脏标记，回来再画
        return;
    }
    chat_drawing = 1;
    chat_dirty = 0;
    // 一次最多画 400 字节，控制 UI 任务的排版栈用量
    int len = chat_len;
    if (len > 250) {
        len = 250;          /* ui_platform 里 ASSERT(len <= 255) */
    }
    printf(">>>>>> [UI] chat draw len=%d win=0x%x", len, ui_get_current_window_id());
    ui_text_set_textu_by_id(BASEFORM_53, chat_buf, len, FONT_DEFAULT | FONT_SHOW_MULTI_LINE);
    // 只设文本不会自动重绘，必须手动刷新这一块，否则屏幕上看不到
    struct element *elm = ui_core_get_element_by_id(BASEFORM_53);
    if (elm) {
        ui_core_redraw(elm->parent ? elm->parent : elm);
    }
    chat_drawing = 0;
}
/*==================================================================================*/

#endif

#ifdef CHAT_LAYOUT

#define CHAT_TEXT_LIMIT 250
#define CHAT_LINE_UNITS 24
#define CHAT_ANSWER_LINES 5

static OS_MUTEX chat_lock;
static u8 chat_lock_ready;
static char chat_question[512];
static char chat_answer[2048];
static unsigned int chat_page;
static u16 chat_timer;
static volatile u8 chat_visible;
static volatile u8 chat_dirty;
static u8 chat_rendering;
static const char *volatile chat_status = "正在连接";
static const char *chat_drawn_status;

void chat_ui_set_status(const char *status)
{
    chat_status = status;
}

static void chat_copy_text(char *dst, size_t capacity, const char *text, size_t len)
{
    static const struct {
        const char *utf8;
        char glyph;
    } punctuation[] = {
        {"，", ','}, {"。", '.'}, {"、", ','}, {"？", '?'}, {"！", '!'},
        {"：", ':'}, {"；", ';'}, {"（", '('}, {"）", ')'}, {"“", '"'},
        {"”", '"'}, {"‘", '\''}, {"’", '\''}, {"—", '-'},
    };
    size_t out = 0;
    for (size_t i = 0; i < len && out + 1 < capacity;) {
        const unsigned char c = text[i];
        size_t width = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (i + width > len) {
            break;
        }
        char glyph = c < ' ' ? ' ' : 0;
        if (width == 3) {
            for (size_t j = 0; j < ARRAY_SIZE(punctuation); ++j) {
                if (!memcmp(text + i, punctuation[j].utf8, width)) {
                    glyph = punctuation[j].glyph;
                    break;
                }
            }
        }
        if (glyph) {
            dst[out++] = glyph;
        } else {
            if (out + width >= capacity) {
                break;
            }
            memcpy(dst + out, text + i, width);
            out += width;
        }
        i += width;
    }
    dst[out] = '\0';
}

void chat_ui_begin_turn(void)
{
    if (!chat_lock_ready) {
        return;
    }
    os_mutex_pend(&chat_lock, 0);
    chat_question[0] = '\0';
    chat_answer[0] = '\0';
    chat_page = 0;
    chat_dirty = 1;
    os_mutex_post(&chat_lock);
}

void chat_ui_append_delta(const char *text, size_t len)
{
    if (!chat_lock_ready || !text || !len) {
        return;
    }
    os_mutex_pend(&chat_lock, 0);
    size_t used = strlen(chat_answer);
    chat_copy_text(chat_answer + used, sizeof(chat_answer) - used, text, len);
    chat_dirty = 1;
    os_mutex_post(&chat_lock);
}

void chat_ui_append(const char *who, const char *text)
{
    if (!chat_lock_ready || !text || !text[0]) {
        return;
    }
    os_mutex_pend(&chat_lock, 0);
    if (!strcmp(who, "AI")) {
        chat_copy_text(chat_answer, sizeof(chat_answer), text, strlen(text));
    } else {
        chat_copy_text(chat_question, sizeof(chat_question), text, strlen(text));
    }
    chat_dirty = 1;
    os_mutex_post(&chat_lock);
}

static const char *chat_page_end(const char *start, unsigned int max_lines)
{
    const char *p = start;
    unsigned int units = 0;
    unsigned int lines = 1;
    while (*p) {
        const unsigned char c = *p;
        const unsigned int width = c < 0x80 ? 1 : 2;
        unsigned int bytes = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        for (unsigned int i = 1; i < bytes; ++i) {
            if (((unsigned char)p[i] & 0xc0) != 0x80) {
                bytes = i;
                break;
            }
        }
        if (p - start + bytes > CHAT_TEXT_LIMIT) {
            break;
        }
        if (units + width > CHAT_LINE_UNITS) {
            if (lines == max_lines) {
                break;
            }
            ++lines;
            units = 0;
        }
        units += width;
        p += bytes;
    }
    return p;
}

static void chat_set_text(u32 id, const char *text, unsigned int flags)
{
    ui_text_set_textu_by_id(id, text, strlen(text), FONT_DEFAULT | flags);
    if (flags & FONT_SHOW_MULTI_LINE) {
        struct element *elm = ui_core_get_element_by_id(id);
        if (elm) {
            struct ui_text *control = (struct ui_text *)elm;
            control->attrs.y_offset = UI_TEXT_ALIGN_TOP;
        }
    }
}

static void chat_refresh(void *arg)
{
    const char *status = chat_status;
    if (wifi_get_sta_connect_state() != WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        status = "等待联网";
    }
    if (!chat_visible || chat_rendering || ui_get_current_window_id() != PAGE_11 ||
        (!chat_dirty && chat_drawn_status == status)) {
        return;
    }
    chat_rendering = 1;
    static char user_text[CHAT_TEXT_LIMIT + 1];
    static char answer_text[CHAT_TEXT_LIMIT + 1];
    static char page_text[16];
    unsigned int pages = 0;
    unsigned int page = 0;

    os_mutex_pend(&chat_lock, 0);
    const char *user_end = chat_page_end(chat_question, 2);
    memcpy(user_text, chat_question, user_end - chat_question);
    user_text[user_end - chat_question] = '\0';
    const char *p = chat_answer;
    const char *start = p;
    const char *end = p;
    while (*p) {
        const char *next = chat_page_end(p, CHAT_ANSWER_LINES);
        if (pages <= chat_page) {
            start = p;
            end = next;
            page = pages;
        }
        ++pages;
        p = next;
    }
    chat_page = page;
    memcpy(answer_text, start, end - start);
    answer_text[end - start] = '\0';
    chat_dirty = 0;
    os_mutex_post(&chat_lock);

    snprintf(page_text, sizeof(page_text), "%u / %u", page + 1, pages ? pages : 1);
    chat_set_text(CHAT_RETURN, "返回", 0);
    chat_set_text(CHAT_TITLE, "语音助手", 0);
    chat_set_text(CHAT_STATUS, status, 0);
    chat_set_text(CHAT_USER_LABEL, "你说", 0);
    chat_set_text(CHAT_USER_TEXT, user_text[0] ? user_text : "请对着麦克风说话", FONT_SHOW_MULTI_LINE);
    chat_set_text(CHAT_AI_LABEL, "AI 回复", 0);
    chat_set_text(CHAT_AI_TEXT, answer_text[0] ? answer_text : "回复会显示在这里", FONT_SHOW_MULTI_LINE);
    chat_set_text(CHAT_PREV, "上一页", 0);
    chat_set_text(CHAT_COUNT, page_text, 0);
    chat_set_text(CHAT_NEXT, "下一页", 0);
    chat_drawn_status = status;
    struct element *elm = ui_core_get_element_by_id(CHAT_LAYOUT);
    if (elm && chat_visible && ui_get_current_window_id() == PAGE_11) {
        ui_core_redraw(elm);
    }
    chat_rendering = 0;
}

static int chat_onchange(void *ctr, enum element_change_event event, void *arg)
{
    if (event == ON_CHANGE_INIT && chat_lock_ready) {
        chat_visible = 1;
        chat_dirty = 1;
        if (!chat_timer) {
            chat_timer = sys_timer_add(NULL, chat_refresh, 120);
        }
    } else if (event == ON_CHANGE_RELEASE) {
        chat_visible = 0;
        if (chat_timer) {
            sys_timer_del(chat_timer);
            chat_timer = 0;
        }
    }
    return false;
}

static int chat_touch_back(void *ctr, struct element_touch_event *event)
{
    if (event->event == ELM_EVENT_TOUCH_UP) {
        ui_hide_curr_main();
        ui_show_main(PAGE_1);
        return true;
    }
    return event->event == ELM_EVENT_TOUCH_DOWN;
}

static int chat_touch_prev(void *ctr, struct element_touch_event *event)
{
    if (event->event == ELM_EVENT_TOUCH_UP && chat_lock_ready) {
        os_mutex_pend(&chat_lock, 0);
        if (chat_page) {
            --chat_page;
            chat_dirty = 1;
        }
        os_mutex_post(&chat_lock);
        return true;
    }
    return event->event == ELM_EVENT_TOUCH_DOWN;
}

static int chat_touch_next(void *ctr, struct element_touch_event *event)
{
    if (event->event == ELM_EVENT_TOUCH_UP && chat_lock_ready) {
        os_mutex_pend(&chat_lock, 0);
        const char *next = chat_answer;
        for (unsigned int i = 0; i <= chat_page && *next; ++i) {
            next = chat_page_end(next, CHAT_ANSWER_LINES);
        }
        if (*next) {
            ++chat_page;
            chat_dirty = 1;
        }
        os_mutex_post(&chat_lock);
        return true;
    }
    return event->event == ELM_EVENT_TOUCH_DOWN;
}

REGISTER_UI_EVENT_HANDLER(CHAT_LAYOUT)
.onchange = chat_onchange,
.ontouch = NULL,
};
REGISTER_UI_EVENT_HANDLER(CHAT_RETURN)
.onchange = NULL,
.ontouch = chat_touch_back,
};
REGISTER_UI_EVENT_HANDLER(CHAT_PREV)
.onchange = NULL,
.ontouch = chat_touch_prev,
};
REGISTER_UI_EVENT_HANDLER(CHAT_NEXT)
.onchange = NULL,
.ontouch = chat_touch_next,
};

#endif

/*page10 初始化*/
static int bt_music_onchange(void *ctr, enum element_change_event event, void *arg)
{
    switch (event) {
    case ON_CHANGE_INIT:
#ifndef CHAT_LAYOUT
        if (ui_show_text_flog == 2) {
            // AI 聊天页：ON_CHANGE_INIT 时页面还没建好，这里只准备数据，等 SHOW/定时器再画
            chat_page_on = 1;
            if (chat_len == 0) {
                chat_ui_append("提示", "对着板子说话，回复会显示在这里，点屏幕返回");
            }
            chat_dirty = 1;
            if (!chat_timer_id) {
                chat_timer_id = sys_timer_add(NULL, chat_refresh, 500);
                printf(">>>>>> [UI] chat timer id=%d", chat_timer_id);
            }
        } else {
            sys_timeout_add(NULL, bt_name_text, 50);
        }
#else
        sys_timeout_add(NULL, bt_name_text, 50);
#endif
        break;
    case ON_CHANGE_FIRST_SHOW:
    case ON_CHANGE_SHOW:
        // 这里不画也不置脏：绘制会再次触发 SHOW，画/置脏都会形成死循环
        (void)0;
        break;
    case ON_CHANGE_RELEASE:
#ifndef CHAT_LAYOUT
        if (ui_show_text_flog == 2) {
            chat_page_on = 0;
            if (chat_timer_id) {
                sys_timer_del(chat_timer_id);
                chat_timer_id = 0;
            }
        }
#endif
        break;
    }
    return false;
}
/*page10 触摸事件响应*/
static int bt_music_ontouch(void *ctr, struct element_touch_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctr;
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        if (ui_show_text_flog) {
            extern int spi_video_task_kill(void);
            //spi_video_task_kill();
        }
        ui_hide_curr_main();
        ui_show_main(PAGE_1);
        break;
    }
    return false;
}
REGISTER_UI_EVENT_HANDLER(BASEFORM_53)//返回按钮
.onchange = bt_music_onchange,
 .ontouch = bt_music_ontouch,
};
/***********************PAGE_10_END*****************/

/*======================PAGE_====================*/

/***********************PAGE__END*****************/
/*======================测试线程接收消息处理UI==========================*/
static void ui_main_task(void *priv)
{
    int msg[32];
    int play_all_time;
    int play_time;
    int per;
    int time[32];
    static char str1[] = "网络连接成功";
    user_ui_lcd_init();
    open_animation(); //显示开机图片
    ui_show_main(PAGE_1);
    extern void wifi_music_start(void);
    while (1) {
        os_taskq_pend("taskq", msg, ARRAY_SIZE(msg)); //接收app_music.c中发来的消息 没有消息在这行等待
        printf(">>>>>>>>>>>>>>ui_main_task have msg=%d", msg[1]);
        switch (msg[1]) {

        case UI_MSG_VIDEO_EXIT:
            printf(">>>>>> UI Task: Safely switching LCD mode and Page to PAGE_1 <<<<<<\n");
            set_lcd_show_data_mode(UI);
            ui_show_main(PAGE_1);
            break;

        case UI_MSG_KEY_TEST://显示按键按中的图片
            if (ui_get_current_window_id() == PAGE_2) {
                ui_pic_show_image_by_id(BASEFORM_6, msg[2]);
            }
            break;
        case UI_MSG_WIFI_STA_CONNT_OK://sta连接成功
#ifdef CHAT_LAYOUT
            if (ai_chat_pending || ui_get_current_window_id() == PAGE_11) {
                break;
            }
#endif
#ifdef CONFIG_MP3_DEC_ENABLE
            post_msg_play_flash_mp3("NetMusic.mp3", MP3_VOLUME);
#endif
            ui_text_set_textu_by_id(BASEFORM_48, str1, strlen(str1), FONT_DEFAULT | FONT_SHOW_MULTI_LINE);
            if (ai_chat_pending) {
                // 正在等联网进 AI 聊天页，别把页面抢到播歌界面去
                printf(">>>>>> [UI] wifi connected, keep AI chat flow");
                break;
            }
            os_time_dly(150);
            ui_hide_curr_main();
            ui_show_main(PAGE_8);
            // wifi_music_start(); // Keep playback idle during the ASR test.
            break;
        case UI_MSG_PLEAY_ALL_TIME://获取播放总时间
            play_all_time = msg[2];
            sprintf((char *)time, "%d%d:%d%d", play_all_time / 60 / 10, play_all_time / 60 % 10, play_all_time % 60 / 10, play_all_time % 10);
            printf(">>>>>>>>all_time = %s", time);
            ui_text_set_textu_by_id(BASEFORM_45, time, strlen(time), FONT_DEFAULT);
            break;
        case UI_MSG_PLEAY_TIME://获取播放时间
            play_time = msg[2];
            sprintf((char *)time, "%d%d:%d%d", play_time / 60 / 10, play_time / 60 % 10, play_time % 60 / 10, play_time % 10);
            ui_text_set_textu_by_id(BASEFORM_44, time, strlen(time), FONT_DEFAULT);

            per = play_time * 100 / play_all_time;

            ui_slider_set_persent_by_id(NEWLAYOUT_21, per);
            if (play_time == (play_all_time - 1)) {
                os_time_dly(120);
                wifi_music_end();
            }
            break;
        case UI_MSG_BT_CONNT_OK://蓝牙连接成功

            break;
        case UI_MSG_BT_DISCONNECT://蓝牙断开连接

            break;
        }
    }
}

static int ui_main_task_init(void)
{
#ifdef CHAT_LAYOUT
    chat_lock_ready = os_mutex_create(&chat_lock) == OS_NO_ERR;
#endif
    puts("ui_main_task_init \n\n");
    return thread_fork("ui_main_task", 11, 1024, 32, 0, ui_main_task, NULL);
}
late_initcall(ui_main_task_init);

#endif

