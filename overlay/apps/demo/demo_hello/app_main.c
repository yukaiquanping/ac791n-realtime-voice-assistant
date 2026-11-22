#include "hello_realtime_voice.h"
#include "audio_input.h"
extern void hello_realtime_voice_start(void);
#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"

#include "device/gpio.h"
#include "asm/pwm.h"

#include "key/adkey.h"
#include "key/key_driver.h"
#include "event/key_event.h"

//Ļʾ
#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"

#include "ename.h"


//WiFi
#include "event/net_event.h"
#include "wifi/wifi_connect.h"
#include "net/config_network.h"

/*
 * Ļʾ
 */
#ifdef CONFIG_UI_ENABLE // UIʱŰLCDͷļextern
#include "lcd_drive.h" // ȷ·ȷ
#include "ui_api.h"
// --- ڴ˴Ӷ logo.c б extern  ---
//extern const unsigned short logo_image_data[320*480];
extern const unsigned int logo_image_width;
extern const unsigned int logo_image_height;
extern const unsigned int logo_image_data_len;
// -----------------------------------------------------
#endif // CONFIG_UI_ENABLE


const struct irq_info irq_info_table[] =
{
    //жϺ   //ȼ0-7   //עcpu(01)
#ifdef CONFIG_IPMASK_ENABLE
    //жϷ֧дflashжϺ͵úconstҪȫڲram
    { IRQ_SOFT5_IDX,      6,   0    }, //жǿעᵽcpu0
    { IRQ_SOFT4_IDX,      6,   1    }, //жǿעᵽcpu1
#if 0 //£SPI1ʹòж,ȼ̶7
    { IRQ_SPI1_IDX,      7,   1    },//жǿעᵽcpu0/1
#endif
#endif
#if CPU_CORE_NUM == 1
    { IRQ_SOFT5_IDX,      7,   0    }, //жǿעᵽcpu0
    { IRQ_SOFT4_IDX,      7,   1    }, //жǿעᵽcpu1
    { -2,     			-2,   -2   },//˸, ôֻи֮ǰжעᵽӦ, жǿעᵽCPU0
#endif

    { -1,     -1,   -1    },
};

/*б */
const struct task_info task_info_table[] =
{
    {"app_core",            15,     2048,	  1024 },
    {"sys_event",           29,     512,	   0 },
    {"systimer",            14,     256, 	   0 },
    {"sys_timer",            9,     512,	  128 },

    {"audio_replay_task",   11,     1024,   64    },
    {"video_replay_task",   11,     1024,   64    },
    {"video_server",        26,     800,    1024  }, // Ƶĵ
    {"vpkg_server",         26,     512,    512   }, // װ
    {"video_dec_server",    27,     1024,   1024  }, // Ƶ()
    {"vunpkg_server",       23,     1024,   128   }, // װ

#if CPU_CORE_NUM > 1
    {"#C0btencry",          16,      512,   128   },
    {"#C0btctrler",         19,      512,   384   },
    {"#C0btstack",          18,      1024,  384   },
#else
    {"btencry",             14,      512,   128   },
    {"btctrler",            19,      512,   384   },
    {"btstack",             18,      768,   384   },
#endif
    {"audio_server",        16,      512,   64    },
    {"audio_decoder",       30,     1024,   64    },
    {"audio_encoder",       12,      384,   64    },
    {"cvsd_encoder",        13,      512,   0     },
    {"aec_encoder",         13,     1024,   0     },
    {"msbc_encoder",        13,      256,   0     },
    {"audio_mix",           28,      512,   0     },
    {"aec_dbg",             17,     512,      128   },

    {"hello_demo_task",     10,     2048,     0    },
    {"ai_demo_task",        10,     2048,     0    },

#ifdef CONFIG_UI_ENABLE
    {"ui",                  21,     3072,   512   },
    {"lcd_task_0",			9,	    1024,   32    },
    {"lcd_task_1",		    9,	    1024,   32    },
    {"te_task",		       10,	    1024,	32    },
#endif

#ifdef CONFIG_WIFI_ENABLE
    {"tasklet",             10,     1400,    0},//ͨȼƽWIFIշռCPUı
    {"RtmpMlmeTask",        17,     700,  	 0},
    {"RtmpCmdQTask",        17,     300,   	 0},
    {"wl_rx_irq_thread",     5,     256,  	 0},
#endif

    {0, 0, 0, 0, 0},
};

// =====================================================================
// 2. ȫֱ߼
// =====================================================================
static u8 led_always_on = 0;  // 0=˸ģʽ  1=PWMģʽ

