#include "allocator.h"
#include "allocator_threadsafe.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Bu dosya Person 3 entegrasyon katmanıdır.
 *
 * Önemli kural: Mevcut allocator_ops.c dosyasına dokunmamak için Makefile,
 * o dosyadaki my_malloc/my_free/my_calloc sembollerini derleme zamanında
 * allocator_raw_malloc/allocator_raw_free/allocator_raw_calloc isimlerine çevirir.
 * Bu dosya ise public API isimlerini yeniden tanımlar ve tüm çağrıları tek global
 * mutex altında raw fonksiyonlara yönlendirir.
 */
pthread_mutex_t allocator_mutex = PTHREAD_MUTEX_INITIALIZER;

void *allocator_raw_malloc(size_t size);
void allocator_raw_free(void *ptr);
void *allocator_raw_calloc(size_t nmemb, size_t size);

void *my_malloc(size_t size)
{
    void *ptr;
    int rc = pthread_mutex_lock(&allocator_mutex);

    /* Lock alınamazsa allocator veri yapısına girmek güvenli değildir. */
    if (rc != 0) {
        fprintf(stderr, "allocator mutex lock hatasi: %s\n", strerror(rc));
        return NULL;
    }

    ptr = allocator_raw_malloc(size);

    rc = pthread_mutex_unlock(&allocator_mutex);
    if (rc != 0) {
        fprintf(stderr, "allocator mutex unlock hatasi: %s\n", strerror(rc));
    }

    return ptr;
}

void my_free(void *ptr)
{
    int rc;

    /* Standart free(NULL) davranışı korunur; kilit almaya gerek yoktur. */
    if (ptr == NULL) {
        return;
    }

    rc = pthread_mutex_lock(&allocator_mutex);
    if (rc != 0) {
        fprintf(stderr, "allocator mutex lock hatasi: %s\n", strerror(rc));
        return;
    }

    allocator_raw_free(ptr);

    rc = pthread_mutex_unlock(&allocator_mutex);
    if (rc != 0) {
        fprintf(stderr, "allocator mutex unlock hatasi: %s\n", strerror(rc));
    }
}

void *my_calloc(size_t nmemb, size_t size)
{
    void *ptr;
    int rc;

    /* calloc için sıfır eleman veya sıfır boyut geçersiz kabul edilir. */
    if (nmemb == 0 || size == 0) {
        return NULL;
    }

    /* nmemb * size taşmasını mutex almadan önce yakalar. */
    if (nmemb > SIZE_MAX / size) {
        fprintf(stderr, "allocator hatasi: my_calloc boyut tasmasi\n");
        return NULL;
    }

    rc = pthread_mutex_lock(&allocator_mutex);
    if (rc != 0) {
        fprintf(stderr, "allocator mutex lock hatasi: %s\n", strerror(rc));
        return NULL;
    }

    ptr = allocator_raw_calloc(nmemb, size);

    rc = pthread_mutex_unlock(&allocator_mutex);
    if (rc != 0) {
        fprintf(stderr, "allocator mutex unlock hatasi: %s\n", strerror(rc));
    }

    return ptr;
}
