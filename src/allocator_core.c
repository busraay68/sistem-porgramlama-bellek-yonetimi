#include "allocator.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ALLOCATOR_SBRK_FAILURE ((void *)-1)

/*
 * Bu modül, allocator altyapısının temel akışını içerir:
 * başlangıç havuzu oluşturma, blok zincirini yönetme, free list işlemleri
 * ve my_malloc için çekirdek tahsis süreci.
 */

static block_header_t *g_free_list_head = NULL;
static block_header_t *g_block_list_head = NULL;
allocator_strategy_fn g_strategy_fn = NULL;
int g_allocator_initialized = 0;
/* Başlangıç heap adresi, tanılama ve raporlama amaçları için saklanır. */
static void *g_heap_start = NULL;

size_t total_reserved_memory = 0;
size_t total_allocated_memory = 0;
size_t active_block_count = 0;

static void allocator_append_to_block_list(block_header_t *block);
static block_header_t *allocator_create_block(void *region_start,
                                              size_t total_region_size,
                                              int is_free);
static int allocator_is_valid_magic(uint32_t magic);
static int allocator_calculate_region_size(size_t payload_size,
                                           size_t *total_region_size);

/*
 * İstenen boyutu allocator kurallarına uygun hale getirir.
 * Bu adım, hem alignment gereksinimini sağlar hem de çok küçük blokları alt sınırda toplar.
 */
size_t allocator_align_size(size_t size)
{
    size_t aligned_size;

    if (size == 0) {
        return 0;
    }

    if (size > (SIZE_MAX - (ALLOCATOR_ALIGNMENT - 1))) {
        return 0;
    }

    /*
     * Bitwise alignment, güç-of-two tabanlı sınırlar için daha verimli ve
     * standart bir yaklaşımdır.
     */
    aligned_size = (size + (ALLOCATOR_ALIGNMENT - 1)) &
                   ~(ALLOCATOR_ALIGNMENT - 1); /* Son bitleri temizleyip boyutu 16 byte katına yuvarlar. */

    if (aligned_size < ALLOCATOR_MIN_BLOCK_SIZE) {
        return ALLOCATOR_MIN_BLOCK_SIZE;
    }

    return aligned_size;
}

/*
 * Dışarıdan seçilecek yerleştirme stratejisini kaydeder.
 * Böylece çekirdek allocator akışı korunurken blok seçimi modüler kalır.
 */
void allocator_set_strategy(allocator_strategy_fn strategy_fn)
{
    g_strategy_fn = strategy_fn;
}

/*
 * Free list başlangıç işaretçisini döndürür.
 * Bu erişim, listeyi dış modüllerin okuyabilmesi için kontrollü bir kapı sağlar.
 */
block_header_t *allocator_get_free_list_head(void)
{
    return g_free_list_head;
}

/*
 * Heap üzerindeki tüm blokları tutan zincirin başlangıcını döndürür.
 * İstatistik ve raporlama modülleri bu yapı üzerinden tam tarama yapabilir.
 */
block_header_t *allocator_get_block_list_head(void)
{
    return g_block_list_head;
}

/*
 * Bir blok Header adresinden kullanıcıya verilecek payload adresini üretir.
 * Bellek görünümü [Header][payload] şeklindedir; bu fonksiyon payload tarafını döndürür.
 */
void *allocator_block_to_payload(block_header_t *block)
{
    if (block == NULL) {
        return NULL;
    }

    /*
     * Header bilgisinin hemen ardından başlayan alan, kullanıcıya döndürülen
     * payload bölümüdür.
     */
    return (void *)(block + 1); /* [Header][payload] düzeninde Header boyutunu atlayıp payload başlangıcına gider. */
}

/*
 * Kullanıcı pointer'ından tekrar ilgili blok Header bilgisine ulaşır.
 * Free, hata kontrolü ve bütünlük denetimleri bu geri dönüşüm üzerinden çalışır.
 */
block_header_t *allocator_payload_to_block(void *ptr)
{
    block_header_t *block;

    if (ptr == NULL) {
        return NULL;
    }

    /*
     * Kullanıcıya verilen payload işaretçisinin hemen öncesinde
     * allocator tarafından yönetilen Header metadata alanı bulunur.
     */
    block = ((block_header_t *)ptr) - 1; /* [Header][payload] düzeninde payload adresinden bir Header geri gelir. */

    if (!allocator_is_valid_magic(block->magic)) {
        fprintf(stderr,
                "allocator uyarısı: geçersiz Header imzası algılandı; "
                "pointer geçersiz veya bellek bozulmuş olabilir.\n");
        return NULL;
    }

    return block;
}

