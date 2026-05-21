#ifndef SAFE_VAR_H
#define SAFE_VAR_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* Se define _GNU_SOURCE para habilitar el uso de cerrojos de lectura/escritura de POSIX (pthread_rwlock_t), los cuales requieren esta macro de prueba de características en glibc y no se activan por defecto en el modo de compilación estándar. */
#endif

#include <pthread.h>
#include <stddef.h>

/**
 * @file safe_var.h
 * @brief 类型安全且线程安全的通用变量容器。
 *
 * 本模块利用 POSIX 读写锁 (Read-Write Lock) 实现了一个通用的线程安全变量容器。
 * 其核心特色在于利用 C11 `_Generic` 泛型选择器机制，在编译期提供了强大的类型安全校验哨兵，
 * 能够完美确保对容器内的结构体进行整包读写或单字段读写时的类型一致性。
 */

/**
 * @brief 原始线程安全变量容器结构体
 * 
 * 维护底层的 raw 缓冲区和用于并发控制的读写锁。
 */
typedef struct {
	pthread_rwlock_t rwlock; /**< POSIX 读写锁，用于多线程并发读写控制 */
	void *data; /**< 指向动态分配的原始数据缓冲区的指针 */
	size_t data_size; /**< 被管理数据结构的总大小（字节数） */
} safe_var_raw_t;

/**
 * @brief 初始化原始线程安全变量容器
 *
 * 为容器分配动态内存缓冲区，并初始化底层的读写锁。
 * 如果提供了初始数据指针 init_data，则拷贝其内容；否则，整个缓冲区将被清零。
 *
 * @param[out] sv 指向需要初始化的原始安全变量容器结构体的指针。
 * @param[in] init_data 指向初始数据的指针；若传入 NULL，则将缓冲区全部初始化为 0。
 * @param[in] size 被管理数据结构的总大小（字节数）。
 * @return int 成功返回 0；参数非法、内存分配失败或读写锁初始化失败时返回 -1。
 */
int  safe_var_raw_init(safe_var_raw_t *sv, const void *init_data, size_t size);

/**
 * @brief 销毁原始线程安全变量容器并释放资源
 *
 * 彻底释放动态分配的缓冲区内存，并销毁底层的 POSIX 读写锁。
 *
 * @note 必须确保在没有任何其他线程尝试读写此容器的情况下，由主控线程单独发起销毁。
 *       若在其他线程仍阻塞于锁等待时强行调用此函数，将导致未定义行为 (Undefined Behavior)。
 *
 * @param[in,out] sv 指向需要被销毁的原始安全变量容器结构体的指针。
 */
void safe_var_raw_destroy(safe_var_raw_t *sv);

/**
 * @brief 线程安全地读取容器内管理的全量数据
 *
 * 自动获取读锁 (Read Lock)，支持多线程的高并发安全并行读取，并将全量数据深拷贝到输出目标缓冲区。
 *
 * @param[in] sv 指向原始安全变量容器结构体的指针。
 * @param[out] out_dest 指向接收拷贝数据的目标缓冲区的指针，该缓冲区大小必须不小于 sv->data_size。
 * @return int 成功返回 0；参数非法或容器未成功初始化时返回 -1。
 */
int  safe_var_raw_get(safe_var_raw_t *sv, void *out_dest);

/**
 * @brief 线程安全地向容器写入全量数据
 *
 * 自动获取独占写锁 (Write Lock)，以互斥方式将源数据完全覆盖拷贝写入底层的变量容器。
 *
 * @param[in,out] sv 指向原始安全变量容器结构体的指针。
 * @param[in] in_src 指向包含最新全量数据的源缓冲区的指针。
 * @return int 成功返回 0；参数非法或容器未成功初始化时返回 -1。
 */
int  safe_var_raw_set(safe_var_raw_t *sv, const void *in_src);

