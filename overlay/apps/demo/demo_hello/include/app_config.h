#ifndef APP_CONFIG_H
#define APP_CONFIG_H
#define CONFIG_ROOT_PATH "storage/sd0/C/"

#include "demo_config.h"

// 默认存储设备及挂载路径，与 DevKitBoard 保持一致
#define SDX_DEV "sd0"
#define CONFIG_STORAGE_PATH "storage/sd0"

// 提示音资源路径，与 DevKitBoard 的资源存储配置保持一致
#if defined CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_EXPAND_ZONE
#define CONFIG_VOICE_PROMPT_FILE_PATH "mnt/sdfile/EXT_RESERVED/aupackres/tone/"
#elif defined CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_ZONE
#define CONFIG_VOICE_PROMPT_FILE_PATH "mnt/sdfile/app/aupackres/tone/"
#else
#define CONFIG_VOICE_PROMPT_FILE_PATH "mnt/sdfile/res/audlogo/"
#endif




#define __FLASH_SIZE__    (8 * 1024 * 1024)
#define __SDRAM_SIZE__    (8 * 1024 * 1024)



//*********************************************************************************//
//                             路径配置配置                                        //
//*********************************************************************************//
#define SDX_DEV					   			      "sd0"
#define CONFIG_STORAGE_PATH 	   			      "storage/sd0"  //定义对应SD0的路径
#define CONFIG_UDISK_STORAGE_PATH  			      "storage/udisk0"//udisk0对应usb0
#define CONFIG_MUSIC_PATH_SD       			      "storage/sd0/C/"
#define CONFIG_ROOT_PATH           			      "storage/sd0/C/" //定义对应SD文件系统的根目录路径
#define CONFIG_UDISK_ROOT_PATH     			      "storage/udisk0/C/" //定义对应U盘文件系统的根目录路径
#define CONFIG_MUSIC_PATH_UDISK    			      "storage/udisk0/C/"
#define CONFIG_MUSIC_PATH_FLASH                   "mnt/sdfile/res/"
#define CONFIG_UI_PATH_FLASH                      "mnt/sdfile/res/ui_res/"
#define CONFIG_EQ_FILE_NAME                       "mnt/sdfile/res/cfg/eq_cfg_hw.bin"
#define CONFIG_UPGRADE_PATH                       "storage/sd0/C/update.ufw"
#define CONFIG_UDISK_UPGRADE_PATH                 "storage/udisk0/C/update.ufw"
#define CONFIG_REC_PATH_0                         "storage/sd0/C/DCIM/1/"
// #define CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_ZONE  //提示音资源打包后放在预留区，可以通过升级预留区更新此资源，一般用于双备份时提示音资源小于代码大小的方案
// #define CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_EXPAND_ZONE  //提示音资源打包后放在扩展预留区，不可以通过升级更新此资源，一般用于提示音不需要更新的方案
#if defined CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_EXPAND_ZONE
#define CONFIG_VOICE_PROMPT_FILE_PATH       "mnt/sdfile/EXT_RESERVED/aupackres/tone/"
#elif defined CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_ZONE
#define CONFIG_VOICE_PROMPT_FILE_PATH       "mnt/sdfile/app/aupackres/tone/"
#else
#define CONFIG_VOICE_PROMPT_FILE_PATH       "mnt/sdfile/res/audlogo/"
#endif

//**********************************END*******************************************//



//*********************************************************************************//
//                             外设模块使能配置                                    //
//*********************************************************************************//
#define CONFIG_UART_ENABLE                        /* 打印开关 	*/
#ifndef USE_EDR_DEMO
#define CONFIG_WIFI_ENABLE  			          /* 无线WIFI 	*/
#ifdef CONFIG_NO_SDRAM_ENABLE
#define CONFIG_RF_TRIM_CODE_MOVABLE //把RF TRIM 的运行代码动态加载到ram运行(节省4K ram内存), 防止RF TRIM 期间500ms大电流访问flash造成flash挂掉持续大电流
#else
#define CONFIG_RF_TRIM_CODE_AT_RAM //把RF TRIM 的运行代码定死到ram运行(浪费4K ram内存,否则若动态加载到sdram需清cache), 防止RF TRIM 期间500ms大电流访问flash造成flash挂掉持续大电流
#endif
#endif
#define CONFIG_SPI_ENABLE                         /* SPI开关    */
#define CONFIG_KEY_ENABLE                         /* 按键开关   */
#define CONFIG_PWM_ENABLE                         /* PWM开关   	*/
#define CONFIG_IIC_ENABLE                         /* IIC开关   	*/