static void my_key_event_handler(struct sys_event *event)
{
    struct key_event *key;
    if (event->type == SYS_KEY_EVENT)
    {
        key = (struct key_event *)event->payload;
        printf(">>> [Event] Key_Value: %d, Action: %d \r\n", key->value, key->action);

        // KEY1(31) 加音量，KEY2(32) 减音量；单击 +5，长按/连发 +2 便于细调
        {
            int step = (key->action == KEY_EVENT_CLICK) ? 5 : ((key->action == KEY_EVENT_HOLD) ? 2 : 0);
            if (step) {
                if (key->value == 31) {
                    audio_stream_volume_up(step);
                    printf(">>>>>> [KEY] VOL+ -> %d\r\n", audio_stream_get_volume());
                } else if (key->value == 32) {
                    audio_stream_volume_down(step);
                    printf(">>>>>> [KEY] VOL- -> %d\r\n", audio_stream_get_volume());
                }
            }
        }

        /* KEY2(32) 现在用作"音量-"，原来的万能返回逻辑会和它抢同一个键，
           返回请用界面左上角按钮 */
        if (0 && key->value == 32 && key->action == 0) {
            printf(">>>>>> USER CLICKED KEY 32: UNIVERSAL EXIT! <<<<\n");
            #ifdef CONFIG_UI_ENABLE
            int current_page = ui_get_current_window_id();
            extern char avi_replay_exit(void);
            if (avi_replay_exit() == 0) {
                return;
            }
            if (current_page == PAGE_8) {
                #ifdef CONFIG_MP3_DEC_ENABLE
                extern void post_msg_stop_mp3(void);
                post_msg_stop_mp3(); // ֹͣ MP3 
                #endif
                ui_hide_curr_main();
                ui_show_main(PAGE_1); // ˻˵
                return;
            }
            #endif
        }
    }
}


// =====================================================================
// ص
// =====================================================================

/*
 * Ӧó
 */

static void hello_demo_task(void *p)
/*{
    printf("\r\n-------------- Hello Dongshi ! -------------- \r\n");

    int ret;
    u32 duty;
    u32 channel;
    void *pwm_dev_handl = NULL;
    struct pwm_platform_data pwm = {0};

    pwm_dev_handl = dev_open("pwm1", &pwm);
    if (!pwm_dev_handl)
    {
        printf("open pwm err !!!\n\n");
        return ;
    }
    printf("pwm : ch=0x%x,duty=%2f%%,pbit=%d,freq=%dHz\n", pwm.pwm_ch, pwm.duty, pwm.point_bit, pwm.freq);
    os_time_dly(200);


    int i = 0;
    int step = 2;
    u8 last_mode = 0xff; // ¼һģʽڼл

    while (1)
    {
        if (led_always_on)
        {
            // --- PWM ģʽ ---
            if (last_mode != 1)
            {
                // л˲䣺 PA7 Ȩ޽ PWM Ӳ
                dev_ioctl(pwm_dev_handl, PWM_RUN, (u32)&pwm);
                //gpio_output_channle(IO_PORTA_07, PWMCH1_H);
                last_mode = 1;
            }

            pwm.duty = i;
            dev_ioctl(pwm_dev_handl, PWM_SET_DUTY, (u32)&pwm);
            i += step;
            if (i >= 100 || i <= 0) step = -step;
            os_time_dly(2);
        }
        else
        {
            // --- GPIO ˸ģʽ ---
            if (last_mode != 0)
            {
                dev_ioctl(pwm_dev_handl, PWM_STOP, (u32)&pwm);//PWMֹͣ
                // л˲䣺ǿƶ PA7 ȨޣϿ PWM ӳ
                gpio_output_channle(IO_PORTA_07, -1);
                gpio_set_die(IO_PORTA_07, 1);
                last_mode = 0;
            }

            // Գɹ˸߼
            static int level = 0;
            level = !level;
            gpio_direction_output(IO_PORTA_07, level);
            os_time_dly(50); // ˸Ƶ
        }
    }
}*/
{
    // ޸ LED Ϊ PB05ܿ PA07 
    #define MY_LED_PIN  IO_PORTB_05

    gpio_set_die(MY_LED_PIN, 1);
    gpio_direction_output(MY_LED_PIN, 0);

    while (1) {
        // ˸߼в
        static int level = 0;
        level = !level;
        gpio_direction_output(MY_LED_PIN, level);
        os_time_dly(50);
    }
}


static void ai_demo_task(void *p)
{
#if HELLO_REALTIME_VOICE_ENABLE
    hello_realtime_voice_start();
#else
    ai_audio_test();
#endif
    while(1){
        os_time_dly(50);
    }
}

// =====================================================================
// Ӧ
// =====================================================================
void app_main()
{
#ifdef CONFIG_CXX_SUPPORT
    void cpp_run_init(void);
    cpp_run_init();
#endif

    printf("\r\n\n >>> [SYS] Hello Audio Project Start <<< \r\n");

    //gpio_set_die(IO_PORTA_07, 1);
    //gpio_direction_output(IO_PORTA_07, 0);

    // עᰴ¼ص
    register_sys_event_handler(SYS_KEY_EVENT, 0, 1, my_key_event_handler);

//#ifdef CONFIG_BT_ENABLE
//    bt_ble_module_init();
//    gpio_set_die(IO_PORTB_02, 1);
//    gpio_direction_output(IO_PORTB_02, 1);
//#endif

    os_task_create(hello_demo_task, NULL, 10, 2048, 0, "hello_demo_task");

    os_task_create(ai_demo_task, NULL, 10, 2048, 0, "ai_demo_task");


}
