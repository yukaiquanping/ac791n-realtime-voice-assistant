//
// Created by lhk on 2025/2/26.
//
#include "infer_realtime_ws.h"
#define ENABLE_AI_REALTIME

#include "cJSON.h"
#include "onesdk.h"
#include "error_code.h"

void rt_recv_cb(const char *message, size_t len, void *userdata)
{
    onesdk_ctx_t *ctx = userdata;

    if (!len || len >= SIZE_MAX - ctx->rt_json_buf_size) {
        return;
    }
    size_t required = ctx->rt_json_buf_size + len + 1;
    if (ctx->rt_json_buf_capacity < required) {
        size_t capacity = required <= SIZE_MAX / 2 ? required * 2 : required;
        char *new_buf = realloc(ctx->rt_json_buf, capacity);
        if (!new_buf) {
            printf("[RT] cannot allocate event buffer\n");
            return;
        }
        ctx->rt_json_buf = new_buf;
        ctx->rt_json_buf_capacity = capacity;
    }
    memcpy(ctx->rt_json_buf + ctx->rt_json_buf_size, message, len);
    ctx->rt_json_buf_size += len;
    ctx->rt_json_buf[ctx->rt_json_buf_size] = '\0';

    // 尝试解析完整JSON
    // printf("尝试解析的JSON数据: %.*s\n", (int)ctx->rt_json_buf_size, ctx->rt_json_buf);
    cJSON *root = cJSON_ParseWithLengthOpts(ctx->rt_json_buf, ctx->rt_json_buf_size + 1, (const char **) & (ctx->rt_json_pos), 0);

    while (root) {
        cJSON *type_item = cJSON_GetObjectItem(root, "type");
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        cJSON *transcript = cJSON_GetObjectItem(root, "transcript");
        cJSON *text_item = cJSON_GetObjectItem(root, "text");

        if (cJSON_IsString(type_item) && ctx->rt_event_cb &&
            strcmp(type_item->valuestring, "session.created") == 0) {
            if (ctx->rt_event_cb->on_session_created) {
                ctx->rt_event_cb->on_session_created(ctx->user_data);
            }
        } else if (cJSON_IsString(type_item) && ctx->rt_event_cb &&
                   strcmp(type_item->valuestring, "session.updated") == 0) {
            if (ctx->rt_event_cb->on_session_updated) {
                ctx->rt_event_cb->on_session_updated(ctx->user_data);
            }
        } else if (cJSON_IsString(type_item)) {
            const char *type = type_item->valuestring;

            if (ctx->rt_event_cb) {
                if (strcmp(type, "response.created") == 0) {
                    ctx->rt_text_stream = 0;
                    ctx->rt_text_len = 0;
                } else if (strcmp(type, "response.audio.delta") == 0 && delta) {
                    // 音频数据回调
                    if (ctx->rt_event_cb->on_audio) {
                        ctx->rt_event_cb->on_audio(
                            delta->valuestring,
                            cJSON_IsString(delta) ? strlen(delta->valuestring) : 0,
                            ctx->user_data
                        );
                    }
                } else if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0 && transcript) {
                    // 用户说话的识别结果（ASR）
                    if (ctx->rt_event_cb->on_user_text) {
                        ctx->rt_event_cb->on_user_text(
                            transcript->valuestring,
                            cJSON_IsString(transcript) ? strlen(transcript->valuestring) : 0,
                            ctx->user_data
                        );
                    }
                } else if ((strcmp(type, "response.audio_transcript.delta") == 0 ||
                            strcmp(type, "response.output_text.delta") == 0) && delta &&
                           cJSON_IsString(delta)) {
                    const unsigned char stream = !strcmp(type, "response.audio_transcript.delta") ? 1 : 2;
                    if (ctx->rt_text_stream && ctx->rt_text_stream != stream) {
                        goto next_event;
                    }
                    ctx->rt_text_stream = stream;
                    size_t n = strlen(delta->valuestring);
                    if (ctx->rt_event_cb->on_transcript_text) {
                        ctx->rt_event_cb->on_transcript_text(delta->valuestring, n, ctx->user_data);
                    }
                    if (ctx->rt_text_len + n < sizeof(ctx->rt_text_buf)) {
                        memcpy(ctx->rt_text_buf + ctx->rt_text_len, delta->valuestring, n);
                        ctx->rt_text_len += n;
                        ctx->rt_text_buf[ctx->rt_text_len] = 0;
                    }
                } else if (strcmp(type, "response.output_text.done") == 0 && text_item &&
                           cJSON_IsString(text_item)) {
                    if (ctx->rt_text_stream != 1 && ctx->rt_event_cb->on_text) {
                        ctx->rt_event_cb->on_text(text_item->valuestring,
                                                  strlen(text_item->valuestring), ctx->user_data);
                    }
                    if (ctx->rt_text_stream != 1) {
                        ctx->rt_text_len = 0;
                    }
                } else if (strcmp(type, "response.audio_transcript.done") == 0 && transcript) {
                    // 文本转录完成回调
                    if (ctx->rt_text_stream != 2 && ctx->rt_event_cb->on_text) {
                        ctx->rt_event_cb->on_text(
                            transcript->valuestring,
                            cJSON_IsString(transcript) ? strlen(transcript->valuestring) : 0,
                            ctx->user_data
                        );
                    }
                    if (ctx->rt_text_stream != 2) {
                        ctx->rt_text_len = 0;
                    }
                } else if (strcmp(type, "response.audio_translation.delta") == 0 && delta) {
                    if (ctx->rt_event_cb->on_translation_text) {
                        ctx->rt_event_cb->on_translation_text(
                            delta->valuestring,
                            cJSON_IsString(delta) ? strlen(delta->valuestring) : 0,
                            ctx->user_data
                        );
                    }
                } else if (strcmp(type, "response.done") == 0) {
                    if (ctx->rt_text_len && ctx->rt_event_cb->on_text && !ctx->rt_event_cb->on_transcript_text) {
                        ctx->rt_event_cb->on_text(ctx->rt_text_buf, ctx->rt_text_len, ctx->user_data);
                    }
                    ctx->rt_text_len = 0;
                    ctx->rt_text_stream = 0;
                    if (ctx->rt_event_cb->on_response_done) {
                        ctx->rt_event_cb->on_response_done(ctx->user_data);
                    }
                } else if (strcmp(type, "error") == 0) {
                    // 错误回调
                    cJSON *error = cJSON_GetObjectItem(root, "error");
                    cJSON *error_code = cJSON_GetObjectItem(error, "code");
                    cJSON *error_message = cJSON_GetObjectItem(error, "message");
                    if (error_code && cJSON_IsString(error_code) && error_message && cJSON_IsString(error_message)) {
                        if (ctx->rt_event_cb->on_error) {
                            ctx->rt_event_cb->on_error(
                                error_code->valuestring,
                                error_message->valuestring,
                                ctx->user_data
                            );
                        }
                    }
                    // TODO：重新建立session
                }
            }
        }

next_event:;
        // 处理剩余数据
        size_t processed = ctx->rt_json_pos - ctx->rt_json_buf;
        ctx->rt_json_buf_size -= processed;
        memmove(ctx->rt_json_buf, ctx->rt_json_buf + processed, ctx->rt_json_buf_size + 1);

        cJSON_Delete(root);
        root = cJSON_ParseWithLengthOpts(ctx->rt_json_buf, ctx->rt_json_buf_size + 1, (const char **) & (ctx->rt_json_pos), 0);
    }
}