#define CONFIG_NET_ENABLE
//**********************************END********************************************//



//*********************************************************************************//
//                                  BT_BLE配置                                     //
//*********************************************************************************//
#define CONFIG_BT_ENABLE      // <--- 必须加上这一行，否则蓝牙库代码不会被编译！


#define BT_EMITTER_EN     1
#define BT_RECEIVER_EN    2

#define CONFIG_BT_RX_BUFF_SIZE  (12 * 1024)
#define CONFIG_BT_TX_BUFF_SIZE  (6 * 1024)


#define CONFIG_POWER_ON_ENABLE_BT                 1     //开机自动打开经典蓝牙
#define TCFG_USER_BT_CLASSIC_ENABLE               1     //经典蓝牙功能
#define TCFG_USER_BLE_ENABLE                      0     //BLE功能使能
#define TCFG_USER_EMITTER_ENABLE                  0     //蓝牙发射功能
#ifdef CONFIG_LOW_POWER_ENABLE
#define TCFG_BT_SNIFF_ENABLE                      1
#else
#define TCFG_BT_SNIFF_ENABLE                      0
#endif
#define BT_SUPPORT_MUSIC_VOL_SYNC                 0     //音量同步
#define BT_SUPPORT_DISPLAY_BAT                    1     //电池电量同步显示功能
#define SPP_TRANS_DATA_EN                         1     //SPP数传demo
#define TCFG_BT_SUPPORT_AAC                       1     //蓝牙AAC格式支持
//**********************************END*******************************************//

// 音频编解码相关
#define USE_EDR_DEMO
#define CONFIG_BT_ENABLE
#define BT_MUSIC_DECODE
#define BT_PHONE_DECODE
#define CONFIG_PCM_ENC_ENABLE
#define CONFIG_PCM_DEC_ENABLE
#define CONFIG_PCM_PLAY_FIXED_RATE
#define CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE
#define CONFIG_PHONE_CALL_ADC_CHANNEL 1  // MIC1: PH8/PH9

#define CONFIG_AISP_MIC0_ADC_CHANNEL  1
#define CONFIG_AISP_MIC1_ADC_CHANNEL  0
#define CONFIG_AISP_MIC_ADC_GAIN      80

#define CONFIG_AEC_ENC_ENABLE
#define CONFIG_DNS_ENC_ENABLE
#define CONFIG_AUDIO_MIX_ENABLE
#define CONFIG_AEC_ADC_CHANNEL 0
#define CONFIG_AEC_AUDIO_ADC_GAIN 80

#define CONFIG_SBC_DEC_ENABLE
#define CONFIG_MSBC_DEC_ENABLE
#define CONFIG_CVSD_DEC_ENABLE
#define CONFIG_SBC_ENC_ENABLE
#define CONFIG_MSBC_ENC_ENABLE
#define CONFIG_CVSD_ENC_ENABLE
#define CONFIG_AAC_DEC_ENABLE

//*********************************************************************************//
//                            按键配置                                             //
//*********************************************************************************//
#define CONFIG_KEY_ENABLE  1

#define TCFG_IRKEY_ENABLE       				  0 //红外接收头
#define TCFG_RDEC_KEY_ENABLE    				  0 //旋转编码器
#define TCFG_IOKEY_ENABLE       				  0 //io按键
#define TCFG_TOUCH_KEY_ENABLE   				  0 //触摸按键
#define TCFG_CTMU_TOUCH_KEY_ENABLE  			  0 //触摸按键
#define TCFG_UART_KEY_ENABLE					  0 //串口按键
#define TCFG_ADKEY_ENABLE           			  1 //AD按键
//**********************************END*******************************************//


//编解码器使能

#define TCFG_AUDIO_ENABLE           1  // 开启音频总开关
#define TCFG_TONE_ENABLE            1  // 开启提示音功能
#define CONFIG_MP3_DEC_ENABLE       1  // 开启MP3解码支持


#define CONFIG_PCM_DEC_ENABLE
#define CONFIG_PCM_ENC_ENABLE
#define CONFIG_ADPCM_DEC_ENABLE
#define CONFIG_WAV_ENC_ENABLE
#define CONFIG_WAV_ENC_ENABLE

#define USE_DevKitBoard_TEST_DEMO




