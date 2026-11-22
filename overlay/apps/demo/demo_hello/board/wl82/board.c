#include "app_config.h"

#include "system/includes.h"
#include "device/includes.h"
#include "asm/includes.h"
#include "server/audio_dev.h"

#include "device/iic.h"
#ifdef CONFIG_UI_ENABLE
#include "lcd_drive.h"
#include "ui_api.h"
#endif
// *INDENT-OFF*


UART2_PLATFORM_DATA_BEGIN(uart2_data)
	.baudrate = 115200,
	.port = PORT_REMAP,
	.output_channel = OUTPUT_CHANNEL0,
	.tx_pin = IO_PORTB_03,
	.rx_pin = -1,
	.max_continue_recv_cnt = 1024,
	.idle_sys_clk_cnt = 500000,
	.clk_src = PLL_48M,
	.flags = UART_DEBUG,
UART2_PLATFORM_DATA_END();

//例如：PWM1
PWM_PLATFORM_DATA_BEGIN(pwm_data1)
    .port  = IO_PORTA_07,
    .pwm_ch = PWMCH1_H,
    .freq   = 1000,//频率
    .duty   = 50,//占空比
    .point_bit = 0,
PWM_PLATFORM_DATA_END()

//*********************************************************************************//
//                                WIFI,蓝牙模块配置                                //
//*********************************************************************************//

#if defined CONFIG_BT_ENABLE || defined CONFIG_WIFI_ENABLE
#include "wifi/wifi_connect.h"
const struct wifi_calibration_param wifi_calibration_param = {
    .xosc_l     = 0xb,// 调节左晶振电容
    .xosc_r     = 0xb,// 调节右晶振电容
    .pa_trim_data  ={1, 7, 4, 7, 11, 1, 7},// 根据MP测试生成PA TRIM值
    .mcs_dgain     ={
        45,//11B_1M
        45,//11B_2.2M
        45,//11B_5.5M
        42,//11B_11M

        60,//11G_6M
        60,//11G_9M
        75,//11G_12M
        70,//11G_18M
        62,//11G_24M
        52,//11G_36M
        50,//11G_48M
        38,//11G_54M

        62,//11N_MCS0
        80,//11N_MCS1
        70,//11N_MCS2
        62,//11N_MCS3
        50,//11N_MCS4
        48,//11N_MCS5
        40,//11N_MCS6
        36,//11N_MCS7
    }
};
#endif

//**********************************END*******************************************//

//*********************************************************************************//
//                                   按键模块配置                                    //
//*********************************************************************************//
#ifdef CONFIG_KEY_ENABLE
#if TCFG_IRKEY_ENABLE //红外按键

const struct irkey_platform_data irkey_data = {
    .enable = 1,
    .port = IO_PORTB_05,
};
#endif

#if TCFG_RDEC_KEY_ENABLE //旋转编码器
const struct rdec_device rdeckey_list[] = {
    {
        .index = RDEC0 ,
        .sin_port0 = IO_PORTB_06,
        .sin_port1 = IO_PORTB_07,
        .key_value0 = KEY_VOLUME_DEC | BIT(7),
        .key_value1 = KEY_VOLUME_INC | BIT(7),
    },
};
const struct rdec_platform_data rdec_key_data = {
    .enable = 1,
    .num = ARRAY_SIZE(rdeckey_list),            //RDEC按键的个数
    .rdec = rdeckey_list,                       //RDEC按键参数表
};
#endif

#if TCFG_IOKEY_ENABLE
static const struct iokey_port iokey_list[] = {
    {
        .connect_way = ONE_PORT_TO_LOW,         //IO按键的连接方式
        .key_type.one_io.port = IO_PORTB_06,    //IO按键对应的引脚
        .key_value = KEY_VOLUME_DEC,            //按键值
    },
    {
        .connect_way = ONE_PORT_TO_LOW,
        .key_type.one_io.port = IO_PORTB_07,
        .key_value = KEY_VOLUME_INC,            //按键值
    },
};
const struct iokey_platform_data iokey_data = {
    .enable = 1,                                //是否使能IO按键
    .num = ARRAY_SIZE(iokey_list),              //IO按键的个数
    .port = iokey_list,                         //IO按键参数表
};
#endif