/*
 * Bir bloğu free list başına ekler.
 * Listenin başına ekleme yapıldığı için işlem sabit zamanlı ve basit kalır.
 */
void allocator_add_to_free_list(block_header_t *block)
{
    if (block == NULL) {
        return;
    }

    /*
     * Serbest bırakılmış bloklar free list başına eklenir; bu yaklaşım ekleme
     * maliyetini sabit tutar ve tarama stratejilerinin dışarıdan değişmesine
     * engel olmaz.
     */
    block->is_free = 1;
    block->magic = ALLOCATOR_MAGIC_FREE;
    block->prev_free = NULL;
    block->next_free = g_free_list_head; /* Yeni blok eski başı gösterir; [yeni baş] -> [eski baş]. */

    if (g_free_list_head != NULL) {
        g_free_list_head->prev_free = block; /* Eski başın geri bağı yeni bloğa çevrilir. */
    }

    g_free_list_head = block; /* Listenin yeni başlangıcı artık bu bloktur. */
}

/*
 * Bir bloğu free list içinden güvenli şekilde çıkarır.
 * Çift bağlı yapı sayesinde komşu bağlar doğrudan güncellenebilir.
 */
void allocator_remove_from_free_list(block_header_t *block)
{
    if (block == NULL) {
        return;
    }

    /*
     * Çift bağlı yapı kullanıldığı için blok, free list içinden doğrusal arama
     * olmadan güvenli şekilde çıkarılabilir.
     */
    if (block->prev_free != NULL) {
        block->prev_free->next_free = block->next_free; /* Sol komşu sağ komşuya bağlanır. */
    } else if (g_free_list_head == block) {
        g_free_list_head = block->next_free; /* Çıkan blok baş ise baş işaretçisi ilerletilir. */
    }

    if (block->next_free != NULL) {
        block->next_free->prev_free = block->prev_free; /* Sağ komşunun geri bağı sol komşuya çekilir. */
    }

    block->next_free = NULL;
    block->prev_free = NULL;
}

/*
 * Allocator için ilk heap havuzunu oluşturur ve başlangıç durumunu hazırlar.
 * Bu aşamada işletim sisteminden alan alınır, ilk blok yaratılır ve free list başlatılır.
 */
int allocator_init(size_t initial_pool_size)
{
    size_t aligned_pool_size;
    size_t total_region_size;
    void *current_break;
    void *region_start;
    block_header_t *initial_block;

    if (g_allocator_initialized) {
        return 0;
    }

    if (initial_pool_size == 0) {
        initial_pool_size = ALLOCATOR_DEFAULT_POOL_SIZE;
    }

    aligned_pool_size = allocator_align_size(initial_pool_size);
    if (aligned_pool_size == 0 ||
        allocator_calculate_region_size(aligned_pool_size, &total_region_size) != 0) {
        return -1;
    }

    current_break = sbrk(0); /* İşletim sisteminin o anki program break sınırını okur. */
    if (current_break == ALLOCATOR_SBRK_FAILURE) {
        fprintf(stderr,
                "allocator hatası: başlangıç program break değeri alınamadı: %s\n",
                strerror(errno));
        return -1;
    }

    g_heap_start = current_break;

    /* Başlangıç havuzu, işletim sisteminden tek parça bir bölge olarak alınır. */
    region_start = sbrk((intptr_t)total_region_size); /* Program break'i ileri taşıyıp yeni heap bölgesi ister. */
    if (region_start == ALLOCATOR_SBRK_FAILURE) {
        fprintf(stderr,
                "allocator hatası: başlangıç havuzu ayrılamadı: %s\n",
                strerror(errno));
        return -1;
    }

    total_reserved_memory += total_region_size;

    initial_block = allocator_create_block(region_start, total_region_size, 1);
    if (initial_block == NULL) {
        return -1;
    }

    allocator_append_to_block_list(initial_block);
    allocator_add_to_free_list(initial_block);
    g_allocator_initialized = 1;

    return 0;
}

/*
 * Free list içinde uygun blok yoksa işletim sisteminden yeni bir bölge ister.
 * Böylece allocator, mevcut havuz yetmediğinde heap'i kontrollü şekilde büyütebilir.
 */
