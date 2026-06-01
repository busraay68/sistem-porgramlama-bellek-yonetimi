# API Referans Dokümanı

## Temel Tahsis Fonksiyonları

### `void *my_malloc(size_t size)`

Bellek tahsis eder.

**Parametreler:**
- `size`: Tahsis edilecek bellek boyutu (byte cinsinden)

**Dönen Değer:**
- Başarı: Tahsis edilen belleğin başlangıç adresini döndürür
- Başarısız: `NULL`

**Hata Durumları:**
- `size == 0`: `NULL` döndürür
- `aligned_size == 0`: Boyut taşması, `NULL` döndürür
- OS tahsisi başarısız: `NULL` döndürür

**Örnek:**
```c
int *arr = (int *)my_malloc(10 * sizeof(int));
if (arr == NULL) {
    fprintf(stderr, "Tahsis başarısız\n");
    return -1;
}
```

**Notlar:**
- Dönen adres **16-byte aligned** dir
- Blok otomatik olarak split edilir (eğer gerekirse)
- Tahsis edilen bellek önceden sıfırlanmamış olabilir

---

### `void my_free(void *ptr)`

Tahsis edilen belleği serbest bırakır.

**Parametreler:**
- `ptr`: `my_malloc()` tarafından döndürülen adres

**Dönen Değer:**
- Yok (void)

**Hata Durumları:**
- `ptr == NULL`: Yoksayılır (standart C davranışı)
- Geçersiz magic: Hata mesajı yazdırılır, dönülür
- Double-free: Hata mesajı yazdırılır, dönülür

**Örnek:**
```c
int *arr = (int *)my_malloc(100);
// ... kullan ...
my_free(arr);
```

**Notlar:**
- Serbest bırakılan bellek otomatik olarak komşu bloklar ile birleştirilir
- İki kez `my_free` çağrısı hataya sebep olur
- Stack adreslerini geçirmek hataya sebep olur

---

### `void *my_calloc(size_t nmemb, size_t size)`

Tahsis edilen belleği sıfırlı olarak ayırdır. (calloc = clear + allocate)

**Parametreler:**
- `nmemb`: Elemanın sayısı
- `size`: Her bir elemanın boyutu (byte cinsinden)

**Dönen Değer:**
- Başarı: Tahsis edilen belleğin başlangıç adresini döndürür (sıfırlandı)
- Başarısız: `NULL`

**Hata Durumları:**
- `nmemb == 0` veya `size == 0`: `NULL` döndürür
- Taşma kontrolü: `nmemb * size > SIZE_MAX` ise başarısız
- `my_malloc` başarısız: `NULL` döndürür

**Örnek:**
```c
// 100 int'lik dizi, hepsi sıfırlandı
int *arr = (int *)my_calloc(100, sizeof(int));

// Her elemanın değeri 0'dır
assert(arr[0] == 0);
assert(arr[99] == 0);
```

**Notlar:**
- `my_malloc()` + `memset()` işlemi kombinasyonudur
- Tüm baytlar `0x00` olarak başlatılır
- Standart C `calloc()` gibi davranır

---

## Yerleştirme Stratejileri

### `block_header_t *allocator_strategy_first_fit(size_t size)`

Free list'te ilk uygun bloğu bulur.

**Parametreler:**
- `size`: Aranan minimum blok boyutu

**Dönen Değer:**
- Bulundu: Uygun blok header'ı
- Bulunamadı: `NULL`

**Karmaşıklık:**
- Zaman: O(n), n = serbest blok sayısı
- Durum: Erken çıkış (ilk uygun bulunca)

**Örnek:**
```c
allocator_set_strategy(allocator_strategy_first_fit);
// my_malloc şimdi first-fit kullanır
```

**Notlar:**
- Varsayılan stratejidir
- Hızlı ama fragmentation'a yatkındır

---

### `block_header_t *allocator_strategy_best_fit(size_t size)`

Free list'te boyuta en yakın (en küçük yeterli) bloğu bulur.

**Parametreler:**
- `size`: Aranan minimum blok boyutu

**Dönen Değer:**
- Bulundu: Uygun blok header'ı
- Bulunamadı: `NULL`

**Karmaşıklık:**
- Zaman: O(n), n = serbest blok sayısı
- Durum: Tüm list gezilmesi gerekir

**Örnek:**
```c
allocator_set_strategy(allocator_strategy_best_fit);
// my_malloc şimdi best-fit kullanır
```