#if TCFG_TOUCH_KEY_ENABLE
static const struct touch_key_port plcnt_port[] = {
    {
        .port = IO_PORTC_09,
        .key_value = KEY_VOLUME_DEC,
    },
    {
        .port = IO_PORTC_10,
        .key_value = KEY_VOLUME_INC,
    },
};
const struct touch_key_platform_data touch_key_data = {
    .num            = ARRAY_SIZE(plcnt_port),
    .clock          = TOUCH_KEY_PLL_240M_CLK,
    .change_gain 	= 4,		//变化放大倍数, 一般固定
    .press_cfg		= -100,		//触摸按下灵敏度, 类型:s16, 数值越大, 灵敏度越高
    .release_cfg0 	= -50,		//触摸释放灵敏度0, 类型:s16, 数值越大, 灵敏度越高
    .release_cfg1 	= -80,		//触摸释放灵敏度1, 类型:s16, 数值越大, 灵敏度越高
    .port_list      = plcnt_port,
};
#endif

#if TCFG_CTMU_TOUCH_KEY_ENABLE
static const struct touch_key_port ctmu_port[] = {
    {
        .port = IO_PORTC_09,
        .key_value = KEY_VOLUME_DEC,
    },
    {
        .port = IO_PORTC_10,
        .key_value = KEY_VOLUME_INC,
    },
};
const struct touch_key_platform_data ctmkey_data = {
    .num            = ARRAY_SIZE(ctmu_port),
    .press_cfg		= -10,
    .release_cfg0 	= -50,
    .release_cfg1 	= -80,
    .port_list      = ctmu_port,
};
#endif

#if TCFG_ADKEY_ENABLE
#define ADKEY_UPLOAD_R  22
#define ADC_VDDIO (0x3FF)
#define ADC_09   (0x3FF)
#define ADC_08   (0x3FF)
#define ADC_07   (0x3FF * 150 / (150 + ADKEY_UPLOAD_R))
#define ADC_06   (0x3FF * 62  / (62  + ADKEY_UPLOAD_R))
#define ADC_05   (0x3FF * 36  / (36  + ADKEY_UPLOAD_R))
#define ADC_04   (0x3FF * 22  / (22  + ADKEY_UPLOAD_R))
#define ADC_03   (0x3FF * 13  / (13  + ADKEY_UPLOAD_R))
#define ADC_02   (0x3FF * 75  / (75  + ADKEY_UPLOAD_R * 10))
#define ADC_01   (0x3FF * 3   / (3   + ADKEY_UPLOAD_R))
#define ADC_00   (0)
#define ADKEY_V_9      	((ADC_09 + ADC_VDDIO)/2)
#define ADKEY_V_8 		((ADC_08 + ADC_09)/2)
#define ADKEY_V_7 		((ADC_07 + ADC_08)/2 + 50)
#define ADKEY_V_6 		((ADC_06 + ADC_07)/2)
#define ADKEY_V_5 		((ADC_05 + ADC_06)/2)
#define ADKEY_V_4 		((ADC_04 + ADC_05)/2)
#define ADKEY_V_3 		((ADC_03 + ADC_04)/2)
#define ADKEY_V_2 		((ADC_02 + ADC_03)/2)
#define ADKEY_V_1 		((ADC_01 + ADC_02)/2)
#define ADKEY_V_0 		((ADC_00 + ADC_01)/2)
const struct adkey_platform_data adkey_data = {
    .enable     = 1,
    .adkey_pin  = IO_PORTB_01,
    .extern_up_en = 1,
    .ad_channel = 3,
    .ad_value = {
        ADKEY_V_0,//122   //0~122
        ADKEY_V_1,//192   //122~192
        ADKEY_V_2,//319   //192~319
        ADKEY_V_3,//445   //319~445
        ADKEY_V_4,//572   //445~572
        ADKEY_V_5,//694   //572~694
        ADKEY_V_6,//823   //694~823
        ADKEY_V_7,//1007  //823~1007
        ADKEY_V_8,//1023  //1007~1023
        ADKEY_V_9,//1023  //1023
    },
    .key_value = {
        KEY_8,
        KEY_7,
        KEY_6,
        KEY_5,
        KEY_4,
        KEY_3,
        KEY_2,
        KEY_1,
	    NO_KEY,
	    NO_KEY,
    },
};
#endif
#endif//CONFIG_KEY_ENABLE
//**********************************END*******************************************//


