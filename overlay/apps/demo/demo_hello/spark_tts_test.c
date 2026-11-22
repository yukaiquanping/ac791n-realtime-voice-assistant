/*
 * spark_tts_test.c
 * 讯飞在线语音合成 TTS 测试（WebSocket + HMAC 签名）
 * 说明：本文件故意不 include 杰理的 system/includes.h、fs.h，
 *       只 include 标准库和 mbedtls/websocket 头，避免 stdio.h 与 fs.h 的 FILE 冲突。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "web_socket/websocket_api.h"
#include "cJSON.h"

// 讯飞 TTS 三要素（APPID 与星火相同）
#define TTS_APPID       ""
#define TTS_API_KEY     ""
#define TTS_API_SECRET  ""
#define TTS_HOST        "tts-api.xfyun.cn"
#define TTS_PATH        "/v2/tts"

// 从 spark_llm_test.c 获取星火回答文本
extern const char *spark_get_answer(void);

static struct websocket_struct *tts_ws = NULL;
static int tts_audio_received = 0;
static int tts_done = 0;

// 音频收集缓冲区（PCM，aue=raw 16k 单声道 16bit）
// 16k单声道16bit = 32KB/秒。512KB 可容纳约 16 秒音频，避免长回答后半段被截断"说一半"。
#define TTS_AUDIO_BUF_SIZE  (512 * 1024)
static unsigned char tts_audio_buf[TTS_AUDIO_BUF_SIZE];
static unsigned int tts_audio_len = 0;

// 生成 RFC1YOUR_LOCAL_VALUE 格式的 GMT 时间
static void get_rfc1YOUR_LOCAL_VALUE_date(char *buf, int buf_len)
{
    time_t t = time(NULL);
    struct tm tmv;
    struct tm *tp = gmtime(&t);
    if (tp) {
        tmv = *tp;
    } else {
        tmv.tm_year = 126; tmv.tm_mon = 7; tmv.tm_mday = 6;
        tmv.tm_hour = 3; tmv.tm_min = 21; tmv.tm_sec = 2; tmv.tm_wday = 4;
    }
    char wday[][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char mon[][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    snprintf(buf, buf_len, "%s, %02d %s %04d %02d:%02d:%02d GMT",
             wday[tmv.tm_wday], tmv.tm_mday, mon[tmv.tm_mon],
             tmv.tm_year + 1900, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

// HMAC-SHA256
static int hmac_sha256(const unsigned char *key, size_t keylen,
                       const unsigned char *data, size_t datalen,
                       unsigned char output[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) {
        return -1;
    }
    return mbedtls_md_hmac(md, key, keylen, data, datalen, output);
}

// base64 编码
static int base64_encode(const unsigned char *src, size_t slen, char *dst, size_t dlen)
{
    size_t olen = 0;
    int ret = mbedtls_base64_encode((unsigned char *)dst, dlen, &olen, src, slen);
    if (ret != 0) {
        return ret;
    }
    dst[olen] = '\0';
    return 0;
}

// URL 编码（只处理 TTS URL 需要的字符）
static void url_encode(const char *src, char *dst, int dst_len)
{
    static const char hex[] = "0YOUR_LOCAL_VALUE9ABCDEF";
    int j = 0;
    for (int i = 0; src[i] && j < dst_len - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0x0F];
        }
    }
    dst[j] = '\0';
}

// base64 解码，返回解码后字节数
static int base64_decode(const char *src, unsigned char *dst, int dlen)
{
    size_t olen = 0;
    int ret = mbedtls_base64_decode(dst, dlen, &olen, (const unsigned char *)src, strlen(src));
    if (ret != 0) {
        return 0;
    }
    return (int)olen;
}

// 从 JSON 中提取数字字段 "key":N
static int json_get_int(const char *json, const char *key, int *val)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return 0;
    }
    p += strlen(pat);
    *val = atoi(p);
    return 1;
}

/*
 * 生成 TTS 的 WebSocket URL（带鉴权参数，URL 编码）
 */
static int tts_build_url(char *out_url, int out_url_len)
{
    char date[64];
    char signature_origin[256];
    unsigned char sig_raw[32];
    char sig_b64[128];
    char auth_origin[512];
    char authorization[768];
    char date_enc[128];
    char auth_enc[1024];

    get_rfc1YOUR_LOCAL_VALUE_date(date, sizeof(date));
    printf("[TTS] date: %s\n", date);

    // signature_origin
    snprintf(signature_origin, sizeof(signature_origin),
             "host: %s\ndate: %s\nGET %s HTTP/1.1", TTS_HOST, date, TTS_PATH);

    if (hmac_sha256((unsigned char *)TTS_API_SECRET, strlen(TTS_API_SECRET),
                    (unsigned char *)signature_origin, strlen(signature_origin),
                    sig_raw) != 0) {
        printf("[TTS] hmac fail\n");
        return -1;
    }
    if (base64_encode(sig_raw, 32, sig_b64, sizeof(sig_b64)) != 0) {
        printf("[TTS] base64 sig fail\n");
        return -2;
    }

    snprintf(auth_origin, sizeof(auth_origin),
             "api_key=\"%s\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%s\"",
             TTS_API_KEY, sig_b64);
    printf("[TTS] auth_origin: %s\n", auth_origin);

    if (base64_encode((unsigned char *)auth_origin, strlen(auth_origin),
                      authorization, sizeof(authorization)) != 0) {
        printf("[TTS] base64 auth fail\n");
        return -3;
    }

    // URL 编码 date 和 authorization
    url_encode(date, date_enc, sizeof(date_enc));
    url_encode(authorization, auth_enc, sizeof(auth_enc));

    snprintf(out_url, out_url_len,
             "wss://%s%s?host=%s&date=%s&authorization=%s",
             TTS_HOST, TTS_PATH, TTS_HOST, date_enc, auth_enc);

    return 0;
}