#define CONFIG_IIC_ENABLE       1
#define CONFIG_SPI_ENABLE       1
#define CONFIG_SD_ENABLE        1    // 虽然你不用SD卡，但官方配置里有，建议带着防止有些宏依赖它
#define CONFIG_VIDEO_ENABLE     1 // 摄像头/视频总开关，LCD驱动有时会依赖它





//*********************************************************************************//
//                             SD模块配置                                          //
//*********************************************************************************//
#ifdef CONFIG_SD_ENABLE
#define TCFG_SD0_ENABLE                           1          //暂只支持1个SD外设，默认开SD0
#define TCFG_SD_PORTS                     	     'A'
#define TCFG_SD_DAT_WIDTH                 	      1			//开发板只接了一线 所有只能为一线模式
#define TCFG_SD_DET_MODE                  	      SD_CMD_DECT	//CMD模式
#define TCFG_SD_DET_IO_LEVEL              	      0			//IO检卡上线的电平(0/1),SD_DET_MODE为SD_IO_DECT时有效
#define TCFG_SD_CLK                       	      20000000		//SD时钟
#endif
//**********************************END********************************************//




//*********************************************************************************//
//                                  UI配置                                         //
//*********************************************************************************//
#ifdef CONFIG_UI_ENABLE
//资源存放在预留区请查阅cpu/tool/isd_config_rule.c进行配置资源文件地址以及大小
//#define UI_USE_WIFI_CAMERA_PROJECT          //使用wifi_camera_ui工程 该宏会在download.c进行调用 批处理自动生成UI资源文件
#define CONFIG_UI_RES_SAVE_EXTFLASH               0  //资源文件存放在flash最后不进行备份使用这个可以提高编译下载速度，需要根据提示自行解决下载问题 具体阅读isd_config_rule.c

#if CONFIG_UI_RES_SAVE_EXTFLASH
//*********************************************************************************//
//                          预留区UI和AUDIO资源配置                                //
//*********************************************************************************//
//注意：ui和audio资源的起始地址和大小, 根据产品生命周期最大情况定义,根据实际需求进行配置
//FLASH后面4K用于一些配置存储，所以禁止覆盖
//具体说明和注意事项请阅读文档

//(1)ui和audio资源 ,如果存在ui资源则位于扩展预留区末尾,
//还存在audio则位于ui项前面，其他配置项则位于它们之前.
/*
#------------------------------|
#  (其他预留区配置项)          |
#------------------------------|<----CONFIG_UI_PACKRES_ADR - CONFIG_AUDIO_PACKRES_LEN = CONFIG_AUDIO_PACKRES_ADR
#  (CONFIG_AUDIO_PACKRES_LEN)  |
#------------------------------|<----__FLASH_SIZE__ - 0x1000 - CONFIG_UI_PACKRES_LEN = CONFIG_UI_PACKRES_ADR
#  (CONFIG_UI_PACKRES_LEN)     |
#------------------------------|<----__FLASH_SIZE__ - 0x1000
#  (4K Reserved)               |
#------------------------------+<----__FLASH_SIZE__
*/

//(2)只有audio资源,则将其位于扩展预留区末尾.
/*
#------------------------------|
#  (其他预留区配置项)          |
#------------------------------|<----__FLASH_SIZE__ - 0x1000 - CONFIG_AUDIO_PACKRES_LEN = CONFIG_AUDIO_PACKRES_ADR
#  (CONFIG_AUDIO_PACKRES_LEN)  |
#------------------------------|<----__FLASH_SIZE__ - 0x1000
#  (4K Reserved)               |
#------------------------------+<----__FLASH_SIZE__
*/

#if defined CONFIG_UI_ENABLE && !defined CONFIG_SDFILE_EXT_ENABLE
#define CONFIG_UI_FILE_SAVE_IN_RESERVED_EXPAND_ZONE //UI资源打包后放在扩展预留区
#endif

#if defined CONFIG_AUDIO_ENABLE && !defined CONFIG_SDFILE_EXT_ENABLE
#define CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_EXPAND_ZONE //AUDIO资源打包后放在扩展预留区
#endif

#if defined CONFIG_UI_FILE_SAVE_IN_RESERVED_EXPAND_ZONE
#define CONFIG_UI_PACKRES_LEN 0x400000
#define CONFIG_UI_PACKRES_ADR ((__FLASH_SIZE__) - (CONFIG_UI_PACKRES_LEN) - 0x1000)
#else
#define CONFIG_UI_PACKRES_LEN 0
#define CONFIG_UI_PACKRES_ADR 0
#endif

