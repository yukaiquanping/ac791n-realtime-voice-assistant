#include "app_config.h"
#include "system/includes.h"
#include "system/server/server_core.h"
#include "server/audio_server.h"
#include "system/generic/circular_buf.h"
#include "os/os_api.h"
#include "net/http/http_cli.h"
#include "wifi/wifi_connect.h"
#include "audio_music/pcm_play_api.h"   // 官方内存 PCM 流播放接口
#include "device/gpio.h"                // GPIO/LED 控制

// ===== 状态指示灯（PA7，app_main 已初始化为输出）=====

#define STATE_LED_PIN   IO_PORTA_07
#define led_state_on()      gpio_direction_output(STATE_LED_PIN, 0)
#define led_state_off()     gpio_direction_output(STATE_LED_PIN, 1)

// 星火文字问答（实现在 spark_llm_test.c，独立文件避免 stdio.h 与 fs.h 冲突）
int spark_text_request_with(const char *question);
const char *spark_get_answer(void);

// 语音听写 ASR（实现在 spark_asr_test.c）
int asr_open(void);
int asr_send(const unsigned char *data, u32 len, u8 status);
const char *asr_get_result(void);
int asr_is_done(void);
int asr_close(void);

// 前置声明
void tts_test_start(void);
void tts_close(void);
int tts_get_audio(unsigned char **buf, unsigned int *len);

// ===== TTS 音频播放 =====
// TTS 收到的 PCM 数据指针和长度（由 spark_tts_test.c 提供）
static unsigned char *tts_pcm_buf = NULL;
static unsigned int tts_pcm_len = 0;

// 说明：改用官方 audio_pcm_play_* 接口播放内存 PCM 流，
// 它内部自动处理 单声道->双声道 扩展、cbuffer 缓冲、信号量，最可靠。

// 播放 TTS 拿到的 PCM（16k 单声道 16bit）
static void tts_play(void)
{
    if (!tts_get_audio(&tts_pcm_buf, &tts_pcm_len) || tts_pcm_len == 0) {
        printf("[TTS_PLAY] no audio to play\n");
        return;
    }
    printf("[TTS_PLAY] play %u bytes audio...\n", tts_pcm_len);

    // ===== 用官方 audio_pcm_play_* 接口播放内存 PCM =====
    int sample_rate = 16000;
    int channel = 1;

    // frame_size 影响内部缓冲大小：cache_buf_size = frame_size*4*(rate/11025)。
    //   frame_size=16000 -> 缓冲 64000 字节 = 2秒(单声道16k)。较大缓冲使喂入更流畅、播放不易卡顿，
    //   且喂完时缓冲剩余约2秒，等待时间好掌控。
    // 音量 = 第5个参数（0~100，100最大），改小此数值即可降低播放声音
    void *pcm = audio_pcm_play_open(sample_rate, 16000, 0, channel, 60, 1);
    if (!pcm) {
        printf("[TTS_PLAY] pcm play open fail\n");
        return;
    }

    if (audio_pcm_play_start(pcm) != 0) {
        printf("[TTS_PLAY] pcm play start fail\n");
        audio_pcm_play_stop(pcm);
        return;
    }
    printf("[TTS_PLAY] pcm play started, feeding %u bytes...\n", tts_pcm_len);

    // 分块写入 PCM 数据（官方接口内部做单声道->双声道扩展）
    u32 written = 0;
    while (written < tts_pcm_len) {
        u32 chunk = tts_pcm_len - written;
        if (chunk > 1024) chunk = 1024;
        int ret = audio_pcm_play_data_write(pcm, tts_pcm_buf + written, chunk);
        if (ret != (int)chunk) {
            printf("[TTS_PLAY] data_write ret=%d (want %u)\n", ret, chunk);
            break;
        }
        written += chunk;
    }
    printf("[TTS_PLAY] fed %u bytes\n", written);

    // 等待播放完成。
    // 关键1：用 msleep（参数明确为毫秒），避免 os_time_dly 的 tick 单位不确定导致等待不足。
    // 关键2：audio_pcm_play_data_write 是阻塞喂入（缓冲满时等播放消费），喂入过程本身已与播放并行推进。
    //        本函数把播放缓冲设为约2秒，喂完时缓冲里还剩约2秒数据，因此喂完后只需再等
    //        "音频时长+1s"与 3s 的较小值即可播完，避免播放完还白等（之前等8s导致"播完还等好久"）。
    // Keep the decoder fed through the last partial PCM block.
    static u8 silence[1024];
    for (unsigned int i = 0; i < 8; ++i) {
        if (audio_pcm_play_data_write(pcm, silence, sizeof(silence)) != sizeof(silence)) {
            break;
        }
    }
    int wait_ms = (written + 8 * sizeof(silence)) / 32 + 1000;
    msleep(wait_ms);
    printf("[TTS_PLAY] play done\n");

    audio_pcm_play_stop(pcm);
}