//*********************************************************************************//
//                                   IIC模块配置                                    //
//*********************************************************************************//
#ifdef CONFIG_IIC_ENABLE
HW_IIC0_PLATFORM_DATA_BEGIN(hw_iic1_data)//硬件IIC
    .clk_pin = IO_PORTH_00,
    .dat_pin = IO_PORTH_01,
    .baudrate = 0x3f,
HW_IIC0_PLATFORM_DATA_END()

SW_IIC_PLATFORM_DATA_BEGIN(sw_iic0_data)//软件iic
    .clk_pin = IO_PORTH_00,
    .dat_pin = IO_PORTH_01,
    .sw_iic_delay = 50,
SW_IIC_PLATFORM_DATA_END()
#endif
//**********************************END*******************************************//

//*********************************************************************************//
//                                   SPI模块配置                                    //
//*********************************************************************************//
#ifdef CONFIG_SPI_ENABLE
SPI0_PLATFORM_DATA_BEGIN(spi0_data)
    .clk    = 40000000,
    .mode   = SPI_DUAL_MODE,
    .port   = 'A',
    .attr   = SPI_SCLK_L_UPL_SMPH | SPI_UPDATE_SAMPLE_SAME,
SPI0_PLATFORM_DATA_END()

SPI1_PLATFORM_DATA_BEGIN(spi1_data)
    .clk    = 2000000,
    .mode   = SPI_STD_MODE,
    .port   = 'B',
    .attr   = SPI_SCLK_L_UPH_SMPH | SPI_BIDIR_MODE,
SPI1_PLATFORM_DATA_END()

SPI2_PLATFORM_DATA_BEGIN(spi2_data)
    .clk    = 40000000,
    .mode   = SPI_DUAL_MODE,
    .port   = 'C',
    .attr	= (SPI_MODE_SLAVE | SPI_SCLK_L_UPL_SMPH | SPI_UNIDIR_MODE),//从机，CLK低 更新数据低，单向模式

SPI2_PLATFORM_DATA_END()

static const struct spiflash_platform_data spiflash_data = {
    .name           = "spi0",
    .mode           = FAST_READ_OUTPUT_MODE,//FAST_READ_IO_MODE,
    .sfc_run_mode   = SFC_FAST_READ_DUAL_OUTPUT_MODE,
};
#endif
//**********************************END*******************************************//



//*********************************************************************************//
//                                   UI模块配置                                    //
//*********************************************************************************//
#ifdef CONFIG_UI_ENABLE //注意使用UI不能使用SD卡D口 建议使用A口 io冲突问题

#define EMI_BAUD		EMI_BAUD_DIV4
const struct emi_platform_data emi_data = {
    .bits_mode      = EMI_8BITS_MODE,
    .baudrate       = EMI_BAUD,			//clock = HSB_CLK / (baudrate + 1) , HSB分频
    .colection      = EMI_FALLING_COLT,		//EMI_FALLING_COLT / EMI_RISING_COLT : 下降沿 上升沿 采集数据
    .time_out       = 1 * 1000,				//最大写超时时间ms
    .th             = EMI_TWIDTH_NO_HALF,
    .ts             = 0,
    .tw             = EMI_BAUD,
    .data_bit_en    = 0,					//0默认根据bits_mode数据位来配置
};
const struct ui_lcd_platform_data pdata = {
    .rst_pin = IO_PORTB_00,//触摸和屏共用一个复位引脚
    .cs_pin = -1,
    .rs_pin = IO_PORTC_09,
    .bl_pin = IO_PORTB_08,
    .te_pin = IO_PORTC_00,
    .check_pin_D6 = IO_PORTC_07,//用于检查区分不同的显示屏驱动
    .check_pin_D7 = IO_PORTC_08,
    .touch_int_pin = IO_PORTB_04,
    .lcd_if  = LCD_EMI,//屏幕接口类型还有 PAP , SPI
};
const struct ui_devices_cfg ui_cfg_data = {
    .type = TFT_LCD,
    .private_data = (void *) &pdata,
};
#endif
//**********************************END*******************************************//