// WebSocket 接收回调
static void tts_recv_cb(u8 *buf, u32 len, u8 type, void *priv)
{
    if (!buf || len == 0) {
        return;
    }

    // 解析 JSON：提取 audio(base64) 和 status
    char *audio_b64 = NULL;

    // 提取 code（成功=0）
    int code = 0;
    json_get_int((char *)buf, "code", &code);

    // 动态提取 audio 字段（base64 音频可能较大）
    char pat[32];
    snprintf(pat, sizeof(pat), "\"audio\":\"");
    const char *p = strstr((char *)buf, pat);
    if (p) {
        p += strlen(pat);
        int b64_len = 0;
        while (p[b64_len] && p[b64_len] != '"') {
            b64_len++;
        }
        if (b64_len > 0) {
            audio_b64 = malloc(b64_len + 1);
            if (audio_b64) {
                memcpy(audio_b64, p, b64_len);
                audio_b64[b64_len] = '\0';

                // 解码 base64 音频并拼接
                int dec_cap = (b64_len / 4) * 3 + 4;
                unsigned char *dec = malloc(dec_cap);
                if (dec) {
                    int dec_len = base64_decode(audio_b64, dec, dec_cap);
                    if (dec_len > 0 && tts_audio_len + dec_len <= TTS_AUDIO_BUF_SIZE) {
                        memcpy(tts_audio_buf + tts_audio_len, dec, dec_len);
                        tts_audio_len += dec_len;
                    }
                    free(dec);
                }
                free(audio_b64);
                tts_audio_received = 1;
            }
        }
    }

    // 解析 status 判断结束（2=结束）
    int st = 0;
    if (json_get_int((char *)buf, "status", &st) && st == 2) {
        printf("[TTS] === final frame (status=2) ===\n");
        tts_done = 1;
    }
}

// WebSocket 客户端注册
static void tts_ws_reg(struct websocket_struct *ws, char mode)
{
    memset(ws, 0, sizeof(struct websocket_struct));
    ws->_init        = websockets_client_socket_init;
    ws->_exit        = websockets_client_socket_exit;
    ws->_handshack   = webcockets_client_socket_handshack;
    ws->_send        = websockets_client_socket_send;
    ws->_recv_thread = websockets_client_socket_recv_thread;
    ws->_heart_thread = websockets_client_socket_heart_thread;
    ws->_recv_cb     = tts_recv_cb;
    ws->_recv        = NULL;
    ws->websocket_mode = mode;
}

/*
 * 安全关闭 TTS websocket 连接。
 * 参照官方讯飞 e2e_demo.c 的关闭流程：
 *   thread_kill(ping) -> thread_kill(recv) -> _exit -> free
 * 必须在每次 tts_test_start() 前后调用，避免连接/线程泄漏导致 [HEAP_MEM] 崩溃。
 */
void tts_close(void)
{
    if (!tts_ws) {
        tts_done = 0;
        tts_audio_len = 0;
        return;
    }
    // 关键：必须用 KILL_WAIT（阻塞等待线程彻底退出）再 free，
    // 否则 recv/heart 线程在 free 后仍在访问 tts_ws->websockets_mbtls_info
    // （含 mbedtls 的 entropy/ssl 上下文），造成野指针写、破坏堆(HEAP_MEM)，
    // 下一次 ASR/TTS 重新建连握手时崩溃。
    if (tts_ws->ping_thread_id) {
        thread_kill(&tts_ws->ping_thread_id, KILL_WAIT);
    }
    if (tts_ws->recv_thread_id) {
        thread_kill(&tts_ws->recv_thread_id, KILL_WAIT);
    }
    if (tts_ws->_exit) {
        tts_ws->_exit(tts_ws);
    }
    free(tts_ws);
    tts_ws = NULL;
    tts_done = 0;
    tts_audio_len = 0;
}

/*
 * TTS 测试主流程：
 *  1. 生成鉴权 URL
 *  2. WebSocket(wss) 连接 + 握手
 *  3. 发送 TTS 请求 JSON
 *  4. 接收音频（回调打印）
 */
