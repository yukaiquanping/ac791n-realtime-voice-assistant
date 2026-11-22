#include "hello_realtime_voice.h"

#if HELLO_REALTIME_VOICE_ENABLE
#include "system/includes.h"
#include "wifi/wifi_connect.h"
#include "mbedtls/base64.h"
#include "audio_input.h"
#include "onesdk.h"
#include "volc_ca.h"

#ifdef CONFIG_UI_ENABLE
extern void chat_ui_append(const char *who, const char *text);
extern void chat_ui_set_status(const char *status);
extern void chat_ui_begin_turn(void);
extern void chat_ui_append_delta(const char *text, size_t len);
#else
#define chat_ui_set_status(status) ((void)0)
#define chat_ui_begin_turn() ((void)0)
#endif

#define MIC_PACKET_BYTES 3200
#define RESPONSE_TIMEOUT_MS 30000
#define REPLY_WAIT_MS 20000
#define LISTEN_GATE_PEAK 1100
#define BARGE_IN_GATE_PEAK 1600
#define BARGE_IN_GATE_MEAN 230
// 16 kHz / 16 bit mono. Require 160 ms of qualifying PCM before interrupting.
#define BARGE_IN_CONFIRM_BYTES 5120

static iot_basic_config_t device_config = {
    .http_host = HELLO_IOT_HTTP_HOST,
    .instance_id = HELLO_IOT_INSTANCE_ID,
    .auth_type = ONESDK_AUTH_DYNAMIC_NO_PRE_REGISTERED,
    .product_key = HELLO_IOT_PRODUCT_KEY,
    .product_secret = HELLO_IOT_PRODUCT_SECRET,
    .device_name = HELLO_IOT_DEVICE_NAME,
    .verify_ssl = true,
};

static onesdk_config_t config = {
    .device_config = &device_config,
    .hardware_agent_url = "wss://ai-gateway.vei.volces.com",
    .aigw_path = "/v1/realtime?bot=" HELLO_IOT_BOT_ID "&wait_for_session_update=true",
    .send_ping = true,
};

static onesdk_ctx_t voice;
static OS_SEM websocket_exited;
static volatile bool stop_websocket;
static volatile bool reply_done;
static volatile bool session_created;
static volatile bool session_updated;
static bool first_audio = true;
static volatile bool audio_in_turn;      // 本轮是否已经开始收到回复音频
static volatile u32 last_audio_ms;       // 最后一次收到回复音频的时间
static unsigned char mic_pcm[MIC_PACKET_BYTES] __attribute__((aligned(2)));
// Keep the onset through VAD and interruption confirmation.
#define PRE_ROLL_BYTES 20480
static unsigned char pre_pcm[PRE_ROLL_BYTES];
static unsigned int pre_len;

static void pre_roll_push(const void *data, unsigned int len)
{
    if (!len) {
        return;
    }
    if (len >= PRE_ROLL_BYTES) {
        memcpy(pre_pcm, (const unsigned char *)data + (len - PRE_ROLL_BYTES), PRE_ROLL_BYTES);
        pre_len = PRE_ROLL_BYTES;
        return;
    }
    if (pre_len + len > PRE_ROLL_BYTES) {
        unsigned int drop = pre_len + len - PRE_ROLL_BYTES;
        memmove(pre_pcm, pre_pcm + drop, pre_len - drop);
        pre_len -= drop;
    }
    memcpy(pre_pcm + pre_len, data, len);
    pre_len += len;
}
static unsigned char mic_base64[4 * ((MIC_PACKET_BYTES + 2) / 3) + 1];

static void receive_audio(const void *data, size_t len, void *arg)
{
    size_t decoded_len = 0;
    if (!data || !len) {
        return;
    }
    unsigned char *pcm = malloc(len);
    if (!pcm) {
        printf("[RT] no memory for reply audio\n");
        return;
    }
    if (!mbedtls_base64_decode(pcm, len, &decoded_len, data, len)) {
        audio_in_turn = true;
        last_audio_ms = timer_get_ms();
        if (first_audio) {
            printf("[RT] received reply audio\n");
            chat_ui_set_status("正在播报");
            first_audio = false;
        }
        for (size_t offset = 0; offset < decoded_len;) {
            unsigned int written = audio_stream_write_available(pcm + offset, decoded_len - offset);
            offset += written;
            if (!written) {
                if (!is_audio_play_open() || stop_websocket) {
                    break;
                }
                os_time_dly(1);
            }
        }
    } else {
        printf("[RT] invalid reply audio\n");
    }
    free(pcm);
}

