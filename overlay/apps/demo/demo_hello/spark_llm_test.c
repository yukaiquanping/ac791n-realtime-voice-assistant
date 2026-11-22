#include "cJSON.h"
/*
 * spark_llm_test.c
 * 星火(Spark Lite) HTTP 文字问答测试
 * 说明：本文件故意不 include 杰理的 system/includes.h、fs.h，
 *       只 include 标准库和 curl 头，避免 stdio.h 与 fs.h 的 FILE 冲突。
 * 调用方（ai_audio_test.c）负责等待 WiFi 就绪后调用 spark_text_request()。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "curl/curl.h"

// ===== 大模型服务配置（OpenAI 兼容 HTTP 接口）=====
// 当前使用 DeepSeek。DeepSeek 官方 API 也是 OpenAI 兼容格式，
// 认证方式同样是 "Authorization: Bearer <API_KEY>"（key 为纯字符串）。
// 换成星火时：URL 改回 spark-api-open.xf-yun.com/x2/chat/completions，
//            key 用星火 APIPassword（含冒号），请求体 model 改回 "spark-x"。
#define SPARK_API_KEY   ""   // ← 替换成你的 DeepSeek API Key
#define SPARK_URL       "https://api.deepseek.com/chat/completions"

// 保存模型回答文本的全局缓冲（DeepSeek 回答可能较长，加大到 2048）
#define SPARK_ANSWER_MAX  (2048)
static char spark_answer_buf[SPARK_ANSWER_MAX];
static int  spark_answer_len = 0;

/*
 * 从星火响应 JSON 中提取 choices[0].message.content 的文本。
 * 返回提取到的 content，写入 dst（自动处理 \" \\ \n 等转义）。
 * 成功返回提取的字节数，失败返回 0。
 */
static int spark_parse_content(const char *json, char *dst, int dst_size)
{
    if (!json || !dst || dst_size <= 0) {
        return 0;
    }
    const char *key = "\"content\":\"";
    const char *p = strstr(json, key);
    if (!p) {
        return 0;
    }
    p += strlen(key);
    int out = 0;
    while (*p && out < dst_size - 1) {
        char c = *p;
        if (c == '"') {
            if (*(p - 1) == '\\') {
                // 转义的双引号，输出 "
                dst[out++] = '"';
                p++;
                continue;
            } else {
                break;   // 结束
            }
        } else if (c == '\\' && (p[1] == 'n' || p[1] == 'r' || p[1] == 't')) {
            // 转义换行/回车/制表符 -> 空格
            dst[out++] = ' ';
            p += 2;
            continue;
        } else if (c == '\\' && p[1] == '\\') {
            dst[out++] = '\\';
            p += 2;
            continue;
        } else {
            dst[out++] = c;
            p++;
        }
    }
    dst[out] = '\0';
    return out;
}

// 接收响应的回调
static size_t spark_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    char **buf = (char **)userdata;
    size_t new_len = size * nmemb;
    if (*buf == NULL) {
        *buf = malloc(new_len + 1);
        if (*buf) {
            memcpy(*buf, ptr, new_len);
            (*buf)[new_len] = 0;
        }
    } else {
        size_t cur_len = strlen(*buf);
        char *tmp = realloc(*buf, cur_len + new_len + 1);
        if (tmp) {
            *buf = tmp;
            memcpy(*buf + cur_len, ptr, new_len);
            (*buf)[cur_len + new_len] = 0;
        }
    }
    return new_len;
}

/*
 * 发送一次星火文字问答请求。
 * 前提：WiFi 已连上（由调用方负责）。
 * 返回：CURLE_OK(0) 表示请求成功，否则为 curl 错误码。
 */
/*
 * 提供最近一次星火回答的文本。供 TTS 使用。
 * 返回指向内部缓冲的指针（未收到回答时返回 NULL）。
 */
const char *spark_get_answer(void)
{
    return spark_answer_len > 0 ? spark_answer_buf : NULL;
}

/*
 * 带自定义问题的星火问答请求。
 * question: 用户问题（UTF-8）。若为 NULL 则用默认问题。
 */
