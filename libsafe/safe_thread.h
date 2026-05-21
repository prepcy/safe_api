#ifndef SAFE_THREAD_H
#define SAFE_THREAD_H

#define _GNU_SOURCE 
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

/**
 * @file safe_thread.h
 * @brief 类型安全且具备超时控制的线程管理封装层。
 *
 * 本模块对 POSIX 线程 (pthreads) 进行了二次封装。
 * 核心特性包括：
 * 1. 基于 C11 的编译期强类型安全校验机制，防止向线程函数传递错误类型的参数。
 * 2. 具备超时等待的 join 机制，超时后会自动通过 pthread_cancel 强制终止线程并进行 detach 释放资源，防止主线程无限期挂起。
 * 3. 采用 C11 `<stdatomic.h>` 原子类型优化并发标记，避免多核处理器下的缓存可见性与数据竞争问题。
 */

/**
 * @brief 安全线程控制句柄结构体
 *
 * 存储安全线程的上下文、配置以及运行状态。
 * 内部设计保持简单纯粹 (KISS)，并采用原子变量确保多线程可见性。
 */
typedef struct {
    pthread_t thread_id;         /**< POSIX 线程标识符 */
    atomic_bool should_stop;     /**< 原子停止标志，指示是否已请求该线程停止退出 */
    unsigned int timeout_ms;     /**< 线程停止时的硬超时时间（毫秒），用于限时 Join 汇合 */
    void *(*proxy_func)(void *); /**< 内部代理桥接函数指针，用于安全的强类型参数转型转换 */
    void *user_arg;              /**< 用户传入的线程函数实参指针 */
    atomic_bool is_running;      /**< 原子运行状态标志，指示线程是否正在运行 */
} safe_thread_t;

/**
 * @brief 初始化原始安全线程控制句柄
 *
 * 将线程 ID 置为 0，将 should_stop 和 is_running 原子变量初始化为 false，并保存用户函数及实参。
 *
 * @param[out] st 指向需要初始化的安全线程句柄的指针。
 * @param[in] func 用户真实的线程入口函数（会被桥接强转）。
 * @param[in] arg 需要传递给线程入口函数的实参指针。
 * @param[in] timeout_ms 线程退出时的限时 Join 超时时间（毫秒）。
 * @return int 成功返回 0，参数非法时返回 -1。
 */
int  safe_thread_raw_init(safe_thread_t *st, void *(*func)(void *), void *arg, unsigned int timeout_ms);

/**
 * @brief 启动线程执行
 *
 * 创建并运行一个新的 POSIX 线程，该线程首先会进入内部代理桥接函数。
 *
 * @param[in,out] st 指向已初始化的安全线程句柄的指针。
 * @return int 成功返回 0，失败返回具体的 pthread_create 错误码，若线程已在运行则返回 -1。
 */
int  safe_thread_start(safe_thread_t *st);

/**
 * @brief 请求线程停止并限时等待其退出（Join 汇合）
 *
 * 将 should_stop 标志置为 true，然后利用 pthread_timedjoin_np 在 timeout_ms 时间内阻塞等待线程退出。
 * 如果在限时内线程正常退出，则释放资源并获取返回值。
 * 如果等待超时，为了防止主线程无限期挂起，将自动向子线程发送 pthread_cancel 取消信号，并强行 detach 剥离线程以防止资源泄漏。
 *
 * @note 强行取消正在持有互斥锁或进行动态内存分配的线程可能会导致资源泄漏或死锁。
 *       建议用户子线程周期性调用 safe_thread_is_requested_to_stop() 优雅自杀，
 *       或者在子线程中注册 pthread_cleanup_push/pop 清理函数。
 *
 * @param[in,out] st 指向安全线程句柄的指针。
 * @param[out] retval 若不为 NULL，则在成功 Join 时用于接收子线程的返回值。
 * @return int 成功返回 0；超时返回 ETIMEDOUT (110)；其他情况返回非零错误码。
 */
int  safe_thread_stop(safe_thread_t *st, void **retval);

/**
 * @brief 检查是否已向该线程发送停止请求
 *
 * 线程工作函数应当周期性地轮询调用此函数，检测是否需要优雅地提前结束退出。
 *
 * @param[in] st 指向安全线程句柄的指针。
 * @return true 表示已发出停止请求或句柄为空；false 表示线程应继续正常运行。
 */
bool safe_thread_is_requested_to_stop(safe_thread_t *st);

// ==================== 【核心优化】C11 编译期强类型安全哨兵 ====================

/**
 * @brief 强类型安全线程初始化宏
 *
 * 在编译期进行双重静态安全验证，保障类型安全：
 * 1. 核心黑魔法：利用 `(void)sizeof(func(arg_ptr))` 在编译期模拟函数调用以触发参数类型校对，
 *    一旦用户传入的实参指针 `arg_ptr` 的类型与线程入口函数 `func` 声明的形参类型不一致，编译器将立刻拒绝编译。
 *    (sizeof 仅在编译期分析表达式的返回类型，绝对不会在运行时真正去执行 func)。
 * 2. 静态断言：利用 `_Static_assert` 强制拦截并确保线程入口函数返回的是一个标准指针类型 (如 `void*`)。
 * 校验通过后，宏会自动将函数和实参强转并调用底层初始化接口。
 *
 * @param st_ptr 指向 safe_thread_t 句柄的指针。
 * @param func 用户自定义的强类型线程函数（如 `void* my_worker(my_business_context_t *ctx)`）。
 * @param arg_ptr 传入线程函数的实参指针，其类型必须与 func 入参类型完美强匹配。
 * @param timeout_ms 强制退出超时时间（毫秒）。
 */
#define SAFE_THREAD_INIT(st_ptr, func, arg_ptr, timeout_ms) \
    do { \
        /* 1. 核心黑魔法：利用 sizeof 触发编译器对 func(arg_ptr) 进行入参类型匹配审查 */ \
        (void)sizeof(func(arg_ptr)); \
        \
        /* 2. 静态断言：强制拦截确保线程函数的返回值必须是一个标准指针类型 */ \
        _Static_assert(sizeof(__typeof__(func(arg_ptr))) == sizeof(void*), \
            "🚨 [Compile Error] SAFE_THREAD_INIT: Thread function must return a pointer type (e.g., void*)!"); \
        \
        /* 3. 校验通过，安全强转并灌入底层框架 */ \
        safe_thread_raw_init(st_ptr, (void *(*)(void *))(func), (void *)(arg_ptr), timeout_ms); \
    } while(0)

#endif // SAFE_THREAD_H
