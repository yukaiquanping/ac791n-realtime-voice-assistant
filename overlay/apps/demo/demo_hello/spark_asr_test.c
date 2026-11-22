/*
 * spark_asr_test.c
 * 讯飞在线语音听写 ASR 测试（WebSocket + HMAC 签名，流式上传音频）
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

// 讯飞 ASR（语音听写）三要素（与 TTS 同一套）
#define ASR_APPID       ""
#define ASR_API_KEY     ""
#define ASR_API_SECRET  ""
#define ASR_HOST        "iat-api.xfyun.cn"
#define ASR_PATH        "/v2/iat"

static struct websocket_struct *asr_ws = NULL;
static int asr_opened = 0;
static int asr_done = 0;

// 识别结果文字缓冲
#define ASR_RESULT_MAX  (2048)
static char asr_result_buf[ASR_RESULT_MAX];
static int  asr_result_len = 0;

// 生成 RFC1YOUR_LOCAL_VALUE 格式的 GMT 时间
static void asr_get_rfc1YOUR_LOCAL_VALUE_date(char *buf, int buf_len)
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
static int asr_hmac_sha256(const unsigned char *key, size_t keylen,
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
static int asr_base64_encode(const unsigned char *src, size_t slen, char *dst, size_t dlen)
{
    size_t olen = 0;
    int ret = mbedtls_base64_encode((unsigned char *)dst, dlen, &olen, src, slen);
    if (ret != 0) {
        return ret;
    }
    dst[olen] = '\0';
    return 0;
}

// URL 编码
static void asr_url_encode(const char *src, char *dst, int dst_len)
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

// 从 JSON 中提取数字字段 "key":N
static int asr_json_get_int(const char *json, const char *key, int *val)
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
 * 从 ASR 返回 JSON 中提取识别文字，并【追加】到 dst 已有内容之后。
 * 识别文字在 data.result.ws[].cw[].w。
 * 关键：讯飞 IAT 的每一帧只返回"该帧新增/修正"的词（最终帧只补句尾标点），
 *       因此必须累加拼接，而不是覆盖。否则最终帧只有句号时会把之前识别的内容覆盖掉。
 * 返回本次追加的字节数。
 */
static int asr_parse_result_append(const char *json, char *dst, int dst_size, int *cur_len)
{
    if (!json || !dst || dst_size <= 0 || !cur_len) {
        return 0;
    }
    const char *key = "\"w\":\"";
    const char *p = json;
    int out = *cur_len;
    while ((p = strstr(p, key)) != NULL && out < dst_size - 1) {
        p += strlen(key);
        while (*p && *p != '"' && out < dst_size - 1) {
            dst[out++] = *p;
            p++;
        }
        p++;   // 跳过结束引号
    }
    dst[out] = '\0';
    int added = out - *cur_len;
    *cur_len = out;
    return added;
}

/*
 * 生成 ASR 的 WebSocket URL（带鉴权参数）
 */
static int asr_build_url(char *out_url, int out_url_len)
{
    char date[64];
    char signature_origin[256];
    unsigned char sig_raw[32];
    char sig_b64[128];
    char auth_origin[512];
    char authorization[768];
    char date_enc[128];
    char auth_enc[1024];

    asr_get_rfc1YOUR_LOCAL_VALUE_date(date, sizeof(date));

    snprintf(signature_origin, sizeof(signature_origin),
             "host: %s\ndate: %s\nGET %s HTTP/1.1", ASR_HOST, date, ASR_PATH);

    if (asr_hmac_sha256((unsigned char *)ASR_API_SECRET, strlen(ASR_API_SECRET),
                        (unsigned char *)signature_origin, strlen(signature_origin),
                        sig_raw) != 0) {
        return -1;
    }
    if (asr_base64_encode(sig_raw, 32, sig_b64, sizeof(sig_b64)) != 0) {
        return -2;
    }

    snprintf(auth_origin, sizeof(auth_origin),
             "api_key=\"%s\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%s\"",
             ASR_API_KEY, sig_b64);

    if (asr_base64_encode((unsigned char *)auth_origin, strlen(auth_origin),
                          authorization, sizeof(authorization)) != 0) {
        return -3;
    }

    asr_url_encode(date, date_enc, sizeof(date_enc));
    asr_url_encode(authorization, auth_enc, sizeof(auth_enc));

    snprintf(out_url, out_url_len,
             "wss://%s%s?host=%s&date=%s&authorization=%s",
             ASR_HOST, ASR_PATH, ASR_HOST, date_enc, auth_enc);

    return 0;
}

