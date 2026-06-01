# Implementasyon Detayları (Person 2)

Bu dokümanda, Person 2 tarafından uygulanan yerleştirme stratejileri, blok bölme, serbest bırakma, coalescing, calloc ve hata kontrol mekanizmaları detaylı olarak açıklanmıştır.

---

## Dosya Yapısı

```
Person 2 Sorumluluğu:
├── allocator_ops.c
│   ├── allocator_strategy_first_fit()     - First-fit yerleştirme
│   ├── allocator_strategy_best_fit()      - Best-fit yerleştirme
│   ├── allocator_split_block()            - Blok bölme
│   ├── allocator_coalesce_blocks()        - Blok birleştirme
│   ├── my_malloc()                        - Geliştirilmiş tahsis
│   ├── my_free()                          - Serbest bırakma
│   └── my_calloc()                        - Sıfırlanmış tahsis
│
└── allocator_safety.c
    ├── allocator_is_valid_block()         - Blok doğrulama
    ├── allocator_is_block_allocated()     - Tahsis durumu kontrolü
    ├── allocator_report_memory_leaks()    - Sızıntı raporu
    └── allocator_print_stats()            - İstatistikler
```

---

## Yerleştirme Stratejileri Detayları

### First-Fit (allocator_strategy_first_fit)

**Mantık:**
```c
block_header_t *allocator_strategy_first_fit(size_t size)
{
    block_header_t *current = allocator_get_free_list_head();
    
    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            return current;  // İlk uygun bulundu
        }
        current = current->next_free;
    }
    
    return NULL;  // Uygun blok yok
}
```

**Adımlar:**
1. Free list başından başla (`allocator_get_free_list_head()`)
2. Her blok için:
   - `is_free` kontrolü
   - `size >= istenen boyut` kontrolü
3. Koşul sağlanırsa hemen dön
4. Aksi takdirde sonraki bloka geç
5. Uygun blok bulunamadıysa `NULL` dön

**Avantajlar:**
- Hızlı: Ortalama O(n), ama çoğu zaman erken çıkış
- Simple: Basit implementasyon
- Cache-friendly: Sık tahsis edilen blokları tercih

**Dezavantajlar:**
- Fragmentation: İlk tahsis edilen bloktan arta kalırsa hala serbest
- Worst-case: O(n) tam tarama gerekebilir

**Performans:**
- Best-case: O(1) - ilk blok uygun
- Average-case: O(n/2) - orta noktada bulunur
- Worst-case: O(n) - son blok veya yok

---

### Best-Fit (allocator_strategy_best_fit)

**Mantık:**
```c
block_header_t *allocator_strategy_best_fit(size_t size)
{
    block_header_t *current = allocator_get_free_list_head();
    block_header_t *best_fit = NULL;
    size_t best_fit_size = (size_t)-1;  // SIZE_MAX
    
    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            if (current->size < best_fit_size) {
                best_fit = current;
                best_fit_size = current->size;
            }
        }
        current = current->next_free;
    }
    
    return best_fit;  // En iyi uygun veya NULL
}
```

**Adımlar:**
1. Free list başından başla
2. Tüm blokları tara (erken çıkış yok!)
3. Her uygun blok için:
   - Boyutunu en iyi seçilen ile karşılaştır
   - Daha küçükse: güncelle (`best_fit`, `best_fit_size`)
4. En küçük uygun bloğu dön

**Avantajlar:**
- Internal frag. azalır: Daha yakın boyut seçilir
- Büyük bloklar korunur: Sonraki büyük tahsis için

**Dezavantajlar:**
- Daha yavaş: O(n) tam tarama her zaman
- Bellek parçalanması: Küçük bloklar oluşabilir

**Performans:**
- Best-case: O(n)
- Average-case: O(n)
- Worst-case: O(n)

---

## Blok Bölme (Block Splitting)

### Konsept

Seçilen blok istenenden büyükse, gereksiz kısım ayrılır:

```
Bölme öncesi:
Heap:  [Header₁][=============== 512 byte ===============]

my_malloc(256) sonrası:
Heap:  [Header₁][=== 256 ===][Header₂][=== 256 serbest ===]
                              ↓
                      Free list'e eklenir
```

### Implementasyon