// ===== 完整语音对话主流程 =====
// 在 audio_encoder 回调里直接实时调用 asr_send 发送音频（这样 ASR 才能识别到内容）。
// asr_send 内部的大缓冲已改为 static，不再占用 audio_encoder 的小栈，避免栈溢出。
// ======================================================
// ===== 采集 + 上传（采用官方 audio_input.c 架构）=====
// 讯飞 IAT 要求：每帧音频攒满 1280 字节(40ms@16k16bit) 才发送，且按 40ms 间隔连续发送。
// 因此：MIC 编码器把全量 PCM 写入环形缓冲（不做任何能量检测），
//       独立上传线程从环形缓冲攒够 1280 字节再发送给讯飞 ASR。
// 说话起止由编码器内部 VAD（use_vad=1）通过 SPEAK_START/STOP 事件判定。
#define ASR_ENC_FRAME_SIZE     640     // 编码器回调粒度 40ms@16k (640字节)
#define ASR_UPLOAD_FRAME_SIZE  1280    // 上传给讯飞的帧大小 40ms@16k (1280字节)
#define ASR_CBUF_SIZE          (ASR_UPLOAD_FRAME_SIZE * 10)  // 环形缓冲 400ms

static struct server *asr_enc_server = NULL;
static volatile int asr_turn_done = 0;     // 本轮说话结束标志（上传线程置位）
static volatile int asr_upload_active = 0; // 本轮上传是否激活（mic_asr_start 置1，结束清0）
static volatile int asr_cbuf_inited = 0;   // 环形缓冲是否已初始化
static volatile u32 asr_record_start_ms = 0; // 录音启动时间戳（用于抑制启动瞬态）
static cbuffer_t asr_pcm_cbuf;             // MIC有效语音PCM环形缓冲
static u8 *asr_pcm_buf;
static u8 asr_upload_buf[ASR_UPLOAD_FRAME_SIZE];  // 攒满1280字节的发送缓冲

// 说话状态标志：mic_asr_fwrite 用能量检测维护。
//   检测到语音(avg>阈值)置 asr_speaking=1；
//   连续静音达 ASR_SPEAK_END_CNT 帧后置 asr_speaking=0（说话结束）。
// 上传线程据此：说话时上传、说话结束发结束帧，避免说话后持续上传静音导致讯飞迟迟不返回结果。
static volatile int asr_speaking = 0;
static int asr_speak_silent_cnt = 0;   // 连续静音帧计数（mic_asr_fwrite 内使用）
static int asr_speak_confirm = 0;      // 连续语音帧计数（用于确认真正说话，防噪声误触发）

// 有效语音能量阈值（检测说话起止）
#define ASR_SPEAK_THRESHOLD  300
// 连续 ASR_SPEAK_CONFIRM 帧 avg>阈值 才确认为"开始说话"，避免单次噪声尖峰误触发识别空
#define ASR_SPEAK_CONFIRM  3   // 3帧 * 40ms = 120ms
// 说话结束后，连续静音多少帧判定"说话结束"并通知上传线程发结束帧（每帧40ms）
// 1600ms 静音：给说话中间停顿留出时间，避免一句话没说完就被误判结束、语音太短识别为空。
#define ASR_SPEAK_END_CNT  100
// 录音启动后需等待稳定（丢弃前 400ms 数据，抑制 MIC 上电瞬态噪声，如 avg=6000+）
#define ASR_START_STABLE_MS  400

