#include "allocator.h"

#include <stdio.h>
#include <string.h>

/*
 * Yerleştirme Stratejileri (Placement Strategies)
 *
 * Bu modül blok seçimi için farklı algoritmalar sunmaktadır.
 * Her stratejinin performance özellikleri farklıdır:
 * - first-fit: hızlı ama fragmentation'a yatkın
 * - best-fit: fragmentation'ı azaltır ama daha yavaş
 */

/*
 * First-fit stratejisi: free list'te ilk uygun blok döndürülür.
 * Avantaj: O(n) ama n genellikle küçüktür, çabuk sonlanır.
 */
block_header_t *allocator_strategy_first_fit(size_t size)
{
    block_header_t *current = allocator_get_free_list_head();

    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            return current;
        }
        current = current->next_free;
    }

    return NULL;
}

/*
 * Best-fit stratejisi: istenen boyuta en yakın (en küçük yeterli) blok seçilir.
 * Avantaj: internal fragmentation'ı minimize eder, bellek kullanımı daha verimli.
 * Dezavantaj: tüm free list gezilmesi gerekir, biraz daha yavaştır.
 */
block_header_t *allocator_strategy_best_fit(size_t size)
{
    block_header_t *current = allocator_get_free_list_head();
    block_header_t *best_fit = NULL;
    size_t best_fit_size = (size_t)-1; /* SIZE_MAX */

    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            if (current->size < best_fit_size) {
                best_fit = current;
                best_fit_size = current->size;
            }
        }
        current = current->next_free;
    }

    return best_fit;
}

/*
 * Blok Bölme (Block Splitting)
 *
 * Seçilen blok istenenden büyükse, gereksiz olan kısım
 * yeni serbest blok olarak ayrılır ve free list'e eklenir.
 * Bu, internal fragmentation'ı azaltır.
 */
void allocator_split_block(block_header_t *block, size_t required_size)
{
    size_t remaining_size;
    block_header_t *new_free_block;
    void *split_point;

    if (block == NULL || block->size <= required_size) {
        return;
    }

    remaining_size = block->size - required_size;

    /*
     * Kalan alan en az bir Header kadar olmalı ve alignment'a uymalı.
     * Aksi durumda bölme yapılmaz.
     */
    if (remaining_size < sizeof(block_header_t) + ALLOCATOR_MIN_BLOCK_SIZE) {
        return;
    }

    /* Yeni blok, önceki bloğun kalan kısmından oluşturulacak. */
    split_point = (void *)((uint8_t *)allocator_block_to_payload(block) + required_size);
    new_free_block = (block_header_t *)split_point;

    /* Yeni blok, eski bloğun hemen ardına geldiğinden metadata başlatılır. */
    new_free_block->size = remaining_size - sizeof(block_header_t);
    new_free_block->is_free = 1;
    new_free_block->magic = ALLOCATOR_MAGIC_FREE;
    new_free_block->next_free = NULL;
    new_free_block->prev_free = NULL;

    /* Yeni blok, all-blocks zincirinde eski bloğun hemen ardına eklenir. */
    new_free_block->next_all = block->next_all;
    new_free_block->prev_all = block;

    if (block->next_all != NULL) {
        block->next_all->prev_all = new_free_block;
    }

    block->next_all = new_free_block;

    /* Bölünen bloğun boyutu güncellenir. */
    block->size = required_size;

    /* Yeni blok serbest list'e eklenir. */
    allocator_add_to_free_list(new_free_block);
}

/*
 * Blok Birleştirme (Coalescing)
 *
 * Serbest bırakılan bir blok etrafındaki komşu serbest blokları
 * birleştirerek external fragmentation'ı azaltır.
 * Çift bağlı all-blocks listesinde önce ve sonra kontrol edilir.
 */
void allocator_coalesce_blocks(block_header_t *block)
{
    block_header_t *prev_block;
    block_header_t *next_block;

    if (block == NULL || !block->is_free) {
        return;
    }

    /* Önceki blok serbest mi? Evet ise birleştirilir. */
    prev_block = block->prev_all;
    if (prev_block != NULL && prev_block->is_free) {
        /* Önceki blok, bu bloğun tamamını içerecek şekilde genişletilir. */
        prev_block->size += sizeof(block_header_t) + block->size;
        prev_block->next_all = block->next_all;

        if (block->next_all != NULL) {
            block->next_all->prev_all = prev_block;
        }

        /* Şu anki blok free list'ten çıkarılır. */
        allocator_remove_from_free_list(block);

        /* Önceki bloğa geçilir, çünkü artık başlıca blok bu olmuştur. */
        block = prev_block;
    }

    /* Sonraki blok serbest mi? Evet ise birleştirilir. */
    next_block = block->next_all;
    if (next_block != NULL && next_block->is_free) {
        /* Şu anki blok, sonraki bloğu da içerecek şekilde genişletilir. */
        block->size += sizeof(block_header_t) + next_block->size;
        block->next_all = next_block->next_all;

        if (next_block->next_all != NULL) {
            next_block->next_all->prev_all = block;
        }

        /* Sonraki blok free list'ten çıkarılır. */
        allocator_remove_from_free_list(next_block);
    }
}

