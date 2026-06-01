#include "allocator.h"
#include "allocator_threadsafe.h"

#include <stdio.h>

/*
 * Fragmentation ve kullanım istatistiklerini hesaplayan Person 3 raporlama modülü.
 * Heap üzerindeki tüm blok listesi okunurken allocator_mutex tutulur; böylece aynı
 * anda çalışan thread'ler listeyi değiştirirken rapor fonksiyonu tutarsız veri okumaz.
 */
void print_memory_stats(void)
{
    block_header_t *current;
    size_t free_block_count = 0;
    size_t allocated_block_count = 0;
    size_t total_free_memory = 0;
    size_t largest_free_block = 0;
    double external_fragmentation = 0.0;
    double usage_ratio = 0.0;

    pthread_mutex_lock(&allocator_mutex);

    /* Tüm blok listesi tek geçişte taranır: hem blok sayıları hem serbest alan ölçülür. */
    current = allocator_get_block_list_head();
    while (current != NULL) {
        if (current->is_free) {
            free_block_count++;
            total_free_memory += current->size;
            if (current->size > largest_free_block) {
                largest_free_block = current->size;
            }
        } else {
            allocated_block_count++;
        }

        current = current->next_all;
    }

    /*
     * External fragmentation:
     * Serbest belleğin ne kadarı en büyük tek blok dışında parçalanmış durumda?
     */
    if (total_free_memory > 0) {
        external_fragmentation =
            (1.0 - ((double)largest_free_block / (double)total_free_memory)) * 100.0;
    }

    /* Toplam kullanım oranı, ayrılmış payload'ın OS'ten rezerve edilen alana oranıdır. */
    if (total_reserved_memory > 0) {
        usage_ratio = ((double)total_allocated_memory / (double)total_reserved_memory) * 100.0;
    }

    printf("\n========== Bellek Yoneticisi Istatistikleri ==========\n");
    printf("Toplam rezerve bellek      : %zu byte (%.2f KB)\n",
           total_reserved_memory, (double)total_reserved_memory / 1024.0);
    printf("Kullanımdaki payload       : %zu byte (%.2f KB)\n",
           total_allocated_memory, (double)total_allocated_memory / 1024.0);
    printf("Toplam serbest payload     : %zu byte (%.2f KB)\n",
           total_free_memory, (double)total_free_memory / 1024.0);
    printf("Aktif blok sayısı          : %zu\n", active_block_count);
    printf("Tahsisli blok sayısı       : %zu\n", allocated_block_count);
    printf("Serbest blok sayısı        : %zu\n", free_block_count);
    printf("En büyük serbest blok      : %zu byte\n", largest_free_block);
    printf("External fragmentation     : %.2f%%\n", external_fragmentation);
    printf("Toplam kullanım oranı      : %.2f%%\n", usage_ratio);
    printf("======================================================\n\n");

    pthread_mutex_unlock(&allocator_mutex);
}