int onesdk_rt_set_event_cb(onesdk_ctx_t *ctx, onesdk_rt_event_cb_t *cb)
{
    int ret = VOLC_OK;
    if (NULL == ctx || NULL == ctx->aigw_ws_ctx) {
        return VOLC_ERR_INIT;
    }
    ctx->aigw_ws_ctx->callback = rt_recv_cb;
    ctx->aigw_ws_ctx->userdata = ctx;
    ctx->rt_event_cb = cb;
    return ret;
}

int onesdk_rt_session_update(onesdk_ctx_t *ctx, aigw_ws_session_t *session)
{
    int ret = VOLC_OK;
    if (NULL == ctx || NULL == ctx->aigw_ws_ctx) {
        return VOLC_ERR_INIT;
    }
    return aigw_ws_session_update(ctx->aigw_ws_ctx, session);
}

int onesdk_rt_session_keepalive(onesdk_ctx_t *ctx)
{
    int ret = VOLC_OK;
    if (NULL == ctx || NULL == ctx->aigw_ws_ctx) {
        return VOLC_ERR_INIT;
    }
    int n = 0;
    while (n >= 0) {
        n = aigw_ws_run_event_loop(ctx->aigw_ws_ctx, 0);
    }

    return ret;
}

int onesdk_rt_audio_send(onesdk_ctx_t *ctx, const char *audio_data, size_t len, bool commit)
{
    int ret = VOLC_OK;
    if (NULL == ctx || NULL == ctx->aigw_ws_ctx) {
        return VOLC_ERR_INIT;
    }
    ret = aigw_ws_input_audio_buffer_append(ctx->aigw_ws_ctx, audio_data, len);
    if (ret != VOLC_OK) {
        return ret;
    }
    if (commit) {
        ret = aigw_ws_input_audio_buffer_commit(ctx->aigw_ws_ctx);
        if (ret != VOLC_OK) {
            return ret;
        }
        ret = aigw_ws_response_create(ctx->aigw_ws_ctx);
    }
    return ret;
}

int onesdk_rt_audio_response_cancel(onesdk_ctx_t *ctx)
{
    int ret = VOLC_OK;
    if (NULL == ctx || NULL == ctx->aigw_ws_ctx) {
        return VOLC_ERR_INIT;
    }
    return aigw_ws_response_cancel(ctx->aigw_ws_ctx);
}

int onesdk_rt_translation_session_update(onesdk_ctx_t *ctx, aigw_ws_translation_session_t *session)
{
    int ret = VOLC_OK;
    if (NULL == ctx || NULL == ctx->aigw_ws_ctx) {
        return VOLC_ERR_INIT;
    }
    return aigw_ws_translation_session_update(ctx->aigw_ws_ctx, session);
}

int onesdk_rt_translation_audio_send(onesdk_ctx_t *ctx, const char *audio_data, size_t len, bool commit)
{
    int ret = VOLC_OK;
    if (NULL == ctx || NULL == ctx->aigw_ws_ctx) {
        return VOLC_ERR_INIT;
    }
    ret = aigw_ws_input_audio_buffer_append(ctx->aigw_ws_ctx, audio_data, len);
    if (ret != VOLC_OK) {
        return ret;
    }
    if (commit) {
        ret = aigw_ws_input_audio_buffer_commit(ctx->aigw_ws_ctx);
        if (ret != VOLC_OK) {
            return ret;
        }
        ret = aigw_ws_input_audio_done(ctx->aigw_ws_ctx);
    }
    return ret;
}