#ifdef CONFIG_UI_ENABLE
static void receive_text_delta(const void *data, size_t len, void *arg)
{
    chat_ui_append_delta(data, len);
}
#endif

static void receive_text(const void *data, size_t len, void *arg)
{
    if (data && len) {
        printf("[RT] reply text: %.*s\n", (int)len, (const char *)data);
#ifdef CONFIG_UI_ENABLE
        chat_ui_append("AI", (const char *)data);
#endif
    }
}

static void receive_user_text(const void *data, size_t len, void *arg)
{
    if (data && len) {
        printf("[RT] user text: %.*s\n", (int)len, (const char *)data);
#ifdef CONFIG_UI_ENABLE
        chat_ui_append("我", (const char *)data);
#endif
    }
}

static void finish_reply(void *arg)
{
    first_audio = true;
    reply_done = true;
    printf("[RT] cloud response complete\n");
}

static void receive_error(const char *code, const char *message, void *arg)
{
    printf("[RT] cloud error: %s %s\n", code, message);
}

static void session_created_cb(void *arg)
{
    session_created = true;
    printf("[RT] session.created\n");
}

static void session_updated_cb(void *arg)
{
    session_updated = true;
    printf("[RT] session.updated\n");
}

static onesdk_rt_event_cb_t callbacks = {
    .on_audio = receive_audio,
    .on_text = receive_text,
#ifdef CONFIG_UI_ENABLE
    .on_transcript_text = receive_text_delta,
#endif
    .on_response_done = finish_reply,
    .on_error = receive_error,
    .on_session_created = session_created_cb,
    .on_session_updated = session_updated_cb,
    .on_user_text = receive_user_text,
};

static void websocket_task(void *arg)
{
    u32 tick = 0;
    while (!stop_websocket) {
        if (aigw_ws_run_event_loop(voice.aigw_ws_ctx, 0) < 0) {
            break;
        }
        // 关键：发送队列必须由事件循环线程自己触发 writable。
        // 业务线程里的 lws_callback_on_writable() 在这个 lws 移植版上不一定能唤醒服务循环，
        // 表现为 session.update/音频全部堆在 ring 里发不出去（日志上只剩一句 No message to send）。
        // 空闲时也要周期性触发，好让写回调有机会发 WebSocket PING，避免网关 20s 空闲超时断开。
        aigw_ws_ctx_t *ws = voice.aigw_ws_ctx;
        if (ws && ws->connected && ws->active_conn) {
            int pending = lws_ring_get_element(ws->send_ring, &ws->tail) != NULL;
            if (pending || (++tick % 20) == 0) {
                lws_callback_on_writable(ws->active_conn);
            }
        }
        os_time_dly(1);
    }
    stop_websocket = true;
    os_sem_post(&websocket_exited);
}

static unsigned int packet_peak(const void *buf, unsigned int len, unsigned int *mean)
{
    int peak = 0;
    unsigned int total = 0;
    const short *s = (const short *)buf;
    for (unsigned int i = 0; i < len / 2; i++) {
        int v = s[i] < 0 ? -s[i] : s[i];
        total += v;
        if (v > peak) {
            peak = v;
        }
    }
    if (mean) {
        *mean = len >= 2 ? total / (len / 2) : 0;
    }
    return (unsigned int)peak;
}

static int send_pcm_data(const unsigned char *pcm, unsigned int length, bool commit)
{
    size_t encoded_len;
    int ret = mbedtls_base64_encode(mic_base64, sizeof(mic_base64),
                                    &encoded_len, pcm, length);
    if (ret) {
        return ret;
    }
    return onesdk_rt_audio_send(&voice, (const char *)mic_base64, encoded_len, commit);
}

static int send_pcm(unsigned int length, bool commit)
{
    return send_pcm_data(mic_pcm, length, commit);
}