int spark_text_request_with(const char *question)
{
    if (!question || question[0] == '\0') {
        question = "Please introduce yourself briefly.";
    }

    // 动态构造请求体（注意 JSON 转义，问题里不应含引号）。
    // 关键：max_tokens 控制回答长度上限。回答越长，TTS合成/接收/播报越久（之前16秒音频要等很久）。
    //       调小到 80 并提示"简短回答"，避免模型长篇大论，大幅减少"等很久才听到声音"。
    // 换模型：改 model 字段即可（如 "spark-x" 星火 / "deepseek-chat" 深度求索）。
    char body[4096];
    cJSON *root = cJSON_CreateObject();
    cJSON *messages = root ? cJSON_AddArrayToObject(root, "messages") : NULL;
    cJSON *message = messages ? cJSON_CreateObject() : NULL;
    if (!message) {
        cJSON_Delete(root);
        return -1;
    }
    cJSON *system = cJSON_CreateObject();
    if (!system) {
        cJSON_Delete(message);
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddItemToArray(messages, system);
    cJSON_AddItemToArray(messages, message);
    if (!cJSON_AddStringToObject(system, "role", "system") ||
        !cJSON_AddStringToObject(system, "content",
            "You are a spoken assistant. Reply in conversational Mandarin with "
            "one or two short, complete sentences, ideally under 40 Chinese "
            "characters. No Markdown or lists. If the question is unclear, "
            "ask one brief clarifying question. Finish your answer before stopping. "
            "You have no live data access. Never invent current weather or other live facts.")) {
        cJSON_Delete(root);
        return -1;
    }
    cJSON *thinking = cJSON_AddObjectToObject(root, "thinking");
    if (!thinking || !cJSON_AddStringToObject(thinking, "type", "disabled") ||
        !cJSON_AddStringToObject(root, "model", "deepseek-flash") ||
        !cJSON_AddStringToObject(message, "role", "user") ||
        !cJSON_AddStringToObject(message, "content", question) ||
        !cJSON_AddBoolToObject(root, "stream", 0) ||
        !cJSON_AddNumberToObject(root, "max_tokens", 256) ||
        !cJSON_PrintPreallocated(root, body, sizeof(body), 0)) {
        cJSON_Delete(root);
        printf("[LLM] JSON build failed\n");
        return -1;
    }
    cJSON_Delete(root);
    printf("[SPARK] question: %s\n", question);

    CURLcode res;
    struct curl_slist *headers = NULL;
    char *response = NULL;

    // 清空上次回答
    spark_answer_len = 0;
    spark_answer_buf[0] = '\0';

    // curl 全局资源只初始化一次（反复 global_init/cleanup 会导致内存崩溃 HEAP_MEM）
    static char curl_global_ready = 0;
    if (!curl_global_ready) {
        printf("[SPARK] curl_global_init...\n");
        curl_global_init(CURL_GLOBAL_ALL);
        curl_global_ready = 1;
        printf("[SPARK] curl_global_init done\n");
    }
    printf("[SPARK] curl_easy_init...\n");
    CURL *curl = curl_easy_init();
    printf("[SPARK] curl_easy_init done=%p\n", curl);
    if (!curl) {
        printf("[SPARK] curl_easy_init fail\n");
        return -1;
    }

    // 构造 header：Content-Type + Authorization(Bearer)
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", SPARK_API_KEY);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, SPARK_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, spark_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    // 禁用证书验证（板子上没有公网根证书，测试阶段直接跳过）
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    printf("[SPARK] POST to spark (curl_easy_perform)...\n");
    res = curl_easy_perform(curl);
    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    printf("[LLM] HTTP status=%ld\n", http_status);
    printf("[SPARK] curl_easy_perform done, res=%d\n", (int)res);
    if (res != CURLE_OK) {
        printf("[SPARK] curl_easy_perform failed: %d (%s)\n", res, curl_easy_strerror(res));
    } else {
        // 从响应中提取回答文本
        int n = spark_parse_content(response ? response : "", spark_answer_buf, sizeof(spark_answer_buf));
        if (n > 0) {
            spark_answer_len = n;
            printf("[SPARK] Answer: %s\n", spark_answer_buf);
        } else {
            printf("[SPARK] No content in response:\n%s\n", response ? response : "(null)");
        }
    }

    if (response) {
        free(response);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    // 注意：不要调用 curl_global_cleanup()。curl 全局资源在首次 global_init 后保持，
    //      反复 cleanup 会释放全局状态导致后续请求崩溃。
    printf("[SPARK] test done\n");

    return res == CURLE_OK && http_status == 200 && spark_answer_len > 0 ? 0 : -1;
}