// 编码器回调：全量写入环形缓冲（不做能量过滤），保证上传给讯飞的音频流连续完整。
// 说话中的弱音/静音帧不会被丢弃，避免"音频不连续导致时灵时不灵"。
// 同时用能量检测维护 asr_speaking，告诉上传线程何时开始上传、何时结束。
static int mic_asr_fwrite(void *file, void *data, u32 len)
{
    if (!data || len < 2) {
        return len;
    }
    // 录音启动稳定期：丢弃前 ASR_START_STABLE_MS 数据，抑制 MIC 上电瞬态噪声
    if (asr_record_start_ms > 0 &&
        (int)(timer_get_ms() - asr_record_start_ms) < ASR_START_STABLE_MS) {
        return len;
    }

    // 能量检测：维护说话状态（不丢帧，只用于起止判定）
    short *p = (short *)data;
    int n = len / 2;
    long sum = 0;
    for (int i = 0; i < n; i++) {
        int v = p[i];
        if (v < 0) v = -v;
        sum += v;
    }
    int avg = n > 0 ? (int)(sum / n) : 0;
    if (avg > ASR_SPEAK_THRESHOLD) {
        // 连续多帧 avg>阈值 才确认为"开始说话"，防噪声尖峰误触发识别空
        if (asr_speak_confirm < ASR_SPEAK_CONFIRM) {
            asr_speak_confirm++;
        }
        if (asr_speak_confirm >= ASR_SPEAK_CONFIRM) {
            asr_speaking = 1;
        }
        asr_speak_silent_cnt = 0;
    } else {
        asr_speak_confirm = 0;
        if (asr_speaking) {
            if (asr_speak_silent_cnt < ASR_SPEAK_END_CNT) {
                asr_speak_silent_cnt++;      // 说话中/刚说完的静音帧
            } else {
                asr_speaking = 0;            // 连续静音足够，判定说话结束
                asr_speak_silent_cnt = 0;
            }
        }
    }

    // 全量写入缓冲
    cbuffer_t *cbuf = &asr_pcm_cbuf;
    if (0 == cbuf_write(cbuf, data, len)) {
        cbuf_clear(cbuf);   // 缓冲满，清空防止滞后
    }
    return len;
}
static const struct audio_vfs_ops mic_asr_vfs_ops = {
    .fwrite = mic_asr_fwrite,
};

// 编码器服务器事件回调：保留（仅打印诊断，说话起止不由 VAD 事件驱动）
static void asr_enc_event_handler(void *priv, int argc, int *argv)
{
    if (argc < 1) {
        return;
    }
    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_SPEAK_START:
        printf("[ASR] VAD speak start\n");
        break;
    case AUDIO_SERVER_EVENT_SPEAK_STOP:
        printf("[ASR] VAD speak stop\n");
        break;
    default:
        break;
    }
}

// 本地拷贝 ASR 结果的最大长度（防止 asr_close 清空后指针失效）
#define ASR_RESULT_MAX_LOCAL  512

/*
 * ASR 上传线程：从环形缓冲按讯飞 IAT 要求（每帧1280B/40ms）连续发送。
 *   - 说话中(asr_speaking)发送音频，首帧status=0，后续status=1
 *   - 说话结束(asr_speaking变0，由能量检测判定连续静音)发结束帧status=2
 * 缓冲是全量写入，音频连续完整；不依赖 VAD 事件（VAD 事件在本平台不可靠）。
 */
static void asr_upload_task(void *priv)
{
    int first_frame = 1;     // 是否尚未发首帧
    int end_sent = 0;        // 本轮结束帧是否已发
    printf("[ASR] upload task started\n");
    while (1) {
        // 等待环形缓冲初始化 + 本轮上传激活（由 mic_asr_start 完成）
        while (!asr_cbuf_inited || !asr_upload_active) {
            msleep(20);
        }

        // 说话中：持续把缓冲里攒够的帧发给讯飞（首帧 status=0，后续 status=1）
        if (asr_speaking) {
            while (cbuf_get_data_size(&asr_pcm_cbuf) >= ASR_UPLOAD_FRAME_SIZE) {
                cbuf_read(&asr_pcm_cbuf, asr_upload_buf, ASR_UPLOAD_FRAME_SIZE);
                if (first_frame) {
                    end_sent = 0;   // 新一轮开始，重置结束帧标志
                    int rc = asr_send(asr_upload_buf, ASR_UPLOAD_FRAME_SIZE, 0);
                    first_frame = 0;
                    printf("[ASR] first PCM frame: bytes=%u send_rc=%d\n", (unsigned int)ASR_UPLOAD_FRAME_SIZE, rc);
                } else {
                    asr_send(asr_upload_buf, ASR_UPLOAD_FRAME_SIZE, 1);  // 中间帧
                }
            }
        }

        // 说话结束(asr_speaking 变0) 或 服务器已返回最终结果：发结束帧结束本轮
        if (!first_frame && !end_sent && asr_turn_done == 0 &&
            (!asr_speaking || asr_is_done())) {
            // 把缓冲里剩余的音频发完（避免语音尾巴丢失）
            while (cbuf_get_data_size(&asr_pcm_cbuf) >= ASR_UPLOAD_FRAME_SIZE) {
                cbuf_read(&asr_pcm_cbuf, asr_upload_buf, ASR_UPLOAD_FRAME_SIZE);
                asr_send(asr_upload_buf, ASR_UPLOAD_FRAME_SIZE, 1);
            }
            printf("[ASR] send end frame\n");
            int rc = asr_send(NULL, 0, 2);
            printf("[ASR] end frame send_rc=%d\n", rc);   // 结束帧，通知讯飞本段音频结束，立即返回识别结果
            asr_turn_done = 1;
            end_sent = 1;
            printf("[DLG] speech end\n");
            first_frame = 1;
            // 本轮结束：清空缓冲、停止上传，等待 mic_asr_start 重新激活下一轮。
            // 关键：asr_turn_done 置位后主流程会 asr_close（free asr_ws），
            //       上传线程必须在此停止发送，否则会访问已释放的连接而崩溃。
            cbuf_clear(&asr_pcm_cbuf);
            asr_upload_active = 0;
            printf("[ASR] upload stopped\n");
        }
        msleep(40);
    }
}

