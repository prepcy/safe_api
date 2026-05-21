#include <stdio.h>
#include <unistd.h>
#include "safe_thread.h"

// 专属业务上下文
typedef struct {
    int task_id;
    char thread_name[16];
} context_t;

// 1. 【强类型函数定义】入参直接就是 context_t*，告别隐式 void* 转换
void* high_cpu_calculation_worker(context_t *ctx) {
    printf("[%s] Started heavy loop...\n", ctx->thread_name);
    
    unsigned long long count = 0;
    while (1) {
        count++;
        
        // ✨ 【优雅退出最佳实践】
        // 如果这里不加这个取消点，由于当前循环属于没有任何系统调用的纯 CPU 密集计算，
        // 外部的 pthread_cancel 将无限期失效。加上这一行，超时后立刻在此处安全自杀！
        pthread_testcancel();
    }
    return NULL;
}

int main() {
    safe_thread_t my_thread;
    context_t my_context = { .task_id = 9527, .thread_name = "CPU-Worker" };
    void *retval;

    // =========================================================================
    // 🔥 【强类型守门员实验区】 解开下面任意一行的注释，编译将直接被熔断：
    // =========================================================================
    // 实验 1：参数类型不匹配。误将一个普通的 int 变量指针传给了需要 context_t* 的函数
    // int fake_ctx = 42;
    // SAFE_THREAD_INIT(&my_thread, high_cpu_calculation_worker, &fake_ctx, 500);

    // 实验 2：函数返回值不匹配。如果线程函数返回的不是指针（比如返回 int），此处立刻拦截
    // =========================================================================

    // 正确的强类型初始化：配置 500ms 硬退出超时
    SAFE_THREAD_INIT(&my_thread, high_cpu_calculation_worker, &my_context, 500);
    
    printf("[Main] Starting type-safe thread...\n");
    safe_thread_start(&my_thread);
    
    // 让子线程狂飙 1 秒
    sleep(1);
    
    printf("[Main] 500ms time limit reached. Stopping thread forcibly...\n");
    int ret = safe_thread_stop(&my_thread, &retval);
    
    if (ret == 110) { // ETIMEDOUT
        printf("[Main] SUCCESS! Heavy CPU thread stopped safely within 500ms limit via cancellation point!\n");
    } else {
        printf("[Main] Thread stopped with code: %d\n", ret);
    }

    return 0;
}