**Notlar:**
- Internal fragmentation'ı azaltır
- First-fit'ten biraz daha yavaştır

---

## İç Operasyonlar

### `void allocator_split_block(block_header_t *block, size_t required_size)`

Seçilen bloğu gerekli kısım ve serbest kısım olarak böler.

**Parametreler:**
- `block`: Bölünecek blok
- `required_size`: Gerekli payload boyutu

**Notlar:**
- Otomatik olarak `my_malloc` içinde çağrılır
- Eğer kalan alan çok küçükse splitting yapılmaz
- Yeni blok free list'e eklenir

**Diagram:**
```
Splitting öncesi:
[Header][============ 512 byte ============]

Splitting sonrası:
[Header₁][=== 256 byte ===][Header₂][=== 256 serbest ===]
                            ↓ free list'e eklenir
```

---

### `void allocator_coalesce_blocks(block_header_t *block)`

Serbest bırakılan bloğun etrafındaki serbest blokları birleştirir.

**Parametreler:**
- `block`: Birleştirilecek merkez blok

**Notlar:**
- Otomatik olarak `my_free` içinde çağrılır
- Önceki ve sonraki blokları kontrol eder
- Çift bağlı all-blocks listesini kullanır

**Diagram:**
```
Coalesce öncesi:
[free₁] [USED] [free₂] [USED] [free₃]
         ↓my_free
Coalesce sonrası:
[================== birleştirilmiş ==================] [USED] [free₃]
```

---

## Hata Kontrol ve Doğrulama

### `int allocator_is_valid_block(block_header_t *block)`

Bir bloğun geçerli olup olmadığını kontrol eder.

**Parametreler:**
- `block`: Kontrol edilecek blok

**Dönen Değer:**
- Geçerli: 1 (true)
- Geçersiz: 0 (false)

**Kontroller:**
- `block != NULL`
- `magic == ALLOCATOR_MAGIC_ALLOC` veya `ALLOCATOR_MAGIC_FREE`
- `size > 0`

**Örnek:**
```c
block_header_t *blk = allocator_payload_to_block(ptr);
if (!allocator_is_valid_block(blk)) {
    fprintf(stderr, "Blok bozulmuş\n");
}
```

---

### `int allocator_is_block_allocated(block_header_t *block)`

Bir bloğun tahsis edilmiş (allocated) olup olmadığını kontrol eder.

**Parametreler:**
- `block`: Kontrol edilecek blok

**Dönen Değer:**
- Tahsis edilmiş: 1 (true)
- Serbest: 0 (false)

**Kontroller:**
- `block != NULL`
- `is_free == 0` (kullanımda)
- `magic == ALLOCATOR_MAGIC_ALLOC`

---

## İstatistik ve Raporlama

### `void allocator_report_memory_leaks(void)`

Tahsis edilen ama serbest bırakılmamış tüm blokları listeler.

**Parametreler:**
- Yok