// 打开 MIC 录音（采用官方 audio_input.c 配置：frame_size=640 + VAD 断句 + 环形缓冲）
static int mic_asr_start(void)
{
    union audio_req req = {0};

    // 首次初始化环形缓冲与信号量
    static char pre_inited = 0;
    if (!pre_inited) {
        asr_pcm_buf = malloc(ASR_CBUF_SIZE);
        if (!asr_pcm_buf) {
            printf("[DLG] cbuf malloc fail\n");
            return -1;
        }
        cbuf_init(&asr_pcm_cbuf, asr_pcm_buf, ASR_CBUF_SIZE);
        pre_inited = 1;
        asr_cbuf_inited = 1;
    }
    cbuf_clear(&asr_pcm_cbuf);
    asr_turn_done = 0;
    asr_upload_active = 1;          // 激活上传线程开始本轮
    asr_record_start_ms = timer_get_ms();   // 记录录音启动时间（抑制启动瞬态）
    asr_speaking = 0;               // 重置说话状态，避免跨轮残留
    asr_speak_silent_cnt = 0;
    asr_speak_confirm = 0;

    asr_enc_server = server_open("audio_server", "enc");
    if (!asr_enc_server) {
        asr_upload_active = 0;
        printf("[DLG] enc server open fail\n");
        return -1;
    }
    // 注册 VAD 说话起止事件
    server_register_event_handler_to_task(asr_enc_server, NULL, asr_enc_event_handler, "app_core");

    req.enc.cmd            = AUDIO_ENC_OPEN;
    req.enc.channel        = 1;
    req.enc.sample_rate    = 16000;
    req.enc.volume         = 70;   // 提高 MIC 增益，增强语音信号（原35信号avg仅1043，太弱）
    req.enc.format         = "pcm";
    req.enc.sample_source  = "mic";
    req.enc.channel_bit_map = BIT(1);   // 左 MIC 通道
    req.enc.frame_size     = ASR_ENC_FRAME_SIZE;          // 640字节 = 40ms
    req.enc.output_buf_len = ASR_ENC_FRAME_SIZE * 3;
    req.enc.vfs_ops        = &mic_asr_vfs_ops;
    req.enc.file           = NULL;
    // 开启编码器 VAD 断句（官方标准做法，识别说话起止）
    req.enc.use_vad        = 1;
    req.enc.vad_auto_refresh = 1;
    req.enc.vad_start_threshold = 300;   // ms
    req.enc.vad_stop_threshold  = 0;
    int rc = server_request(asr_enc_server, AUDIO_REQ_ENC, &req);
    if (rc != 0) {
        printf("[ASR] MIC open failed: rc=%d\n", rc);
        asr_upload_active = 0;
        server_close(asr_enc_server);
        asr_enc_server = NULL;
        return -1;
    }
    printf("[DLG] mic recording start\n");
    return 0;
}

// 关闭 MIC 录音
static void mic_asr_stop(void)
{
    union audio_req req = {0};
    if (asr_enc_server) {
        req.enc.cmd = AUDIO_ENC_STOP;
        server_request(asr_enc_server, AUDIO_REQ_ENC, &req);
        server_close(asr_enc_server);
        asr_enc_server = NULL;
    }
    cbuf_clear(&asr_pcm_cbuf);
}