#ifdef CONFIG_PRESS_LONG_KEY_POWERON
    //ad按键和开关机键复用
    static unsigned int read_power_key(int dly)
    {
        gpio_latch_en(adkey_data.adkey_pin, 0);
        gpio_direction_input(adkey_data.adkey_pin);
        gpio_set_die(adkey_data.adkey_pin, 0);
        gpio_set_pull_up(adkey_data.adkey_pin, 0);
        gpio_set_pull_down(adkey_data.adkey_pin, 0);

        if (dly) {
            delay_us(3000);
        }

        JL_ADC->CON = (0xf << 12) | (adkey_data.ad_channel << 8) | (1 << 6) | (1 << 4) | (1 << 3) | (6 << 0);
        while (!(JL_ADC->CON & BIT(7)));
        return (JL_ADC->RES < 50);
    }

    void sys_power_poweroff_wait_powerkey_up(void)
    {
        JL_TIMER1->CON = 0;
        JL_TIMER1->CON = 0;
        JL_TIMER1->CON = 0;
        JL_ADC->CON = 0;
        JL_ADC->CON = 0;
        JL_ADC->CON = 0;
        while (read_power_key(1));
    }
#endif

//*********************************************************************************//
//                            音频模块配置                  //
//*********************************************************************************//
static const struct dac_platform_data dac_data = {
    .sw_differ = 0,
    .pa_auto_mute = 1,
    .pa_mute_port = IO_PORTB_02,
    .pa_mute_value = 0,
    .differ_output = 0,
    .hw_channel = 0x05,
    .ch_num = 4,
    .vcm_init_delay_ms = 1000,
    .mute_delay_ms = 200,
    #ifdef CONFIG_DEC_ANALOG_VOLUME_ENABLE
    .fade_enable = 1,
    .fade_delay_ms = 40,
    #endif
};
static const struct adc_platform_data adc_data = {
    #ifdef CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE
    .mic_channel = (LADC_CH_MIC1_P_N | LADC_CH_MIC3_P_N),
    .linein_channel = (LADC_CH_AUX1 | LADC_CH_AUX3),
    .mic_ch_num = 2,
    .linein_ch_num = 2,
    .all_channel_open = 1,
    #else
    .mic_channel = LADC_CH_MIC1_P_N,
    .mic_ch_num = 1,
    #endif
    .isel = 2,
    .dump_num = 480,
};
static const struct iis_platform_data iis0_data = {
    .channel_in = BIT(1),
    .channel_out = BIT(0),
    .port_sel = IIS_PORTA,
    .data_width = 0,
    .mclk_output = 0,
    .slave_mode = 0,
    .dump_points_num = 320,
};
static const struct iis_platform_data iis1_data = {
    .channel_in = BIT(0),
    .channel_out = BIT(1),
    .port_sel = IIS_PORTA,
    .data_width = 0,
    .mclk_output = 0,
    .slave_mode = 0,
    .dump_points_num = 320,
};


static void plnk0_port_remap_cb(void)
{
    extern int gpio_plnk_rx_input(u32 gpio, u8 index, u8 data_sel);
    gpio_plnk_rx_input(IO_PORTH_02, 0, 0);
    gpio_output_channle(IO_PORTH_03, CH0_PLNK0_SCLK_OUT);
    JL_IOMAP->CON3 |= BIT(18);
}
static void plnk0_port_unremap_cb(void)
{
    JL_IOMAP->CON3 &= ~BIT(18);
    gpio_clear_output_channle(IO_PORTH_03, CH0_PLNK0_SCLK_OUT);
    gpio_set_die(IO_PORTH_02, 0);
}
static const struct plnk_platform_data plnk0_data = {
    .hw_channel = PLNK_CH_MIC_L,
    .clk_out = 1,
    .port_remap_cb = plnk0_port_remap_cb,
    .port_unremap_cb = plnk0_port_unremap_cb,
    .sample_edge = 0,
    .share_data_io = 1,
    .high_gain = 1,
    .dc_cancelling_filter = 14,
    .dump_points_num = 640,
};
static const struct plnk_platform_data plnk1_data = {
    .hw_channel = PLNK_CH_MIC_DOUBLE,
    .clk_out = 1,
    .dc_cancelling_filter = 14,
    .dump_points_num = 640,
};


static const struct audio_pf_data audio_pf_d = {
    .adc_pf_data = &adc_data,
    .dac_pf_data = &dac_data,
    .iis0_pf_data = &iis0_data,
    .iis1_pf_data = &iis1_data,
    .plnk0_pf_data = &plnk0_data,
    .plnk1_pf_data = &plnk1_data,
};
static const struct audio_platform_data audio_data = {
    .private_data = (void *) &audio_pf_d,
};
//**********************************END*******************************************//


