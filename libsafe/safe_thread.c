#define _GNU_SOURCE
#include "safe_thread.h"
#include <stdlib.h>
#include <errno.h>
#include <time.h>

/**
 * @brief 内部代理桥接入口函数。
 *
 * 包装用户定义的强类型线程入口函数，作为标准的 pthread 入口进行桥接。
 *
 * @param[in] arg 指向 safe_thread_t 句柄的指针。
 * @return void* 线程返回的指针结果。
 */
static void* safe_thread_proxy(void *arg) {
    safe_thread_t *st = (safe_thread_t *)arg;
    return st->proxy_func(st->user_arg);
}

int safe_thread_raw_init(safe_thread_t *st, void *(*func)(void *), void *arg, unsigned int timeout_ms) {
    if (!st || !func) return -1;
    st->thread_id = 0;
    
    // 初始化原子变量。此处不需要复杂的屏障，使用 relaxed 初始化即可
    atomic_init(&st->should_stop, false);
    atomic_init(&st->is_running, false);
    
    st->timeout_ms = timeout_ms;
    st->proxy_func = func;
    st->user_arg = arg;
    return 0;
}

int safe_thread_start(safe_thread_t *st) {
    if (!st) return -1;

    // 【防重入与 TOCTOU 核心优化】：
    // 使用原子比较与交换 (CAS) 指令将 is_running 从 false 原子地转换为 true。
    // 这彻底解决了多个线程并发调用 safe_thread_start 导致的“多次创建线程”竞态条件 (TOCTOU)。
    // 内存顺序采用 memory_order_seq_cst，在 ARM64 下会生成带屏障的原子指令 (如 ldaxr/stlxr 或 LSE 的 casal)，
    // 确保绝对的顺序一致性。
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&st->is_running, &expected, true, 
                                                  memory_order_seq_cst, memory_order_relaxed)) {
        return -1; // 线程已经在运行，安全熔断
    }
    
    // 设置停止标记为 false。采用 Release 语义确保在子线程启动前，此状态清除已刷新到内存中。
    // 在 ARM64 下编译为 stlr 释放写屏障指令，防止指令重排。
    atomic_store_explicit(&st->should_stop, false, memory_order_release);
    
    int ret = pthread_create(&st->thread_id, NULL, safe_thread_proxy, st);
    if (ret != 0) {
        // 创建失败，通过 Release 原子写入回滚 is_running 标志
        atomic_store_explicit(&st->is_running, false, memory_order_release);
    }
    return ret;
}

bool safe_thread_is_requested_to_stop(safe_thread_t *st) {
    if (!st) return true;

    // 【ARM64 弱内存模型优化】：
    // 使用 Acquire (获取) 语义加载停止标志。
    // 1. 在 ARM64 汇编层级，编译为单条 ldar 指令，开销极低（无需插入昂贵的 dmb 全局内存屏障指令）。
    // 2. 语义保证：确保子线程在观测到 should_stop 变为 true 之后，后续对共享业务数据的读取操作
    //    绝对不会被 CPU 乱序优化提前到该 ldar 之前执行。
    //    这与主线程写入停止标记时的 Release (释放) 语义构成了经典的 Release-Acquire 同步对，
    //    完美解决 ARM64 下的乱序执行和内存可见性问题。
    return atomic_load_explicit(&st->should_stop, memory_order_acquire);
}

int safe_thread_stop(safe_thread_t *st, void **retval) {
    // 使用 Acquire 语义加载运行状态，保证读取到的状态最新，并与 start 的 CAS 形成 Happen-Before 关系
    if (!st || !atomic_load_explicit(&st->is_running, memory_order_acquire)) return -1;

    // 【ARM64 弱内存模型优化】：
    // 主线程写入 should_stop 采用 Release (释放) 语义。
    // 1. 在 ARM64 汇编层级，编译为单条 stlr 指令。
    // 2. 语义保证：确保主线程在将 should_stop 置为 true 之前所做的所有内存写入操作（如数据刷新等）
    //    在子线程观测到 should_stop 为 true 时，全部都已落盘并对子线程完全可见，绝对不会被 CPU 重排到 stlr 之后。
    atomic_store_explicit(&st->should_stop, true, memory_order_release);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long nsec = ts.tv_nsec + ((long long)(st->timeout_ms % 1000) * 1000000LL);
    ts.tv_sec += (st->timeout_ms / 1000) + (nsec / 1000000000LL);
    ts.tv_nsec = nsec % 1000000000LL;

    // 限时等待子线程合拢 (Join)
    int ret = pthread_timedjoin_np(st->thread_id, retval, &ts);
    
    // 超时硬退机制
    if (ret == ETIMEDOUT) {
        pthread_cancel(st->thread_id); // 发射取消信号强制中断线程
        pthread_detach(st->thread_id); // 剥离线程，防止后续产生僵尸线程
        atomic_store_explicit(&st->is_running, false, memory_order_release);
        return ETIMEDOUT; 
    }

    // 成功合拢，原子回滚运行状态
    atomic_store_explicit(&st->is_running, false, memory_order_release);
    return ret;
}
