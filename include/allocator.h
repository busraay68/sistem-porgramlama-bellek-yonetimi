#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALLOCATOR_ALIGNMENT 16UL                    /* Tüm payload adresleri için temel alignment sınırı. */
#define ALLOCATOR_MIN_BLOCK_SIZE 16UL               /* Çok küçük tahsislerde alignment/padding etkisini dengeleyen alt payload sınırı. */
#define ALLOCATOR_DEFAULT_POOL_SIZE (64UL * 1024UL) /* Başlangıçta ayrılan varsayılan heap havuzu boyutu. */
#define ALLOCATOR_MAGIC_ALLOC 0xC0FFEE01U /* Ayrılmış blok imzası; memory corruption ve geçersiz işlemleri saptamada kullanılır. */
#define ALLOCATOR_MAGIC_FREE 0xC0FFEE02U  /* Serbest blok imzası; bozulmuş Header bilgisi ve hatalı durum geçişlerini yakalamaya yardımcı olur. */

#define BLOCK_HEADER_POINTER_COUNT 4UL /* Header içindeki pointer alanlarının sayısı. */
#define BLOCK_HEADER_RAW_SIZE                                                      \
    (sizeof(size_t) + sizeof(int) + sizeof(uint32_t) +                            \
     (BLOCK_HEADER_POINTER_COUNT * sizeof(void *))) /* Header içindeki temel metadata alanlarının toplamı. */
#define BLOCK_HEADER_PADDING_SIZE                                                  \
    ((ALLOCATOR_ALIGNMENT - (BLOCK_HEADER_RAW_SIZE % ALLOCATOR_ALIGNMENT)) %      \
     ALLOCATOR_ALIGNMENT) /* Boyutu alignment sınırına tamamlamak için gereken padding. */
/*
 * Doğal yerleşim zaten hizalıysa bile bir alignment birimi ayrılır.
 * Böylece Header boyutu her platformda öngörülebilir bir hizalı sınırda kalır
 * ve gelecekte eklenecek metadata alanları için güvenli bir rezerv bırakılır.
 */
#define BLOCK_HEADER_RESERVED_PADDING_SIZE                                         \
    (BLOCK_HEADER_PADDING_SIZE == 0 ? ALLOCATOR_ALIGNMENT : /* Sıfırsa tam bir alignment bloğu ayırır. */ \
                                      BLOCK_HEADER_PADDING_SIZE) /* Sıfır boyutlu dizi oluşmasını önleyen güvenli padding alanı. */

typedef struct block_header block_header_t;
typedef block_header_t *(*allocator_strategy_fn)(size_t size);

/*
 * Her blok için kullanıcıdan gizlenen temel metadata alanı.
 * size           : Blok içindeki kullanılabilir payload boyutu
 * is_free        : Bloğun free list içinde olup olmadığını belirtir
 * magic          : Header bütünlüğünü doğrulamak için kullanılan imza değeri
 * next/prev_free : Free list bağlantıları
 * next/prev_all  : Heap üzerindeki tüm blokların zinciri
 */
struct block_header {
    size_t size;                 /* Bu blokta kullanıcıya açılan payload boyutu. */
    int is_free;                 /* Blok kullanımda mı, free list içinde mi bilgisini taşır. */
    uint32_t magic;              /* Header bozulmuş mu kontrol etmek için imza değeri. */
    block_header_t *next_free;   /* [Önceki free blok] <-> [Bu blok] <-> [Sonraki free blok] zincirindeki ileri bağ. */
    block_header_t *prev_free;   /* Free list içinde geri yönde dolaşmayı sağlar. */
    block_header_t *next_all;    /* Heap üstündeki tüm blokları sırayla gezmek için ileri bağ. */
    block_header_t *prev_all;    /* Heap zincirinde geri yönde ilerlemeyi sağlar. */
    /*
     * Bu padding alanı, Header boyutunu ALLOCATOR_ALIGNMENT değerinin katına
     * taşır ve gelecekte eklenecek metadata alanları için düzenli bir boşluk
     * bırakır.
     */
    uint8_t padding[BLOCK_HEADER_RESERVED_PADDING_SIZE]; /* Header boyutunu hizalı sınırda sabitler. */
};

_Static_assert(sizeof(block_header_t) % ALLOCATOR_ALIGNMENT == 0,
               "block_header_t boyutu alignment sinirinin kati olmalidir");
_Static_assert((ALLOCATOR_ALIGNMENT & (ALLOCATOR_ALIGNMENT - 1)) == 0,
               "ALLOCATOR_ALIGNMENT power-of-two olmalidir");
_Static_assert((ALLOCATOR_MIN_BLOCK_SIZE % ALLOCATOR_ALIGNMENT) == 0,
               "ALLOCATOR_MIN_BLOCK_SIZE alignment siniriyla uyumlu olmalidir");

/*
 * İstatistik altyapısı; farklı modüller bu sayaçları ortak olarak kullanabilir.
 */
extern size_t total_reserved_memory;
extern size_t total_allocated_memory;
extern size_t active_block_count;
extern int g_allocator_initialized;
extern allocator_strategy_fn g_strategy_fn;

/* Allocator yaşam döngüsü ve yardımcı işlemler */
int allocator_init(size_t initial_pool_size);
size_t allocator_align_size(size_t size);
void allocator_set_strategy(allocator_strategy_fn strategy_fn);

block_header_t *allocator_request_from_os(size_t size);
block_header_t *allocator_default_find_free_block(size_t size);

void allocator_add_to_free_list(block_header_t *block);
void allocator_remove_from_free_list(block_header_t *block);

block_header_t *allocator_get_free_list_head(void);
block_header_t *allocator_get_block_list_head(void);

void *allocator_block_to_payload(block_header_t *block);
block_header_t *allocator_payload_to_block(void *ptr);

/* Temel allocator API */
void *my_malloc(size_t size);
void my_free(void *ptr);
void *my_calloc(size_t nmemb, size_t size);

/* İşlemler ve strateji fonksiyonları */
block_header_t *allocator_strategy_best_fit(size_t size);
block_header_t *allocator_strategy_first_fit(size_t size);
void allocator_split_block(block_header_t *block, size_t required_size);
void allocator_coalesce_blocks(block_header_t *block);

/* Hata kontrolü ve güvenlik */
int allocator_is_valid_block(block_header_t *block);
int allocator_is_block_allocated(block_header_t *block);
void allocator_report_memory_leaks(void);
void allocator_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* ALLOCATOR_H */