```c
void allocator_split_block(block_header_t *block, size_t required_size)
{
    size_t remaining_size;
    block_header_t *new_free_block;
    void *split_point;
    
    // 1. Bölme gerekli mi?
    if (block == NULL || block->size <= required_size) {
        return;  // Gerekli değil
    }
    
    remaining_size = block->size - required_size;
    
    // 2. Kalan alan yeterli mi?
    if (remaining_size < sizeof(block_header_t) + ALLOCATOR_MIN_BLOCK_SIZE) {
        return;  // Çok küçük, splitting yapma
    }
    
    // 3. Yeni blok oluştur
    split_point = (void *)((uint8_t *)allocator_block_to_payload(block) + required_size);
    new_free_block = (block_header_t *)split_point - 1;
    
    // 4. Yeni blok metadata'sını doldur
    new_free_block->size = remaining_size - sizeof(block_header_t);
    new_free_block->is_free = 1;
    new_free_block->magic = ALLOCATOR_MAGIC_FREE;
    new_free_block->next_free = NULL;
    new_free_block->prev_free = NULL;
    
    // 5. All-blocks zincirinde yerleştir
    new_free_block->next_all = block->next_all;
    new_free_block->prev_all = block;
    if (block->next_all != NULL) {
        block->next_all->prev_all = new_free_block;
    }
    block->next_all = new_free_block;
    
    // 6. Eski blok boyutunu güncelle
    block->size = required_size;
    
    // 7. Yeni blok free list'e ekle
    allocator_add_to_free_list(new_free_block);
}
```

### Kritik Noktalar

1. **Bellek Layout Kontrol:**
   ```
   Payload başlangıcı: allocator_block_to_payload(block)
   Yeni header yeri:   payload + required_size - sizeof(header)
   Yeni payload başı:  yeni header + 1
   ```

2. **Minimum Kalan Alan:**
   ```c
   remaining_size >= sizeof(block_header_t) + ALLOCATOR_MIN_BLOCK_SIZE
   // Header (64) + Minimum blok (16) = 80 byte minimum
   ```

3. **Zincir Bağlantıları:**
   - All-blocks zinciri **mutlaka** düzgün tutulmalı
   - Free list otomatik olarak `allocator_add_to_free_list` ile eklenir

---

## Blok Birleştirme (Coalescing)

### Konsept

Serbest bırakılan blok etrafındaki serbest blokları birleştirerek fragmentation azaltır:

```
Birleştirme öncesi:
[serbest₁][KULLANIMDA][serbest₂]

my_free(KULLANIMDA) sonrası:
[========== Birleştirilmiş Serbest ==========]
```

### Implementasyon

```c
void allocator_coalesce_blocks(block_header_t *block)
{
    block_header_t *prev_block;
    block_header_t *next_block;
    
    if (block == NULL || !block->is_free) {
        return;
    }
    
    // 1. Önceki blok serbest mi?
    prev_block = block->prev_all;
    if (prev_block != NULL && prev_block->is_free) {
        // Evet, birleştir
        prev_block->size += sizeof(block_header_t) + block->size;
        prev_block->next_all = block->next_all;
        
        if (block->next_all != NULL) {
            block->next_all->prev_all = prev_block;
        }
        
        allocator_remove_from_free_list(block);
        block = prev_block;  // Merkez bloğu değiştir
    }
    
    // 2. Sonraki blok serbest mi?
    next_block = block->next_all;
    if (next_block != NULL && next_block->is_free) {
        // Evet, birleştir
        block->size += sizeof(block_header_t) + next_block->size;
        block->next_all = next_block->next_all;
        
        if (next_block->next_all != NULL) {
            next_block->next_all->prev_all = block;
        }
        
        allocator_remove_from_free_list(next_block);
    }
}
```

### Adımlar

1. **Önceki Birleştirme:**
   - `prev_block = block->prev_all` (all-blocks zincirinde önceki)
   - Eğer `prev_block` serbest ise:
     - Boyut: `prev_block->size += header + block->size`
     - All-blocks: `prev_block->next_all = block->next_all`
     - Block: Free list'ten çıkar
     - Merkez: `block = prev_block` (artık prev serbest blok)

2. **Sonraki Birleştirme:**
   - `next_block = block->next_all`
   - Eğer `next_block` serbest ise:
     - Boyut: `block->size += header + next_block->size`
     - All-blocks: `block->next_all = next_block->next_all`
     - Block: Free list'ten çıkar

### Neden Iki Adım?

Önceki birleştirme yapıldıktan sonra `block` değiştiğinden, sonraki birleştirme yeni bloğun çevresinde yapılmalıdır.

```
Başlangıç:  [serbest A][KULLANILAN][serbest B]
Adım 1:     A genişler → [========== A ==========]
Adım 2:     Sonra B kontrol → [====== Birleştirilmiş ======]
```

---

## my_malloc Implementasyonu (Geliştirilmiş)