// WebSocket 接收回调
static void asr_recv_cb(u8 *buf, u32 len, u8 type, void *priv)
{
    if (!buf || len == 0) {
        return;
    }
    int code = 0;
    if (asr_json_get_int((char *)buf, "code", &code) && code != 0) {
        printf("[ASR] server rejected request: code=%d\n", code);
        asr_done = 1;
        return;
    }

    // 提取识别文字（追加拼接，避免最终帧把前面识别的内容覆盖）
    int n = asr_parse_result_append((char *)buf, asr_result_buf, sizeof(asr_result_buf), &asr_result_len);
    if (n > 0) {
        printf("[ASR] partial(+%d): %s\n", n, asr_result_buf);
    }

    // 判断是否结束：data.status == 2
    int st = 0;
    if (asr_json_get_int((char *)buf, "status", &st) && st == 2) {
        printf("[ASR] === final frame ===\n");
        asr_done = 1;
    }

    // 诊断：打印收到的原始 JSON 前 200 字符（帮助定位识别不出内容的原因）

}

// WebSocket 客户端注册
static void asr_ws_reg(struct websocket_struct *ws, char mode)
{
    memset(ws, 0, sizeof(struct websocket_struct));
    ws->_init        = websockets_client_socket_init;
    ws->_exit        = websockets_client_socket_exit;
    ws->_handshack   = webcockets_client_socket_handshack;
    ws->_send        = websockets_client_socket_send;
    ws->_recv_thread = websockets_client_socket_recv_thread;
    ws->_heart_thread = websockets_client_socket_heart_thread;
    ws->_recv_cb     = asr_recv_cb;
    ws->_recv        = NULL;
    ws->websocket_mode = mode;
}

/*
 * 打开 ASR WebSocket 连接。成功后启动收发线程。
 * 返回 0 成功。
 */
int asr_open(void)
{
    char url[2048];
    int err;

    if (asr_opened) {
        // 复用已建立的连接，重置本轮识别状态
        asr_result_len = 0;
        asr_result_buf[0] = '\0';
        asr_done = 0;
        return 0;
    }

    asr_result_len = 0;
    asr_result_buf[0] = '\0';
    asr_done = 0;

    if (asr_build_url(url, sizeof(url)) != 0) {
        printf("[ASR] build url fail\n");
        return -1;
    }

    asr_ws = malloc(sizeof(struct websocket_struct));
    if (!asr_ws) {
        return -2;
    }
    asr_ws_reg(asr_ws, WEBSOCKETS_MODE);

    asr_ws->ip_or_url = (u8 *)url;
    asr_ws->origin_str = NULL;
    asr_ws->user_agent_str = NULL;
    asr_ws->recv_time_out = 1000;

    err = websockets_struct_check(sizeof(struct websocket_struct));
    if (FALSE == err) {
        printf("[ASR] ws struct check fail\n");
        goto exit_asr;
    }

    err = asr_ws->_init(asr_ws);
    if (FALSE == err) {
        printf("[ASR] ws init error\n");
        goto exit_asr;
    }

    err = asr_ws->_handshack(asr_ws);
    if (FALSE == err) {
        printf("[ASR] ws handshake error\n");
        goto exit_asr;
    }
    printf("[ASR] ws handshake success\n");

    thread_fork("asr_ws_heart", 19, 512, 0, &asr_ws->ping_thread_id,
                asr_ws->_heart_thread, asr_ws);
    thread_fork("asr_ws_recv", 18, 512, 0, &asr_ws->recv_thread_id,
                asr_ws->_recv_thread, asr_ws);

    msleep(500);
    asr_opened = 1;
    return 0;

exit_asr:
    if (asr_ws) {
        if (asr_ws->_exit) {
            asr_ws->_exit(asr_ws);
        }
        free(asr_ws);
        asr_ws = NULL;
    }
    return -3;
}

