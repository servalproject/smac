#ifndef SUCCINCT_LOG_H
#define SUCCINCT_LOG_H

#ifdef ANDROID

#include <android/log.h>

#define LOGI(X, ...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, X, ##__VA_ARGS__))
#define LOGE(X, ...) ((void)__android_log_print(ANDROID_LOG_ERROR, __FILE__, X, ##__VA_ARGS__))
#else
#define LOGI(X, ...) fprintf(stderr, "%s:%d; " X "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define LOGE(X, ...) fprintf(stderr, "%s:%d; " X "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#endif
