#include "safe_var.h"
#include <stdlib.h>
#include <string.h>

int safe_var_raw_init(safe_var_raw_t *sv, const void *init_data, size_t size) {
    if (!sv || size == 0) return -1;

    // 初始化 POSIX 读写锁
    if (pthread_rwlock_init(&sv->rwlock, NULL) != 0) return -1;

    sv->data = malloc(size);
    if (!sv->data) {
        pthread_rwlock_destroy(&sv->rwlock);
        return -1;
    }
    sv->data_size = size;

    if (init_data) {
        memcpy(sv->data, init_data, size);
    } else {
        memset(sv->data, 0, size);
    }
    return 0;
}

void safe_var_raw_destroy(safe_var_raw_t *sv) {
    if (!sv) return;
    
    pthread_rwlock_wrlock(&sv->rwlock); // 强制写锁保护销毁过程
    if (sv->data) {
        free(sv->data);
        sv->data = NULL;
    }
    sv->data_size = 0;
    pthread_rwlock_unlock(&sv->rwlock);
    
    pthread_rwlock_destroy(&sv->rwlock);
}

int safe_var_raw_get(safe_var_raw_t *sv, void *out_dest) {
    if (!sv || !out_dest || !sv->data) return -1;

    pthread_rwlock_rdlock(
	    &sv->rwlock); // 全量读取：获取共享读锁（支持高并发读）
    memcpy(out_dest, sv->data, sv->data_size);
    pthread_rwlock_unlock(&sv->rwlock);
    return 0;
}

int safe_var_raw_set(safe_var_raw_t *sv, const void *in_src) {
    if (!sv || !in_src || !sv->data) return -1;

    pthread_rwlock_wrlock(&sv->rwlock); // 全量覆盖：获取独占写锁
    memcpy(sv->data, in_src, sv->data_size);
    pthread_rwlock_unlock(&sv->rwlock);
    return 0;
}

int safe_var_raw_get_offset(safe_var_raw_t *sv, size_t offset, void *dest, size_t size) {
    if (!sv || !dest || !sv->data || (offset + size) > sv->data_size) return -1;

    pthread_rwlock_rdlock(&sv->rwlock); // 局部读取：获取共享读锁
    memcpy(dest, (const char *)sv->data + offset, size);
    pthread_rwlock_unlock(&sv->rwlock);
    return 0;
}

int safe_var_raw_set_offset(safe_var_raw_t *sv, size_t offset, const void *src, size_t size) {
    if (!sv || !src || !sv->data || (offset + size) > sv->data_size) return -1;

    pthread_rwlock_wrlock(&sv->rwlock); // 局部写入：获取独占写锁
    memcpy((char *)sv->data + offset, src, size);
    pthread_rwlock_unlock(&sv->rwlock);
    return 0;
}
