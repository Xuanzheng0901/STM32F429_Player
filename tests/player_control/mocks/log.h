#include <stdio.h>
#define LOGI(tag, ...) do { printf("%s: ", tag); printf(__VA_ARGS__); puts(""); } while(0)
#define LOGW LOGI
#define LOGE LOGI