/*
 * my_malloc: Bellek tahsisi
 *
 * İstenen boyutta bellek tahsis eder. İç olarak:
 * 1. Strateji kullanarak uygun free blok bulunur (best-fit veya first-fit)
 * 2. Blok gerekirse bölünür (splitting)
 * 3. Blok kullanımda olarak işaretlenir
 * 4. Payload adresi döndürülür
 */
void *my_malloc(size_t size)
{
    size_t aligned_size;
    block_header_t *selected_block;

    if (size == 0) {
        return NULL;
    }

    if (!g_allocator_initialized && allocator_init(ALLOCATOR_DEFAULT_POOL_SIZE) != 0) {
        return NULL;
    }

    aligned_size = allocator_align_size(size);
    if (aligned_size == 0) {
        return NULL;
    }

    /* Strateji fonksiyonu kullanılarak uygun blok bulunur. */
    if (g_strategy_fn != NULL) {
        selected_block = g_strategy_fn(aligned_size);
    } else {
        selected_block = allocator_default_find_free_block(aligned_size);
    }

    if (selected_block != NULL) {
        /* Blok splitting: gerekirse fazla kısmı ayırır. */
        allocator_split_block(selected_block, aligned_size);

        allocator_remove_from_free_list(selected_block);
        selected_block->is_free = 0;
        selected_block->magic = ALLOCATOR_MAGIC_ALLOC;
        total_allocated_memory += selected_block->size;
        active_block_count++;

        return allocator_block_to_payload(selected_block);
    }

    /* Free list'te blok bulunamadıysa işletim sisteminden yeni bölge istenir. */
    selected_block = allocator_request_from_os(aligned_size);
    if (selected_block == NULL) {
        return NULL;
    }

    selected_block->is_free = 0;
    selected_block->magic = ALLOCATOR_MAGIC_ALLOC;
    total_allocated_memory += selected_block->size;
    active_block_count++;

    return allocator_block_to_payload(selected_block);
}

/*
 * my_free: Bellek serbest bırakma
 *
 * 1. Geçersiz pointer kontrolü (magic number)
 * 2. Double-free tespiti
 * 3. Blok free list'e eklenir
 * 4. Komşu serbest bloklar birleştirilir (coalescing)
 */
void my_free(void *ptr)
{
    block_header_t *block;

    if (ptr == NULL) {
        return;
    }

    block = allocator_payload_to_block(ptr);
    if (block == NULL) {
        fprintf(stderr,
                "allocator hatası: geçersiz pointer serbest bırakılmaya çalışıldı\n");
        return;
    }

    /* Double-free tespiti */
    if (block->is_free) {
        fprintf(stderr,
                "allocator hatası: zaten serbest olan blok tekrar serbest bırakılmaya çalışıldı "
                "(double-free)\n");
        return;
    }

    /* Blok istatistikleri güncellenir. */
    if (total_allocated_memory >= block->size) {
        total_allocated_memory -= block->size;
    }
    if (active_block_count > 0) {
        active_block_count--;
    }

    /* Blok serbest list'e eklenir. */
    allocator_add_to_free_list(block);

    /* Komşu serbest bloklar birleştirilir. */
    allocator_coalesce_blocks(block);
}

/*
 * my_calloc: Tahsisli bellek oluşturma (calloc = clear + allocate)
 *
 * my_malloc gibi bellek tahsis eder, ancak
 * tahsis edilen alanı memset ile sıfırlarır.
 * Böylece başlangıçta tüm baytlar 0x00 olur.
 */
void *my_calloc(size_t nmemb, size_t size)
{
    size_t total_size;
    void *ptr;

    if (nmemb == 0 || size == 0) {
        return NULL;
    }

    /* Taşma kontrolü: nmemb * size > SIZE_MAX ? */
    if (nmemb > (size_t)-1 / size) {
        fprintf(stderr,
                "allocator hatası: my_calloc boyut taşması\n");
        return NULL;
    }

    total_size = nmemb * size;
    ptr = my_malloc(total_size);

    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}