// 语音对话主流程
static void voice_dialog_task(void *priv)
{
    printf("[DLG] waiting for wifi...\n");
    while (wifi_get_sta_connect_state() != WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        os_time_dly(500);
    }
    printf("[DLG] WiFi connected\n");

    while (1) {
        printf("\n========== NEW TURN ==========\n");

        char asr_text[ASR_RESULT_MAX_LOCAL];

        // ===== 监听循环：识别到有效内容才退出进入星火/TTS；识别空立即重试，随时说话随时识别 =====
        while (1) {
            if (asr_open() != 0) {
                printf("[DLG] asr open fail, retry\n");
                os_time_dly(300);
                continue;
            }

            // 重置状态机
            asr_turn_done = 0;

            printf("[DLG] >>> 请说话（说完自动识别）<<<\n");
            if (mic_asr_start() != 0) {
                asr_close();
                os_time_dly(300);
                continue;
            }
            led_state_on();   // 进入"等待语音输入"，点亮状态灯

            // 主线程：等待说话结束（上传线程检测到语音发首帧，说话完发结束帧并置 asr_turn_done）。
            // 阶段1：前 5 秒内若始终没检测到语音（用户没说话/太轻），提前重试，避免干等 30 秒。
            int speak_wait = 0;
            for (; speak_wait < 500; speak_wait++) {   // 最多等 5 秒语音
                if (asr_speaking || asr_turn_done) {
                    break;
                }
                os_time_dly(10);
            }
            // 阶段2：已检测到语音，等待说话结束（最多再等 30 秒）
            if (!asr_turn_done) {
                for (int i = 0; i < 3000 && !asr_turn_done; i++) {
                    os_time_dly(10);
                }
            }

            mic_asr_stop();

            if (!asr_turn_done) {
                printf("[DLG] no speech detected, retry listening\n");
                asr_close();
                continue;   // 立即回到监听循环重试
            }

            // 等待 ASR 返回识别结果
            printf("[DLG] waiting ASR result...\n");
            for (int i = 0; i < 50; i++) {   // 最多等 5 秒
                os_time_dly(100);
                if (asr_is_done()) break;
            }
            // 先把结果拷贝出来（asr_close 会清空 asr_result_buf）
            asr_text[0] = '\0';
            const char *text = asr_get_result();
            if (text && text[0] != '\0') {
                strncpy(asr_text, text, sizeof(asr_text) - 1);
                asr_text[sizeof(asr_text) - 1] = '\0';
            }
            // 拿到结果后立即关闭 ASR 连接（讯飞 IAT 一连接一会话，可安全释放）
            asr_close();

            if (asr_text[0] == '\0') {
                // 识别空（没说话/误触发）：立即回到监听循环重新听，不进入星火/TTS
                printf("[DLG] no ASR result, keep listening\n");
                msleep(300);   // 稍作停顿让网络栈稳定，避免频繁重建连接
                continue;
            }
            printf("[DLG] ASR result: %s\n", asr_text);
            led_state_off();   // 识别到内容，进入解析（星火/TTS），无法接收语音，熄灭状态灯
            break;   // 识别到有效内容，退出监听循环
        }

        // 用识别文字问星火
        printf("[DLG] asking spark...\n");
        if (spark_text_request_with(asr_text) != 0) {
            printf("[DLG] LLM failed, skip TTS\n");
            msleep(800);
            continue;
        }

        // TTS 合成并播放星火回答
        // 偶发的讯飞 TTS 合成失败/超时会收不到音频导致"不说话"，这里自动重试最多2次。
        printf("[DLG] TTS speak...\n");
        int tts_ok = 0;
        for (int try = 0; try < 3 && !tts_ok; try++) {
            tts_test_start();
            unsigned char *ap = NULL;
            unsigned int alen = 0;
            if (tts_get_audio(&ap, &alen) && alen > 0) {
                tts_ok = 1;
            } else {
                printf("[DLG] TTS no audio (try %d), retry\n", try + 1);
                tts_close();   // 关闭未完成的连接，再重试
                msleep(500);
            }
        }
        if (tts_ok) {
            tts_play();
        }
        tts_close();   // 播放完关闭 TTS 连接（KILL_WAIT 等线程退出后再释放，防止野指针写坏堆）

        // 给网络栈(lwip)与 mbedtls 释放资源留足时间，避免立即重建 ASR 连接时
        // 与上一个连接残留的 TLS/entropy 上下文冲突而 HEAP_MEM 崩溃。
        msleep(800);   // 停顿后进入下一轮
    }
}

// 语音对话入口：创建 ASR 上传线程和语音对话主流程线程
void ai_audio_test(void)
{
   // ASR 上传线程：负责从环形缓冲攒够1280字节发给讯飞，常驻后台（首次 cbuf 未初始化时等待）
   if (thread_fork("asr_upload_task", 8, 1024 * 8, 0, NULL, asr_upload_task, NULL) != OS_NO_ERR) {
       printf("[DLG] asr upload task fork fail\n");
   }
   if (thread_fork("voice_dialog_task", 10, 1024 * 32, 0, NULL, voice_dialog_task, NULL) != OS_NO_ERR) {
       printf("[DLG] thread fork fail\n");
   }

   while (1) {
       os_time_dly(100);
   }
}