**Dönen Değer:**
- Yok (stdout'a yazdırır)

**Çıktı Örneği:**
```
========== Bellek Sızıntısı Raporu ==========
  SIZZINTI: Adres=0x...1000, Boyut=1024 byte
  SIZZINTI: Adres=0x...2000, Boyut=512 byte
  TOPLAM SIZZINTI: 2 blok, 1536 byte
==========================================
```

**Kullanım:**
```c
// Program sonunda
allocator_report_memory_leaks();

// Veya test sırasında
my_malloc(100);
allocator_report_memory_leaks();  // 1 sızıntı gösterir
```

---

### `void allocator_print_stats(void)`

Allocator'ın kapsamlı istatistiklerini yazdırır.

**Parametreler:**
- Yok

**Dönen Değer:**
- Yok (stdout'a yazdırır)

**Çıktı Örneği:**
```
========== Allocator İstatistikleri ==========
Toplam Ayrılan Bellek:     65536 byte (64.00 KB)
Tahsis Edilen Bellek:      1024 byte (1.00 KB)
Serbest Bellek:            64512 byte (63.00 KB)

Blok Sayıları:
  Tahsis Edilmiş Blok:     3
  Serbest Blok:            2
  Aktif Blok:              3

Fragmentation Analizi:
  En Büyük Serbest Blok:   64000 byte (62.50 KB)
  External Fragmentation:  0%

Kullanım Oranı:            1.56%
===============================================
```

---

## Başlangıç ve Konfigürasyon

### `int allocator_init(size_t initial_pool_size)`

Allocator'ı başlatır ve ilk heap havuzunu ayırır.

**Parametreler:**
- `initial_pool_size`: İlk tahsis edilecek bellek (byte)
  - `0`: Varsayılan (64 KB)

**Dönen Değer:**
- Başarı: 0
- Başarısız: -1

**Örnek:**
```c
// Varsayılan 64 KB ile başlat
allocator_init(0);

// Veya özel boyut
allocator_init(1024 * 1024);  // 1 MB
```

**Notlar:**
- Otomatik olarak ilk `my_malloc()` çağrısında çalışır
- Yalnızca bir kez başlatılabilir
- OS'ten sbrk kullanarak bellek ister

---

### `void allocator_set_strategy(allocator_strategy_fn strategy_fn)`

Yerleştirme stratejisini ayarlar.

**Parametreler:**
- `strategy_fn`: Strateji fonksiyonu işaretçisi
  - `allocator_strategy_first_fit`
  - `allocator_strategy_best_fit`
  - Özel strateji fonksiyonu

**Örnek:**
```c
allocator_set_strategy(allocator_strategy_best_fit);

// Veya özel strateji
block_header_t *my_strategy(size_t size) {
    // Kendi mantığınız
}
allocator_set_strategy(my_strategy);
```

---

## Küresel İstatistik Değişkenleri

### `extern size_t total_reserved_memory`

OS'ten alınan toplam bellek miktarı (byte cinsinden).

**Başlangıç:**
- 0 (başlangıç havuzundan sonra artır)

**Güncelleme:**
- `allocator_init()` sırasında
- `allocator_request_from_os()` sırasında

---

### `extern size_t total_allocated_memory`

Şu anda tahsis edilen toplam bellek miktarı.

**Başlangıç:**
- 0

**Güncelleme:**
- `my_malloc()`: Artar
- `my_free()`: Azalır

**Formula:**
```
Serbest Bellek = total_reserved_memory - total_allocated_memory
```

---

### `extern size_t active_block_count`

Şu anda tahsis edilen blok sayısı.

**Başlangıç:**
- 0

**Güncelleme:**
- `my_malloc()`: Artar
- `my_free()`: Azalır

---

## Pointer Aritmetiği Yardımcıları

### `void *allocator_block_to_payload(block_header_t *block)`

Blok header'ından payload adresine dönüşüm.

```c
block_header_t *block = get_some_block();
void *payload = allocator_block_to_payload(block);
// Kullanıcıya döndürülecek adres
```

---

### `block_header_t *allocator_payload_to_block(void *ptr)`

Payload adresinden blok header'ına dönüşüm.

```c
void *user_ptr = my_malloc(100);
block_header_t *block = allocator_payload_to_block(user_ptr);
// Magic kontrol otomatik
```

---

## Hata Mesajları

| Mesaj | Sebep | Çözüm |
|-------|-------|-------|
| "geçersiz Header imzası algılandı" | Bellek bozulması veya yanlış pointer | `my_free` ile doğru bir blok serbest bırakın |
| "geçersiz pointer serbest bırakılmaya çalışıldı" | `my_free(NULL)` dışında geçersiz adres | Doğru adres döndürüldüğünü kontrol edin |
| "zaten serbest olan blok tekrar serbest" | Double-free | `my_free` iki kez çağrılıyor |
| "başlangıç heap genişletilemedi" | OS bellek isterken başarısız | Sistem belleği tamam mı kontrol edin |
| "my_calloc boyut taşması" | `nmemb * size > SIZE_MAX` | Daha küçük değerler kullanın |

---

## Tasarım İlkeleri

1. **Thread-safe olmayan**: Mutex eklenene kadar çok-thread ortamı uygun değil
2. **Sadece tahsis**: Realloc uygulanmamıştır (isteğe bağlı gelecek)
3. **Dinamik büyüme**: Heap otomatik olarak büyütülür gerektiğinde
4. **Tanılama**: Hata tespiti magic numbers kullanır
5. **Modüler**: Her sorumluluğu farklı dosyada

---

## Performans İpuçları

| Durum | Öneride | Neden |
|-------|---------|-------|
| Çok sayıda küçük tahsis | Best-fit + Coalescing | Fragmentation azalır |
| Belirli boyutlarda tahsis | First-fit | Daha hızlı |
| Yoğun free/malloc | Mutex geçidi | CPU'da kilitlenme olmaz |
| Çok büyük tahsis | Öncesinde serbest bırakın | Fragmentasyon |

