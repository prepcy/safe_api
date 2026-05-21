#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "safe_var.h"

#define TEST_LOOPS 2000000

// 业务结构体 A
typedef struct {
    int status_a;
    double status_b;
    char log_msg[32];
} my_business_data_t;

// 另一个毫不相干的结构体 B（用来做冒充实验）
typedef struct {
    float dangerous_field;
} intruder_data_t;


// 使用全新宏定义安全变量：强绑定 my_business_data_t 类型
static SAFE_VAR_TYPE(my_business_data_t) g_safe_config;


// 线程 1：超高频读取 status_a（测试多线程并发读锁的吞吐量）
void* reader_thread_fast(void* arg) {
    int val;
    for (int i = 0; i < TEST_LOOPS; i++) {
        SAFE_VAR_GET_FIELD(&g_safe_config, status_a, &val);
    }
    printf("[Reader Fast] Successfully completed %d reads.\n", TEST_LOOPS);
    return NULL;
}

// 线程 2：高频写入者
void* writer_thread_slow(void* arg) {
    for (int i = 0; i < TEST_LOOPS / 10; i++) { // 写操作频率通常低于读
        int val = i;
        SAFE_VAR_SET_FIELD(&g_safe_config, status_a, &val);
    }
    printf("[Writer] Completed updates.\n");
    return NULL;
}

int main() {
    my_business_data_t init_data = { .status_a = 100, .status_b = 99.9, .log_msg = "Safe" };
    
    // 初始化
    if (SAFE_VAR_INIT(&g_safe_config, &init_data, my_business_data_t) != 0) {
        return -1;
    }

    // =========================================================================
    // 🔥 【类型安全守门员验证区】 解开下面任意一行的注释，编译将直接被熔断：
    // =========================================================================
    // 实验 1：把一个 double 类型的变量指针，误传给了 int 类型的字段 status_a
    // double bad_val = 3.14;
    // SAFE_VAR_SET_FIELD(&g_safe_config, status_a, &bad_val); 

    // 实验 2：试图把一个不合法的 float* 缓冲区去读取字符串字段 log_msg
    // float bad_buffer;
    // SAFE_VAR_GET_FIELD(&g_safe_config, log_msg, &bad_buffer);
    // =========================================================================

    printf("Starting stress test with Read-Write Lock...\n");

    pthread_t r1, r2, w1;
    pthread_create(&r1, NULL, reader_thread_fast, NULL);
    pthread_create(&r2, NULL, reader_thread_fast, NULL); // 两个读线程可以同时进锁，毫无阻塞
    pthread_create(&w1, NULL, writer_thread_slow, NULL);

    pthread_join(r1, NULL);
    pthread_join(r2, NULL);
    pthread_join(w1, NULL);

    // 验证全量获取
    my_business_data_t final_snapshot;
    SAFE_VAR_GET(&g_safe_config, &final_snapshot);
    printf("Final Status A: %d, Log Message: %s\n", final_snapshot.status_a, final_snapshot.log_msg);

    SAFE_VAR_DESTROY(&g_safe_config);
    return 0;
}