#if defined CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_EXPAND_ZONE
#if defined CONFIG_UI_FILE_SAVE_IN_RESERVED_EXPAND_ZONE
#define CONFIG_AUDIO_PACKRES_LEN 0x180000
#define CONFIG_AUDIO_PACKRES_ADR ((__FLASH_SIZE__) - (CONFIG_UI_PACKRES_LEN) - 0x1000 - CONFIG_AUDIO_PACKRES_LEN)
#else
#define CONFIG_AUDIO_PACKRES_LEN 0x180000
#define CONFIG_AUDIO_PACKRES_ADR ((__FLASH_SIZE__) - CONFIG_AUDIO_PACKRES_LEN - 0x1000)
#endif
#else
#define CONFIG_AUDIO_PACKRES_LEN 0
#define CONFIG_AUDIO_PACKRES_ADR 0
#endif
#endif

#define CONFIG_VIDEO_DEC_ENABLE             	  1  //打开视频解码器
#define TCFG_USE_SD_ADD_UI_FILE             	  0  //使用SD卡加载资源文件

#define USE_LCD_240X320                           1
#define USE_LCD_320X480                           1


#if USE_LCD_240X320
#define TCFG_TOUCH_GT911_ENABLE             	  0
#define TCFG_LCD_ILI9341_ENABLE	    	    	  0
#endif //USE_LCD_240X320_ILI9341

#if USE_LCD_320X480
#define TCFG_TOUCH_FT6236_ENABLE                  1
#define TCFG_LCD_ILI9488_ENABLE	    	    	  1
#define TCFG_LCD_ILI9481_ENABLE	    	    	  1
#endif //USE_LCD_320X480_ILI9481


#if (defined(USE_CAMERA_DVP_SHOW_TO_LCD_DEMO) || defined(AVI_REPLAY_DEMO))
#define HORIZONTAL_SCREEN                   	  0//0为使用竖屏  //1为使能横屏配置
#else
#define HORIZONTAL_SCREEN                   	  0//0为使用竖屏  //1为使能横屏配置
#endif

#define USE_LCD_TE                          	  1
#endif//CONFIG_UI_ENABLE
//**********************************END*******************************************//






//*********************************************************************************//
//                                  电源配置                                     //
//*********************************************************************************//
#define TCFG_LOWPOWER_BTOSC_DISABLE			0
#define TCFG_LOWPOWER_LOWPOWER_SEL			0//(RF_SLEEP_EN|RF_FORCE_SYS_SLEEP_EN|SYS_SLEEP_EN) //该宏在睡眠低功耗才用到，此处设置为0
#define TCFG_LOWPOWER_VDDIOM_LEVEL			VDDIOM_VOL_32V //正常工作的内部vddio电压值，一般使用外部3.3V，内部设置需比外部3.3V小

#ifdef CONFIG_RTC_ENABLE
#define TCFG_LOWPOWER_VDDIOW_LEVEL			VDDIOW_VOL_32V       //弱VDDIO电压档位。RTCVDD电压低于3.2V可能不走时，因此RTCVDD由IOVDD供电时，VDDIOW应设置为VDDIOW_VOL_32V档
#else
#define TCFG_LOWPOWER_VDDIOW_LEVEL			VDDIOW_VOL_21V       //弱VDDIO电压档位
#endif

#define VDC14_VOL_SEL_LEVEL					VDC14_VOL_SEL_140V //内部的1.4V默认1.4V
#define SYSVDD_VOL_SEL_LEVEL				SYSVDD_VOL_SEL_126V //系统内核电压，默认1.26V


#define CONFIG_CXX_SUPPORT //使能C++支持

#define CONFIG_DEBUG_ENABLE                     /* 打印开关 */

//#define SDTAP_DEBUG

#if !defined CONFIG_DEBUG_ENABLE || defined CONFIG_LIB_DEBUG_DISABLE
#define LIB_DEBUG    0
#else
#define LIB_DEBUG    1
#endif
#define CONFIG_DEBUG_LIB(x)         (x & LIB_DEBUG)

#include "hello_realtime_voice.h"
#if HELLO_REALTIME_VOICE_ENABLE && !defined(CONFIG_VAD_ENC_ENABLE)
#define CONFIG_VAD_ENC_ENABLE
#endif

#endif