```c
void *my_malloc(size_t size)
{
    size_t aligned_size;
    block_header_t *selected_block;
    
    // 1. Boyut kontrolü
    if (size == 0) {
        return NULL;
    }
    
    // 2. Allocator başlatma (lazy init)
    if (!g_allocator_initialized && allocator_init(ALLOCATOR_DEFAULT_POOL_SIZE) != 0) {
        return NULL;
    }
    
    // 3. Boyut alignment
    aligned_size = allocator_align_size(size);
    if (aligned_size == 0) {
        return NULL;  // Taşma
    }
    
    // 4. Uygun blok bulma (strateji)
    if (g_strategy_fn != NULL) {
        selected_block = g_strategy_fn(aligned_size);
    } else {
        selected_block = allocator_default_find_free_block(aligned_size);
    }
    
    // 5. Blok bulunduysa işle
    if (selected_block != NULL) {
        allocator_split_block(selected_block, aligned_size);  // SPLITTING!
        allocator_remove_from_free_list(selected_block);
        selected_block->is_free = 0;
        selected_block->magic = ALLOCATOR_MAGIC_ALLOC;
        total_allocated_memory += selected_block->size;
        active_block_count++;
        
        return allocator_block_to_payload(selected_block);
    }
    
    // 6. Free list'te blok yok, OS'den iste
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
```

**Akış Özeti:**
1. Boyut doğrulama (`size > 0`)
2. Lazy initialization (gerekirse)
3. Alignment (16-byte boundaries)
4. Strateji ile blok seç (best-fit, first-fit, vs.)
5. **SPLITTING** (yeni!)
6. Free list'ten çıkar
7. Tahsis olarak işaretle
8. İstatistikler güncelle
9. Payload dön

---

## my_free Implementasyonu

```c
void my_free(void *ptr)
{
    block_header_t *block;
    
    // 1. NULL kontrolü
    if (ptr == NULL) {
        return;  // Standart C davranışı
    }
    
    // 2. Payload → Block dönüşümü + Magic kontrol
    block = allocator_payload_to_block(ptr);
    if (block == NULL) {  // Magic bozuk
        fprintf(stderr, "allocator hatası: geçersiz pointer serbest bırakılmaya çalışıldı\n");
        return;
    }
    
    // 3. Double-free tespiti
    if (block->is_free) {
        fprintf(stderr, "allocator hatası: zaten serbest olan blok tekrar serbest bırakılmaya çalışıldı\n");
        return;
    }
    
    // 4. İstatistikler güncelle
    if (total_allocated_memory >= block->size) {
        total_allocated_memory -= block->size;
    }
    if (active_block_count > 0) {
        active_block_count--;
    }
    
    // 5. Free list'e ekle
    allocator_add_to_free_list(block);
    
    // 6. COALESCING! (yeni!)
    allocator_coalesce_blocks(block);
}
```

**Akış Özeti:**
1. NULL kontrolü (yoksay)
2. Payload → Block + Magic doğrulama
3. Double-free tespiti
4. İstatistikler azalt
5. Free list'e ekle
6. **COALESCING** (yeni!)

---

## my_calloc Implementasyonu

```c
void *my_calloc(size_t nmemb, size_t size)
{
    size_t total_size;
    void *ptr;
    
    // 1. Sıfır kontrolü
    if (nmemb == 0 || size == 0) {
        return NULL;
    }
    
    // 2. Taşma kontrolü: nmemb * size > SIZE_MAX ?
    if (nmemb > (size_t)-1 / size) {
        fprintf(stderr, "allocator hatası: my_calloc boyut taşması\n");
        return NULL;
    }
    
    total_size = nmemb * size;
    
    // 3. Bellek tahsis et
    ptr = my_malloc(total_size);
    
    // 4. Sıfırla
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }
    
    return ptr;
}
```

**Özellikler:**
- `my_malloc()` + `memset()`
- Taşma kontrolü yapılır (`nmemb * size`)
- Tüm baytlar sıfırlanır (`0x00`)

---

## Hata Kontrolü Detayları

### allocator_is_valid_block

```c
int allocator_is_valid_block(block_header_t *block)
{
    if (block == NULL) {
        return 0;
    }
    
    // Magic number kontrolü
    if (block->magic != ALLOCATOR_MAGIC_ALLOC && 
        block->magic != ALLOCATOR_MAGIC_FREE) {
        return 0;
    }
    
    // Boyut kontrolü
    if (block->size == 0) {
        return 0;
    }
    
    return 1;
}
```