void tts_test_start(void)
{
    char url[2048];
    int err;

    printf("[TTS] === start ===\n");
    // 若上次连接未关闭，先安全关闭（防止内存泄漏/崩溃）
    if (tts_ws) {
        printf("[TTS] close previous ws first\n");
        tts_close();
    }
    // 重置全局状态
    tts_audio_len = 0;
    tts_audio_received = 0;
    tts_done = 0;

    if (tts_build_url(url, sizeof(url)) != 0) {
        printf("[TTS] build url fail\n");
        return;
    }

    tts_ws = malloc(sizeof(struct websocket_struct));
    if (!tts_ws) {
        printf("[TTS] malloc fail\n");
        return;
    }
    // wss 加密模式
    tts_ws_reg(tts_ws, WEBSOCKETS_MODE);

    // 设置 URL 和超时
    tts_ws->ip_or_url = (u8 *)url;
    tts_ws->origin_str = NULL;
    tts_ws->user_agent_str = NULL;
    tts_ws->recv_time_out = 1000;

    // 结构体版本检测
    err = websockets_struct_check(sizeof(struct websocket_struct));
    if (FALSE == err) {
        printf("[TTS] ws struct check fail\n");
        goto exit_tts;
    }

    // 初始化（建立 socket）
    err = tts_ws->_init(tts_ws);
    if (FALSE == err) {
        printf("[TTS] ws init error, err=%d\n", err);
        goto exit_tts;
    }
    printf("[TTS] ws init ok\n");

    // 握手
    err = tts_ws->_handshack(tts_ws);
    if (FALSE == err) {
        printf("[TTS] ws handshake error\n");
        goto exit_tts;
    }
    printf("[TTS] ws handshake success\n");

    // 创建收发线程
    thread_fork("tts_ws_heart", 19, 512, 0, &tts_ws->ping_thread_id,
                tts_ws->_heart_thread, tts_ws);
    thread_fork("tts_ws_recv", 18, 512, 0, &tts_ws->recv_thread_id,
                tts_ws->_recv_thread, tts_ws);

    // 等待连接稳定
    msleep(1000);

    // 取要合成的文本：优先用星火回答，否则用默认"你好"
    const char *text = spark_get_answer();
    if (!text || text[0] == '\0') {
        text = "你好";
    }
    printf("[TTS] text to speak: %s\n", text);

    // 文本 base64 编码（UTF-8）。预留足够空间给较长回答。
    int text_len = strlen(text);
    char text_b64[2048];
    if (base64_encode((unsigned char *)text, text_len, text_b64, sizeof(text_b64)) != 0) {
        printf("[TTS] base64 encode fail\n");
        return;
    }
    char tts_req[4096];
    // 用 aue=raw 返回标准 16bit PCM（16k 单声道），便于播放和导出验证。
    // auf 指定采样率。
    snprintf(tts_req, sizeof(tts_req),
             "{\"common\":{\"app_id\":\"%s\"},"
             "\"business\":{\"vcn\":\"xiaoyan\",\"aue\":\"raw\",\"auf\":\"audio/L16;rate=16000\",\"tte\":\"UTF8\"},"
             "\"data\":{\"text\":\"%s\",\"status\":2}}",
             TTS_APPID, text_b64);

    err = tts_ws->_send(tts_ws, (u8 *)tts_req, strlen(tts_req), WCT_TXTDATA);
    if (FALSE == err) {
        printf("[TTS] send fail\n");
        goto exit_tts;
    }
    printf("[TTS] req sent, waiting response...\n");

    // 等待接收完成（status=2 结束帧）或超时。
    // 回答已限制较短（max_tokens=80），正常情况下几秒内即可合成完。若超时说明本次合成异常，
    // 主流程会据此自动重试。这里等 10 秒，避免卡太久，也让重试能更快触发。
    int tts_wait_timeout = 0;
    for (int i = 0; i < 100; i++) {   // 最多等 10 秒
        msleep(100);
        if (tts_done) {
            break;
        }
    }
    if (!tts_done) {
        tts_wait_timeout = 1;
        printf("[TTS] WARN: receive timeout, audio incomplete (len=%u)\n", tts_audio_len);
    }

    printf("[TTS] === receive complete ===\n");
    printf("[TTS] total PCM len = %u bytes (timeout=%d)\n", tts_audio_len, tts_wait_timeout);
    printf("[TTS] audio duration ~ %u ms (16k mono 16bit)\n", tts_audio_len / 2 / 16);
    printf("[TTS] done (audio_received=%d, done=%d)\n", tts_audio_received, tts_done);

    // 注意：不要在这里强制 kill 线程 / exit WebSocket，
    // 否则可能导致 tcpip 线程卡死 + watchdog 超时崩溃。
    // 连接保持运行，等后续播放阶段再统一管理。
    return;

exit_tts:
    // 仅在连接失败等异常路径才清理
    if (tts_ws) {
        if (tts_ws->_exit) {
            tts_ws->_exit(tts_ws);
        }
        free(tts_ws);
        tts_ws = NULL;
    }
}

/*
 * 提供给外部播放：返回 TTS 收到的 PCM 音频指针和长度
 * 返回 1 有音频，0 无。
 */
int tts_get_audio(unsigned char **buf, unsigned int *len)
{
    if (buf) {
        *buf = tts_audio_buf;
    }
    if (len) {
        *len = tts_audio_len;
    }
    return tts_audio_len > 0 ? 1 : 0;
}