static void run_dialog(void)
{
    bool sending = false;
    bool waiting = false;
    u32 submitted_at = 0;
    unsigned int turn_peak = 0;
    unsigned int dbg_pkt = 0;
    unsigned int barge_in_bytes = 0;
    chat_ui_set_status("请说话");
    printf("[RT] ready, speak toward the board MIC\n");
    while (!stop_websocket && voice.aigw_ws_ctx->connected &&
           wifi_get_sta_connect_state() == WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        int length = _device_get_voice_data(mic_pcm, sizeof(mic_pcm));
        const bool speaking = get_recoder_state() != 0;

        if (waiting) {
            if (!speaking) {
                barge_in_bytes = 0;
            } else if (length > 0) {
                unsigned int mean;
                const unsigned int peak = packet_peak(mic_pcm, length, &mean);
                if (peak >= BARGE_IN_GATE_PEAK && mean >= BARGE_IN_GATE_MEAN) {
                    barge_in_bytes += length;
                } else {
                    barge_in_bytes = 0;
                }
            }
            const u32 now = timer_get_ms();
            const bool drained = !audio_stream_pending_playback();
            // 回复结束的判断：不一定能收到 response.done，所以“音频停了 1.2s 且已播完”也算结束
            const bool audio_idle = audio_in_turn && drained && (now - last_audio_ms > 1200);
            if (drained && (reply_done || audio_idle)) {
                waiting = false;
                chat_ui_set_status("请说话");
                printf("[RT] ready for next turn\n");
            } else if (barge_in_bytes >= BARGE_IN_CONFIRM_BYTES) {
                // 打断：用户一开口就掐掉正在播/正在生成的回复，立刻开始新一轮
                if (!reply_done) {
                    onesdk_rt_audio_response_cancel(&voice);
                }
                audio_stream_clear_playback();
                first_audio = true;
                waiting = false;
                chat_ui_set_status("正在听");
                printf("[RT] barge-in, start new turn (恢复前置 %u 字节)\n", pre_len);
                // 先把打断前缓存的音频补发上去，否则用户开口的第一个字会被 VAD 延迟吃掉
                for (unsigned int off = 0; off < pre_len;) {
                    unsigned int n = pre_len - off;
                    if (n > MIC_PACKET_BYTES) {
                        n = MIC_PACKET_BYTES;
                    }
                    if (send_pcm_data(pre_pcm + off, n, false)) {
                        break;
                    }
                    off += n;
                }
                pre_len = 0;
            } else if (timer_get_ms() - submitted_at > REPLY_WAIT_MS) {
                // 迟迟没有回复：回到监听，不要一直死等（旧逻辑等 30s 直接把对话拖死）
                waiting = false;
                pre_len = 0;
                printf("[RT] no reply yet, back to listening\n");
            } else {
                // 还在等回复：把这段音频留着，别丢
                pre_roll_push(mic_pcm, length);
                continue;
            }
        }

        if (!speaking && sending && !length) {
            length = audio_stream_read_available(mic_pcm, sizeof(mic_pcm));
        }
        if (length > 0 && (speaking || sending)) {
            const unsigned int peak = packet_peak(mic_pcm, length, NULL);
            if (!sending && peak < LISTEN_GATE_PEAK) {
                // 底噪/杂音，不开这一轮。已经在发的轮次不做判断，避免把话尾切掉。
                continue;
            }
            if (!sending) {
                chat_ui_begin_turn();
                reply_done = false;
                audio_in_turn = false;
                turn_peak = 0;
                pre_len = 0;
                ++dbg_pkt;
                chat_ui_set_status("正在听");
                printf("[RT] sending microphone PCM (start peak=%u)\n", peak);
            }
            if (peak > turn_peak) {
                turn_peak = peak;
            }
            if ((dbg_pkt++ % 8) == 0) {
                printf("[RT] mic len=%u peak=%u\n", length, peak);
            }
            if (send_pcm(length, !speaking)) {
                printf("[RT] upload dropped (queue full)\n");
            }
            sending = true;
        } else if (sending && !speaking) {
            // 先把缓冲里剩的音频发完（原来会丢掉话尾），再补 100ms 静音帮云端断句，最后提交
            unsigned int rest;
            while ((rest = audio_stream_read_available(mic_pcm, sizeof(mic_pcm))) > 0) {
                if (send_pcm(rest, false)) {
                    break;
                }
            }
            memset(mic_pcm, 0, MIC_PACKET_BYTES);
            if (send_pcm(MIC_PACKET_BYTES, true)) {
                printf("[RT] commit dropped (queue full)\n");
            }
        }
        if (sending && !speaking) {
            submitted_at = timer_get_ms();
            sending = false;
            waiting = true;
            barge_in_bytes = 0;
            chat_ui_set_status("正在思考");
            printf("[RT] turn submitted (max peak=%u)\n", turn_peak);
        }
    }
    printf("[RT] dialog ended\n");
}

