#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
struct k_mutex { int count; };
#define K_MUTEX_DEFINE(n) struct k_mutex n = {0}
#define K_FOREVER 0
static inline int k_mutex_lock(struct k_mutex *m, int t){ (void)t; m->count++; return 0; }
static inline int k_mutex_unlock(struct k_mutex *m){ m->count--; return 0; }
static inline void k_msleep(int ms){ (void)ms; }
static inline void k_busy_wait(int us){ (void)us; }
