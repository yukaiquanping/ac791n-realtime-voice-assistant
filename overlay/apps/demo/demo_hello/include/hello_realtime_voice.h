#ifndef HELLO_REALTIME_VOICE_H
#define HELLO_REALTIME_VOICE_H

// 0: existing iFly/DeepSeek dialog. 1: OneSDK realtime voice.
#ifndef HELLO_REALTIME_VOICE_ENABLE
#define HELLO_REALTIME_VOICE_ENABLE 1
#endif

#if HELLO_REALTIME_VOICE_ENABLE
// Fill in your own Volcengine IoT instance and bound voice-agent product.
#define HELLO_IOT_HTTP_HOST      "https://iot-cn-shanghai.iot.volces.com"
#define HELLO_IOT_INSTANCE_ID    ""
#define HELLO_IOT_PRODUCT_KEY    ""
#define HELLO_IOT_PRODUCT_SECRET ""
#define HELLO_IOT_DEVICE_NAME    ""
#define HELLO_IOT_BOT_ID         ""
#endif

#endif