static void voice_run_once(void)
{
    device_config.ssl_ca_cert = digicert_global_root_g2;
    int ret = onesdk_init(&voice, &config);
    if (ret) {
        printf("[RT] OneSDK init failed: %d\n", ret);
        return;
    }
    onesdk_rt_set_event_cb(&voice, &callbacks);
    ret = onesdk_connect(&voice);
    if (!ret && os_sem_create(&websocket_exited, 0) == OS_NO_ERR) {
        if (thread_fork("hello_rt_ws", 9, 8192, 0, NULL, websocket_task, NULL) == OS_NO_ERR) {
            for (int i = 0; i < 100 && !stop_websocket && !voice.aigw_ws_ctx->connected; ++i) {
                os_time_dly(10);
            }
            if (voice.aigw_ws_ctx->connected) {
                const u32 connected_at = timer_get_ms();
                while (!session_created && !stop_websocket &&
                       voice.aigw_ws_ctx->connected && timer_get_ms() - connected_at < 1500) {
                    os_time_dly(2);
                }
                ret = !stop_websocket && voice.aigw_ws_ctx->connected ?
                    aigw_ws_send_request(voice.aigw_ws_ctx,
                    "{\"event_id\":\"hello-session-1\",\"type\":\"session.update\","
                    "\"session\":{\"object\":\"realtime.session\",\"input_audio_format\":\"pcm16\","
                    "\"output_audio_format\":\"pcm16\"}}") : VOLC_ERR_CONNECT;
                printf("[RT] session.update sent: %d (created=%d updated=%d)\n",
                       ret, session_created, session_updated);
                if (!ret) {
                    os_time_dly(20);
                    audio_stream_init(16000, 16, 1);
                    start_audio_stream();
                    run_dialog();
                    stop_audio_stream();
                }
            } else {
                printf("[RT] session setup failed (connected=%d rx=%u)\n",
                       voice.aigw_ws_ctx->connected, voice.aigw_ws_ctx->rx_callbacks);
            }
            stop_websocket = true;
            lws_cancel_service(voice.aigw_ws_ctx->lws_ctx);
            os_sem_pend(&websocket_exited, 0);
        } else {
            printf("[RT] WebSocket task could not start\n");
        }
        os_sem_del(&websocket_exited, OS_DEL_ALWAYS);
    } else {
        printf("[RT] WebSocket setup failed: %d\n", ret);
    }
    onesdk_deinit(&voice);
}

static void voice_task(void *arg)
{
    if (!device_config.http_host[0] || !device_config.instance_id[0] ||
        !device_config.product_key[0] || !device_config.product_secret[0] ||
        !device_config.device_name[0]) {
        printf("[RT] fill in hello_realtime_voice.h first\n");
        return;
    }
    printf("[RT] waiting for Wi-Fi\n");
    while (wifi_get_sta_connect_state() != WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        os_time_dly(30);
    }
    while (1) {
        chat_ui_set_status("正在连接");
        voice_run_once();
        chat_ui_set_status("正在重连");
        // 连接被网关断开后自动重连，否则一旦掉线语音就彻底没反应了
        stop_websocket = false;
        session_created = false;
        session_updated = false;
        reply_done = false;
        audio_in_turn = false;
        first_audio = true;
        printf("[RT] session closed, reconnect in 2s\n");
        os_time_dly(200);
    }
}

void hello_realtime_voice_start(void)
{
    if (thread_fork("hello_rt_voice", 8, 16384, 0, NULL, voice_task, NULL) != OS_NO_ERR) {
        printf("[RT] voice task could not start\n");
    }
}
#endif
