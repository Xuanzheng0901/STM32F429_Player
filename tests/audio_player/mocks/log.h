#ifndef TEST_LOG_H
#define TEST_LOG_H
void test_log(const char *tag, const char *format, ...);
#define LOGI test_log
#define LOGW test_log
#define LOGE test_log
#endif
