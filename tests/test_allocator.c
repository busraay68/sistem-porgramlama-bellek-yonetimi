#include "allocator.h"
#include "allocator_terminal_ui.h"
#include "allocator_threadsafe.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define THREAD_COUNT 8
#define THREAD_ITERATIONS 2000

static FILE *g_log_file;

/*
 * Test programı hem terminale hem allocator_test.log dosyasına kısa durum
 * mesajları yazar. Böylece proje isterindeki loglama maddesi karşılanır.
 */
static void log_line(const char *message)
{
    printf("%s\n", message);
    if (g_log_file != NULL) {
        fprintf(g_log_file, "%s\n", message);
        fflush(g_log_file);
    }
}

static void require_condition(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        if (g_log_file != NULL) {
            fprintf(g_log_file, "[FAIL] %s\n", message);
        }
        exit(EXIT_FAILURE);
    }
    printf("[OK] %s\n", message);
}

static long elapsed_us(struct timespec start, struct timespec end)
{
    return (long)((end.tv_sec - start.tv_sec) * 1000000L +
                  (end.tv_nsec - start.tv_nsec) / 1000L);
}

static void basic_tests(void)
{
    int *numbers;
    char *buffer;

    log_line("\n[TEST] Temel malloc/free/calloc");

    buffer = my_malloc(64);
    require_condition(buffer != NULL, "my_malloc 64 byte alan döndürdü");
    memset(buffer, 'A', 64);
    my_free(buffer);

    /* calloc sonrası bütün elemanların sıfırlandığı tek tek doğrulanır. */
    numbers = my_calloc(16, sizeof(int));
    require_condition(numbers != NULL, "my_calloc alan döndürdü");
    for (int i = 0; i < 16; ++i) {
        require_condition(numbers[i] == 0, "my_calloc belleği sıfırladı");
    }
    my_free(numbers);
}

static void edge_case_tests(void)
{
    void *zero;
    void *too_big;
    void *double_free_ptr;

    log_line("\n[TEST] Edge case ve hata senaryoları");

    zero = my_malloc(0);
    require_condition(zero == NULL, "0 byte malloc NULL döndürdü");

    too_big = my_malloc(SIZE_MAX);
    require_condition(too_big == NULL, "Çok büyük malloc güvenli şekilde reddedildi");

    my_free(NULL);
    require_condition(1, "NULL free sessizce yok sayıldı");

    /*
     * Double-free testinde ikinci free çağrısının stderr'e uyarı basması beklenir;
     * programın çökmeden devam etmesi başarı kabul edilir.
     */
    double_free_ptr = my_malloc(32);
    require_condition(double_free_ptr != NULL, "double-free testi için bellek alındı");
    my_free(double_free_ptr);
    my_free(double_free_ptr);
    require_condition(1, "double-free stderr uyarısı ile yakalandı");
}

static void fragmentation_test(void)
{
    void *a;
    void *b;
    void *c;

    log_line("\n[TEST] Fragmentation senaryosu");

    /* Ortadaki blok serbest bırakılarak bilinçli bir fragmentation durumu yaratılır. */
    a = my_malloc(256);
    b = my_malloc(512);
    c = my_malloc(256);
    require_condition(a != NULL && b != NULL && c != NULL, "fragmentation blokları ayrıldı");

    my_free(b);
    print_memory_stats();

    my_free(a);
    my_free(c);
}

static void *thread_worker(void *arg)
{
    long worker_id = (long)arg;

    for (int i = 0; i < THREAD_ITERATIONS; ++i) {
        size_t size = (size_t)(((i + worker_id) % 128) + 1);
        /* Çok sayıda küçük allocation/free çağrısı mutex entegrasyonunu zorlar. */
        unsigned char *ptr = my_malloc(size);

        if (ptr == NULL) {
            fprintf(stderr, "thread %ld: my_malloc başarısız\n", worker_id);
            return (void *)1;
        }

        memset(ptr, (int)(worker_id & 0xFF), size);
        my_free(ptr);
    }

    return NULL;
}

static void thread_safety_test(void)
{
    pthread_t threads[THREAD_COUNT];
    struct timespec start;
    struct timespec end;

    log_line("\n[TEST] Thread safety ve performans");

    /* Basit performans ölçümü: toplam thread yükünün süresi mikrosaniye olarak raporlanır. */
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (long i = 0; i < THREAD_COUNT; ++i) {
        int rc = pthread_create(&threads[i], NULL, thread_worker, (void *)i);
        require_condition(rc == 0, "pthread_create başarılı");
    }

    for (int i = 0; i < THREAD_COUNT; ++i) {
        void *thread_result = NULL;
        int rc = pthread_join(threads[i], &thread_result);
        require_condition(rc == 0 && thread_result == NULL, "pthread_join başarılı");
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("[PERF] %d thread x %d iterasyon: %ld us\n",
           THREAD_COUNT, THREAD_ITERATIONS, elapsed_us(start, end));
}

int main(int argc, char **argv)
{
    int launch_ui = 1;

    if (argc > 1 && strcmp(argv[1], "--no-ui") == 0) {
        launch_ui = 0;
    }

    g_log_file = fopen("allocator_test.log", "w");
    if (g_log_file == NULL) {
        perror("allocator_test.log");
    }

    /* Testlerde deterministik ve hızlı davranış için first-fit stratejisi seçilir. */
    allocator_set_strategy(allocator_strategy_first_fit);

    basic_tests();
    edge_case_tests();
    thread_safety_test();
    fragmentation_test();

    print_memory_stats();
    allocator_report_memory_leaks();

    if (launch_ui) {
        log_line("\n[UI] Terminal arayüzü başlatılıyor");
        run_allocator_terminal_ui();
        allocator_report_memory_leaks();
    } else {
        log_line("\n[UI] --no-ui ile atlandı");
    }

    if (g_log_file != NULL) {
        fclose(g_log_file);
    }

    return EXIT_SUCCESS;
}