/**
 * @brief 线程安全地从容器指定偏移位置读取指定大小的数据段
 *
 * 自动获取读锁 (Read Lock)，提供严苛的边界越界检查，安全地读取结构体的指定部分（如单个字段）。
 *
 * @param[in] sv 指向原始安全变量容器结构体的指针。
 * @param[in] offset 读取的数据段距离缓冲区首地址的偏移量（字节数）。
 * @param[out] dest 指向接收读取字段数据的目标缓冲区的指针。
 * @param[in] size 需要读取的数据段的长度（字节数）。
 * @return int 成功返回 0；参数非法、越界（offset + size > data_size）或未初始化时返回 -1。
 */
int  safe_var_raw_get_offset(safe_var_raw_t *sv, size_t offset, void *dest, size_t size);

/**
 * @brief 线程安全地向容器指定偏移位置写入指定大小的数据段
 *
 * 自动获取独占写锁 (Write Lock)，提供严苛的边界越界检查，安全地覆盖写入结构体的指定部分（如单个字段）。
 *
 * @param[in,out] sv 指向原始安全变量容器结构体的指针。
 * @param[in] offset 写入的目标位置距离缓冲区首地址的偏移量（字节数）。
 * @param[in] src 指向保存新字段数据的源缓冲区的指针。
 * @param[in] size 需要写入的数据段的长度（字节数）。
 * @return int 成功返回 0；参数非法、越界（offset + size > data_size）或未初始化时返回 -1。
 */
int  safe_var_raw_set_offset(safe_var_raw_t *sv, size_t offset, const void *src, size_t size);

// ==================== 现代 C11 泛型类型安全封装层 ====================

/**
 * @brief 强类型安全变量定义宏
 *
 * 核心机制：将底层的原始数据容器与一个专属的强类型结构体指针（用于编译期静态类型推导，不占用运行时空间）捆绑组合。
 *
 * @param struct_type 需要被封装的底层用户自定义 C 结构体类型。
 */
#define SAFE_VAR_TYPE(struct_type) \
    struct { \
        safe_var_raw_t raw; \
        struct_type *type_marker; \
    }

/**
 * @brief 强类型安全变量的高层初始化快捷宏
 *
 * 包装 safe_var_raw_init，自动通过编译期 sizeof 传入目标结构体大小，无需手动测量计算。
 *
 * @param sv_ptr 指向强类型安全变量容器的指针。
 * @param init_data_ptr 指向初始数据的结构体指针；若为 NULL 则自动将缓冲区置 0。
 * @param struct_type 被封装的用户结构体类型名。
 * @return int 成功返回 0，失败返回 -1。
 */
#define SAFE_VAR_INIT(sv_ptr, init_data_ptr, struct_type) \
    safe_var_raw_init(&(sv_ptr)->raw, init_data_ptr, sizeof(struct_type))

/**
 * @brief 强类型安全变量的高层销毁快捷宏
 *
 * 包装 safe_var_raw_destroy。
 *
 * @param sv_ptr 指向强类型安全变量容器的指针。
 */
#define SAFE_VAR_DESTROY(sv_ptr) \
    safe_var_raw_destroy(&(sv_ptr)->raw)

/**
 * @brief 强类型全量读取高层快捷宏
 *
 * 包装 safe_var_raw_get，获取当前的结构体全量快照。
 *
 * @param sv_ptr 指向强类型安全变量容器的指针。
 * @param out_dest_ptr 指向接收全量结构体拷贝的强类型变量指针。
 * @return int 成功返回 0，失败返回 -1。
 */
#define SAFE_VAR_GET(sv_ptr, out_dest_ptr) \
    safe_var_raw_get(&(sv_ptr)->raw, out_dest_ptr)

/**
 * @brief 强类型全量覆写高层快捷宏
 *
 * 包装 safe_var_raw_set，以全新结构体内容原子更新容器。
 *
 * @param sv_ptr 指向强类型安全变量容器的指针。
 * @param in_src_ptr 指向用于更新的源全量结构体变量指针。
 * @return int 成功返回 0，失败返回 -1。
 */
