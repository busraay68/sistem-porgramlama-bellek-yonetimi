#ifndef ALLOCATOR_THREADSAFE_H
#define ALLOCATOR_THREADSAFE_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

extern pthread_mutex_t allocator_mutex;

void print_memory_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* ALLOCATOR_THREADSAFE_H */