**Kontroller:**
1. `block != NULL`
2. `magic == 0xC0FFEE01` veya `0xC0FFEE02`
3. `size > 0`

---

### allocator_is_block_allocated

```c
int allocator_is_block_allocated(block_header_t *block)
{
    if (block == NULL) {
        return 0;
    }
    
    return !block->is_free && block->magic == ALLOCATOR_MAGIC_ALLOC;
}
```

**Kontrol:**
- Serbest değil MI? (`!is_free`)
- Magic doğru MU? (`ALLOCATOR_MAGIC_ALLOC`)

---

## Bellek Sızıntısı Raporu

```c
void allocator_report_memory_leaks(void)
{
    block_header_t *current;
    size_t leak_count = 0;
    size_t leaked_bytes = 0;
    
    printf("\n========== Bellek Sızıntısı Raporu ==========\n");
    
    // All-blocks zincirinde gezin
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
    
    // Özet
    if (leak_count == 0) {
        printf("  Sızıntı tespit edilmedi - Tümü temizlendi! ✓\n");
    } else {
        printf("  TOPLAM SIZZINTI: %zu blok, %zu byte\n", leak_count, leaked_bytes);
    }
    
    printf("==========================================\n\n");
}
```

**Sorgulama:**
- All-blocks zinciri gezilir
- Her blok için `allocator_is_block_allocated()` çağrılır
- Tahsis edilmiş bloklar sızıntı olarak işaretlenir
- Toplam sızıntı rapor edilir

---

## İstatistikler (allocator_print_stats)

Kapsamlı metrikleri hesaplar ve yazdırır:

- **Bellek Metrikleri:**
  - Toplam ayrılan bellek (reserved)
  - Tahsis edilen bellek (allocated)
  - Serbest bellek (free)

- **Blok Metrikleri:**
  - Tahsis edilmiş blok sayısı
  - Serbest blok sayısı
  - Aktif blok sayısı

- **Fragmentation Analizi:**
  ```
  External Frag = (Serbest - En Büyük Serbest) / Serbest * 100
  ```

- **Kullanım Oranı:**
  ```
  Oran = Tahsis Edilen / Toplam Ayrılan * 100
  ```

---

## Test Senaryoları

### Scenario 1: Basic Allocation

```c
int *arr = (int *)my_malloc(100 * sizeof(int));
assert(arr != NULL);
arr[0] = 42;
my_free(arr);
```

### Scenario 2: Block Splitting

```c
// Büyük blok tahsis et
char *big = (char *)my_malloc(1000);

// İçinden küçük blok tahsis et
int *small = (int *)my_malloc(100);  // Split olur

// İkisini serbest bırak
my_free(big);
my_free(small);
```

### Scenario 3: Coalescing

```c
char *a = (char *)my_malloc(100);
char *b = (char *)my_malloc(100);
char *c = (char *)my_malloc(100);

my_free(b);  // b serbest, ama a,c kullanımda
my_free(a);  // a+b birleştirilir
my_free(c);  // Tümü birleştirilir
```

### Scenario 4: Best-Fit vs First-Fit

```c
allocator_set_strategy(allocator_strategy_best_fit);
// Küçük fragmentation, daha iyi bellek kullanımı

allocator_set_strategy(allocator_strategy_first_fit);
// Daha hızlı, ama fragmentation artabilir
```

---

## Debug İpuçları

1. **Magic Corruption Kontrol:**
   ```c
   block_header_t *blk = allocator_payload_to_block(ptr);
   printf("Magic: 0x%08X\n", blk->magic);  // Doğru mu?
   ```

2. **Bellek Düzeni Görselleştirme:**
   ```c
   allocator_print_stats();
   ```

3. **Sızıntı Bulma:**
   ```c
   allocator_report_memory_leaks();
   ```

4. **Pointer Geçerliliği:**
   ```c
   if (allocator_is_valid_block(blk)) {
       printf("Blok geçerli\n");
   } else {
       printf("Blok BOZUK!\n");
   }
   ```

---

## Optimizasyon Önerileri

1. **Cache Locality:**
   - Tahsisleri aynı bölgede yapın
   - Free list'in başında tutma tercih edilir

2. **Fragmentation Azaltma:**
   - Best-fit kullanın
   - Coalescing'in düzgün çalışıp çalışmadığını kontrol edin

3. **Bellek Verimliği:**
   - Gereksiz splitting'i önleyin (minimum size kontrolü)
   - Minimum blok boyutunu optimize edin

4. **Performans:**
   - First-fit çoğu durumda yeterince iyi
   - Best-fit sadece kritik durumlarda