block_header_t *allocator_request_from_os(size_t size)
{
    size_t aligned_size;
    size_t total_region_size;
    void *region_start;
    block_header_t *new_block;

    aligned_size = allocator_align_size(size);
    if (aligned_size == 0) {
        return NULL;
    }

    if (allocator_calculate_region_size(aligned_size, &total_region_size) != 0) {
        return NULL;
    }

    /*
     * Free list içinde uygun blok bulunamadığında heap genişletilir ve
     * işletim sisteminden yeni bir bölge alınır.
     */
    region_start = sbrk((intptr_t)total_region_size); /* İstenen toplam bölge kadar program break'i ilerletir. */
    if (region_start == ALLOCATOR_SBRK_FAILURE) {
        fprintf(stderr,
                "allocator hatası: heap genişletilemedi: %s\n",
                strerror(errno));
        return NULL;
    }

    total_reserved_memory += total_region_size;

    new_block = allocator_create_block(region_start, total_region_size, 0);
    if (new_block == NULL) {
        return NULL;
    }

    allocator_append_to_block_list(new_block);
    return new_block;
}

/*
 * Varsayılan blok arama politikasını uygular.
 * free list baştan sona gezilir ve yeterli ilk blok seçilir.
 */
block_header_t *allocator_default_find_free_block(size_t size)
{
    block_header_t *current;

    current = g_free_list_head;
    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            return current;
        }

        current = current->next_free;
    }

    return NULL;
}

/*
 * my_malloc ve my_free, allocator_ops.c ve allocator_safety.c modüllerinde
 * implementasyonları ile birlikte sunulmaktadır.
 * Bu çekirdek modül, tahsis ve serbest bırakmanın altyapısını sağlar.
 */

/*
 * Yeni oluşturulan blokları tüm-heap zincirinin sonuna ekler.
 * Bu zincir, free list'ten bağımsız olarak tüm blokları adres sırasına yakın biçimde takip eder.
 */
static void allocator_append_to_block_list(block_header_t *block)
{
    block_header_t *tail;

    if (block == NULL) {
        return;
    }

    /*
     * Tüm bloklar ayrı bir zincirde tutulur; bu yapı ileride leak raporu,
     * fragmentation hesabı ve bütünlük kontrolleri için temel oluşturur.
     */
    block->next_all = NULL;
    block->prev_all = NULL;

    if (g_block_list_head == NULL) {
        g_block_list_head = block;
        return;
    }

    tail = g_block_list_head;
    while (tail->next_all != NULL) {
        tail = tail->next_all;
    }

    tail->next_all = block; /* Eski son bloğun ileri bağı yeni bloğu gösterir. */
    block->prev_all = tail; /* Yeni blok geri yönde eski sona bağlanır. */
}

/*
 * İşletim sisteminden alınan ham bölgeyi anlamlı bir blok yapısına dönüştürür.
 * Header alanı doldurulur, payload kapasitesi hesaplanır ve bağlantılar temizlenir.
 */
static block_header_t *allocator_create_block(void *region_start,
                                              size_t total_region_size,
                                              int is_free)
{
    block_header_t *block;

    if (region_start == NULL || total_region_size <= sizeof(block_header_t)) {
        return NULL;
    }

    /*
     * Her yeni bölge, Header ile başlatılır ve geri kalan alan payload kapasitesi
     * olarak kaydedilir.
     */
    block = (block_header_t *)region_start; /* Ham adresi block_header_t olarak yorumlar. */
    block->size = total_region_size - sizeof(block_header_t); /* [Header][payload] düzeninde payload kapasitesini hesaplar. */
    block->is_free = is_free;
    block->magic = is_free ? ALLOCATOR_MAGIC_FREE : ALLOCATOR_MAGIC_ALLOC;
    block->next_free = NULL;
    block->prev_free = NULL;
    block->next_all = NULL;
    block->prev_all = NULL;
    memset(block->padding, 0, sizeof(block->padding)); /* Padding alanını deterministik hale getirir. */

    return block;
}

/*
 * Bir Header içindeki magic değerin geçerli olup olmadığını kontrol eder.
 * Böylece bellek bozulması veya sahte pointer kullanımı erken fark edilebilir.
 */
static int allocator_is_valid_magic(uint32_t magic)
{
    return magic == ALLOCATOR_MAGIC_ALLOC || magic == ALLOCATOR_MAGIC_FREE;
}

/*
 * Toplam istenecek bölge boyutunu güvenli şekilde hesaplar.
 * Amaç, Header maliyetini eklerken taşma oluşmasını engellemektir.
 */
static int allocator_calculate_region_size(size_t payload_size,
                                           size_t *total_region_size)
{
    if (total_region_size == NULL) {
        return -1;
    }

    if (payload_size > (SIZE_MAX - sizeof(block_header_t))) {
        return -1;
    }

    *total_region_size = sizeof(block_header_t) + payload_size; /* Toplam bölge = [Header] + [payload] olarak hesaplanır. */
    return 0;
}
