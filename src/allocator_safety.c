#include "allocator.h"

#include <stdio.h>

/*
 * Hata Kontrolü ve Güvenlik (Error Detection and Safety)
 *
 * Bu modül allocator'ın bütünlüğünü korumak için
 * çeşitli kontrol ve doğrulama işlevleri sağlamaktadır.
 */

/*
 * Blok Doğrulaması
 *
 * Bir bloğun geçerli olup olmadığını kontrol eder.
 * Geçersiz bloklar bellek bozulması veya yanlış pointer kullanımını gösterir.
 */
int allocator_is_valid_block(block_header_t *block)
{
    if (block == NULL) {
        return 0;
    }

    /* Magic number kontrolü */
    if (block->magic != ALLOCATOR_MAGIC_ALLOC && block->magic != ALLOCATOR_MAGIC_FREE) {
        return 0;
    }

    /* Boyut kontrolü */
    if (block->size == 0) {
        return 0;
    }

    return 1;
}

/*
 * Blok Tahsis Durumu Kontrolü
 *
 * Bir bloğun tahsis edilmiş (allocated) olup olmadığını kontrol eder.
 * Serbest bloklar 0, tahsis edilmiş bloklar 1 döndürür.
 */
int allocator_is_block_allocated(block_header_t *block)
{
    if (block == NULL) {
        return 0;
    }

    return !block->is_free && block->magic == ALLOCATOR_MAGIC_ALLOC;
}

/*
 * Bellek Sızıntısı Raporu (Memory Leak Report)
 *
 * Heap üzerinde gezinerek henüz serbest bırakılmamış
 * (allocated durumda olan) tüm blokları listeler.
 * Program kapanmadan önce çağrılabilir.
 */
void allocator_report_memory_leaks(void)
{
    block_header_t *current;
    size_t leak_count = 0;
    size_t leaked_bytes = 0;

    printf("\n========== Bellek Sızıntısı Raporu ==========\n");

    current = allocator_get_block_list_head();
    while (current != NULL) {
        if (allocator_is_block_allocated(current)) {
            printf("  SIZZINTI: Adres=%p, Boyut=%zu byte\n",
                   allocator_block_to_payload(current), current->size);
            leak_count++;
            leaked_bytes += current->size;
        }

        current = current->next_all;
    }

    if (leak_count == 0) {
        printf("  Sızıntı tespit edilmedi - Tümü temizlendi! ✓\n");
    } else {
        printf("  TOPLAM SIZZINTI: %zu blok, %zu byte\n", leak_count, leaked_bytes);
    }

    printf("==========================================\n\n");
}

/*
 * Allocator İstatistikleri Yazdırma (Print Statistics)
 *
 * Allocator'ın mevcut durumunu gösteren kapsamlı istatistikler
 * yazdırır: tahsis edilen bellek, serbest bellek, blok sayıları, vb.
 */
void allocator_print_stats(void)
{
    block_header_t *current;
    size_t free_block_count = 0;
    size_t allocated_block_count = 0;
    size_t total_free_memory = 0;
    size_t largest_free_block = 0;
    size_t fragmentation_ratio;

    current = allocator_get_free_list_head();
    while (current != NULL) {
        if (current->is_free) {
            free_block_count++;
            total_free_memory += current->size;
            if (current->size > largest_free_block) {
                largest_free_block = current->size;
            }
        }
        current = current->next_free;
    }

    current = allocator_get_block_list_head();
    while (current != NULL) {
        if (!current->is_free) {
            allocated_block_count++;
        }
        current = current->next_all;
    }

    /* External fragmentation oranı hesaplanır */
    if (total_reserved_memory > 0) {
        fragmentation_ratio = (total_reserved_memory - total_allocated_memory) * 100
                            / total_reserved_memory;
    } else {
        fragmentation_ratio = 0;
    }

    printf("\n========== Allocator İstatistikleri ==========\n");
    printf("Toplam Ayrılan Bellek:     %zu byte (%.2f KB)\n",
           total_reserved_memory, (double)total_reserved_memory / 1024);
    printf("Tahsis Edilen Bellek:      %zu byte (%.2f KB)\n",
           total_allocated_memory, (double)total_allocated_memory / 1024);
    printf("Serbest Bellek:            %zu byte (%.2f KB)\n",
           total_free_memory, (double)total_free_memory / 1024);
    printf("\nBlok Sayıları:\n");
    printf("  Tahsis Edilmiş Blok:     %zu\n", allocated_block_count);
    printf("  Serbest Blok:            %zu\n", free_block_count);
    printf("  Aktif Blok:              %zu\n", active_block_count);
    printf("\nFragmentation Analizi:\n");
    printf("  En Büyük Serbest Blok:   %zu byte (%.2f KB)\n",
           largest_free_block, (double)largest_free_block / 1024);
    printf("  External Fragmentation:  %zu%%\n", fragmentation_ratio);
    printf("\nKullanım Oranı:            %.2f%%\n",
           total_reserved_memory > 0 ? (double)total_allocated_memory * 100 / total_reserved_memory
                                     : 0.0);
    printf("===============================================\n\n");
}