/*
 * 发送一帧 ASR 数据。
 * status: 0=首帧(含 common+business+首段音频)，1=中间帧，2=结束帧。
 * 首帧调用：asr_send(data, len, 0)
 * 中间帧：asr_send(data, len, 1)
 * 结束帧：asr_send(NULL, 0, 2)
 */
int asr_send(const unsigned char *data, u32 len, u8 status)
{
    // 大缓冲用 static，避免在 audio_encoder 小栈线程里调用时栈溢出
    static char req[8192];
    static char audio_b64[6144];
    int req_len;

    if (!asr_ws || !asr_opened) {
        return -1;
    }

    if (status == 0) {
        // 首帧：base64 首段音频
        if (len > 0) {
            if (asr_base64_encode(data, len, audio_b64, sizeof(audio_b64)) != 0) {
                printf("[ASR] b64 fail\n");
                return -2;
            }
            req_len = snprintf(req, sizeof(req),
                "{\"common\":{\"app_id\":\"%s\"},"
                "\"business\":{\"language\":\"zh_cn\",\"domain\":\"iat\",\"accent\":\"mandarin\",\"ptt\":1},"
                "\"data\":{\"status\":0,\"format\":\"audio/L16;rate=16000\",\"encoding\":\"raw\",\"audio\":\"%s\"}}",
                ASR_APPID, audio_b64);
        } else {
            req_len = snprintf(req, sizeof(req),
                "{\"common\":{\"app_id\":\"%s\"},"
                "\"business\":{\"language\":\"zh_cn\",\"domain\":\"iat\",\"accent\":\"mandarin\",\"ptt\":1},"
                "\"data\":{\"status\":0,\"format\":\"audio/L16;rate=16000\",\"encoding\":\"raw\",\"audio\":\"\"}}",
                ASR_APPID);
        }
    } else if (status == 1) {
        if (len > 0) {
            if (asr_base64_encode(data, len, audio_b64, sizeof(audio_b64)) != 0) {
                printf("[ASR] b64 fail\n");
                return -2;
            }
            req_len = snprintf(req, sizeof(req),
                "{\"data\":{\"status\":1,\"audio\":\"%s\"}}", audio_b64);
        } else {
            return 0;
        }
    } else {   // status == 2 结束帧
        req_len = snprintf(req, sizeof(req), "{\"data\":{\"status\":2}}");
    }

    if (req_len <= 0 || req_len >= (int)sizeof(req)) {
        printf("[ASR] req too long %d\n", req_len);
        return -3;
    }

    int err = asr_ws->_send(asr_ws, (u8 *)req, req_len, WCT_TXTDATA);
    if (FALSE == err) {
        printf("[ASR] send fail\n");
        return -4;
    }
    return 0;
}

/*
 * 提供识别结果文字。返回指向内部缓冲的指针（未识别到时返回 NULL）。
 */
const char *asr_get_result(void)
{
    return asr_result_len > 0 ? asr_result_buf : NULL;
}

/*
 * 关闭 ASR 连接。
 * 注意：直接 free 会导致还在运行的 asr_ws_recv 线程访问野指针而崩溃。
 * 因此这里不释放连接，只重置识别状态，连接保持复用（与 TTS 同样的处理方式）。
 */
int asr_close(void)
{
    if (!asr_ws) {
        asr_opened = 0;
        return 0;
    }
    // 用 KILL_WAIT 阻塞等待收发线程完全退出，避免线程未退出就 free 导致堆损坏(HEAP_MEM)
    if (asr_ws->ping_thread_id) {
        thread_kill(&asr_ws->ping_thread_id, KILL_WAIT);
    }
    if (asr_ws->recv_thread_id) {
        thread_kill(&asr_ws->recv_thread_id, KILL_WAIT);
    }
    if (asr_ws->_exit) {
        asr_ws->_exit(asr_ws);
    }
    free(asr_ws);
    asr_ws = NULL;
    asr_opened = 0;
    asr_done = 0;
    asr_result_len = 0;
    asr_result_buf[0] = '\0';
    return 0;
}

int asr_is_done(void)
{
    return asr_done;
}