//*********************************************************************************//
//                                   SD模块配置                                    //
//*********************************************************************************//
#ifdef CONFIG_SD_ENABLE
SD0_PLATFORM_DATA_BEGIN(sd0_data)
    .port 					= TCFG_SD_PORTS,
    .priority 				= 3,
    .data_width 			= TCFG_SD_DAT_WIDTH,
    .speed 					= TCFG_SD_CLK,
    .detect_mode 			= TCFG_SD_DET_MODE,
#if (TCFG_SD_DET_MODE == SD_CLK_DECT)
    .detect_func 			= sdmmc_0_clk_detect,
#elif (TCFG_SD_DET_MODE == SD_IO_DECT)
    .detect_func 			= sdmmc_0_io_detect,
#else
    .detect_func 			= NULL,
#endif
SD0_PLATFORM_DATA_END()
#endif//TCFG_SD0_ENABLE
//**********************************END*******************************************//



/**************************  POWER config ****************************/
static const struct low_power_param power_param = {
    .config         = TCFG_LOWPOWER_LOWPOWER_SEL,          //系统休眠
    .btosc_disable  = TCFG_LOWPOWER_BTOSC_DISABLE,         //进入低功耗时BTOSC是否保持
    .vddiom_lev     = TCFG_LOWPOWER_VDDIOM_LEVEL,          //强VDDIO等级,可选：2.8V  2.9V  3.0V  3.1V  3.2V  3.3V  3.4V  3.5V
    .vddiow_lev     = TCFG_LOWPOWER_VDDIOW_LEVEL,          //弱VDDIO等级,可选：2.1V  2.4V  2.8V  3.2V
	.vdc14_dcdc 	= TRUE,	   							   //打开内部1.4VDCDC，关闭则用外部
    .vdc14_lev		= VDC14_VOL_SEL_LEVEL, 				   //VDD1.4V配置
	.sysvdd_lev		= SYSVDD_VOL_SEL_LEVEL,				   //内核、sdram电压配置
	.vlvd_enable	= TRUE,                                //TRUE电压复位使能
	.vlvd_value		= VLVD_SEL_25V,                        //低电压复位电压值
};

REGISTER_DEVICES(device_table) = {
	{"uart2", &uart_dev_ops, (void *)&uart2_data },
	{"rtc", &rtc_dev_ops, NULL},
	{"pwm1",   &pwm_dev_ops,  (void *)&pwm_data1},
	{"audio", &audio_dev_ops, (void *)&audio_data },
	{"emi",   &emi_dev_ops,   (void *)&emi_data }, // <--- 必须加上这一行！

#ifdef CONFIG_SD_ENABLE
    {"sd0",  &sd_dev_ops, (void *)&sd0_data},
#endif

#ifdef CONFIG_SPI_ENABLE
    { "spi0", &spi_dev_ops, (void *)&spi0_data },
    { "spi1", &spi_dev_ops, (void *)&spi1_data },
    { "spi2", &spi_dev_ops, (void *)&spi2_data },
#endif


#ifdef CONFIG_IIC_ENABLE
    { "iic0",  &iic_dev_ops, (void *)&sw_iic0_data },
    { "iic1",  &iic_dev_ops, (void *)&hw_iic1_data },
#endif

};

#ifdef CONFIG_DEBUG_ENABLE
void debug_uart_init()
{
    uart_init(&uart2_data);
}
#endif

void board_early_init()
{
    dac_early_init(0, dac_data.differ_output ? (dac_data.ch_num > 1 ? 0xf : 0x3): dac_data.hw_channel, 1000);
    devices_init();
}

static void board_power_init(void)
{

    power_init(&power_param);
}

void board_init()
{
	board_power_init();
#if TCFG_ADKEY_ENABLE || (defined CONFIG_BT_ENABLE || defined CONFIG_WIFI_ENABLE)
    adc_init();
#endif
    key_driver_init();
#ifdef CONFIG_AUTO_SHUTDOWN_ENABLE
	sys_power_init();
#endif
#ifdef CONFIG_BT_ENABLE
	void cfg_file_parse(void);
	cfg_file_parse();
#endif

}