#define SAFE_VAR_SET(sv_ptr, in_src_ptr) \
    safe_var_raw_set(&(sv_ptr)->raw, in_src_ptr)

/**
 * @brief 精准写入指定字段，并进行极其严苛的编译期强类型安全校对
 *
 * 核心黑魔法：利用 C11 `_Generic` 泛型选择器自动匹配 type_marker 中对应字段的静态类型：
 * 1. 验证传入的源数据指针 `val_ptr` 指向的类型与结构体内目标 `field` 字段声明的类型严格一致。
 * 2. 特殊优化处理：如果结构体字段是固定大小的字符数组 (char[N])，允许传入常规字符指针（char* 或 const char*）。
 * 3. 静态熔断：一旦出现类型不匹配，`_Static_assert` 会在**编译阶段**断言报错并熔断编译，抛出极佳的错误提示。
 * 校验通过后，会自动使用 offsetof 和 sizeof 计算地址布局并调用底层局部覆写 API。
 *
 * @param sv_ptr 指向强类型安全变量容器的指针。
 * @param field 结构体中需要被更新的字段名称。
 * @param val_ptr 指向新数据的指针（其解引用后的类型必须与该字段完全匹配）。
 */
#define SAFE_VAR_SET_FIELD(sv_ptr, field, val_ptr) \
    do { \
        _Static_assert( \
            _Generic(((sv_ptr)->type_marker->field), __typeof__(*(val_ptr)): 1, default: 0) || \
            _Generic(((sv_ptr)->type_marker->field), \
                char[sizeof((sv_ptr)->type_marker->field)]: _Generic((val_ptr), char*: 1, const char*: 1, default: 0), \
                default: 0), \
            "🚨 [Compile Error] SAFE_VAR_SET_FIELD: Type mismatch for field '" #field "'!"); \
        \
        safe_var_raw_set_offset(&(sv_ptr)->raw, \
                                offsetof(__typeof__(*(sv_ptr)->type_marker), field), \
                                val_ptr, \
                                sizeof((sv_ptr)->type_marker->field)); \
    } while(0)

/**
 * @brief 精准读取指定字段，并进行极其严苛的编译期强类型安全校对
 *
 * 核心黑魔法：利用 C11 `_Generic` 泛型选择器自动匹配 type_marker 中对应字段的静态类型：
 * 1. 验证传入的接收指针 `dest_ptr` 指向的类型与结构体内目标 `field` 字段声明的类型严格一致。
 * 2. 特殊优化处理：如果结构体字段是固定大小的字符数组 (char[N])，允许传入字符指针（char*）以写入缓存。
 * 3. 静态熔断：一旦出现类型不匹配，`_Static_assert` 会在**编译阶段**断言报错并熔断编译。
 * 校验通过后，会自动使用 offsetof 和 sizeof 计算地址布局并调用底层局部读取 API。
 *
 * @param sv_ptr 指向强类型安全变量容器的指针。
 * @param field 结构体中需要被读取的字段名称。
 * @param dest_ptr 指向接收变量缓冲区的指针（其解引用后的类型必须与该字段完全匹配）。
 */
#define SAFE_VAR_GET_FIELD(sv_ptr, field, dest_ptr) \
    do { \
        _Static_assert( \
            _Generic(((sv_ptr)->type_marker->field), __typeof__(*(dest_ptr)): 1, default: 0) || \
            _Generic(((sv_ptr)->type_marker->field), \
                char[sizeof((sv_ptr)->type_marker->field)]: _Generic((dest_ptr), char*: 1, default: 0), \
                default: 0), \
            "🚨 [Compile Error] SAFE_VAR_GET_FIELD: Type mismatch for field '" #field "'!"); \
        \
        safe_var_raw_get_offset(&(sv_ptr)->raw, \
                                offsetof(__typeof__(*(sv_ptr)->type_marker), field), \
                                dest_ptr, \
                                sizeof((sv_ptr)->type_marker->field)); \
    } while(0)

#endif // SAFE_VAR_H
