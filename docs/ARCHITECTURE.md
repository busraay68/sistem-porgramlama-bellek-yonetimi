# Allocator Mimarisi (Architecture)

## Genel Yapı

Allocator, modüler bir tasarımla 3 ana bileşenden oluşmaktadır:

```
┌─────────────────────────────────────────────────────────┐
│  Kullanıcı Uygulaması                                   │
│  (my_malloc, my_free, my_calloc)                        │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│  allocator_ops.c (Tahsis Operasyonları - Person 2)     │
│  - Yerleştirme stratejileri                            │
│  - Block splitting                                      │
│  - Block coalescing                                     │
│  - my_malloc, my_free, my_calloc                       │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│  allocator_safety.c (Hata Kontrolleri - Person 2)      │
│  - Double-free tespiti                                 │
│  - Invalid pointer tespiti                             │
│  - Memory leak raporu                                  │
│  - İstatistikler                                       │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│  allocator_core.c (Çekirdek Altyapı - Person 1)        │
│  - allocator_init                                      │
│  - Free list yönetimi                                  │
│  - Block list yönetimi                                 │
│  - Pointer arithmetic                                  │
│  - OS ile iletişim (sbrk)                             │
└────────────┬────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────────┐
│  İşletim Sistemi                                        │
│  (sbrk - Program Break Genişletme)                      │
└─────────────────────────────────────────────────────────┘
```

---

## Bellek Düzeni (Memory Layout)

### Heap Yapısı

```
┌─────────────────────────────────────────────────────────┐
│  OS Tarafından Ayrılan Heap                             │
└─────────────────────────────────────────────────────────┘
   ▲
   │
   ├─ [Header₁][Payload₁]  ← Tahsis edilmiş blok
   │
   ├─ [Header₂][Payload₂]  ← Serbest blok
   │
   ├─ [Header₃][Payload₃]  ← Tahsis edilmiş blok
   │
   └─ [Header₄][Payload₄]  ← Serbest blok
```

### Block Header Yapısı

```c
struct block_header {
    size_t size;             // Payload boyutu (byte cinsinden)
    int is_free;             // 0=tahsis edilmiş, 1=serbest
    uint32_t magic;          // 0xC0FFEE01 veya 0xC0FFEE02
    block_header_t *next_free;   // Free list'te sonraki blok
    block_header_t *prev_free;   // Free list'te önceki blok
    block_header_t *next_all;    // All-blocks zincirinde sonraki
    block_header_t *prev_all;    // All-blocks zincirinde önceki
    uint8_t padding[16];     // Alignment
};
// Toplam: 64 byte (16-byte aligned)
```

### Payload ve Header İlişkisi

```
Bellek görünümü:
┌──────────────────┬──────────────────────────┐
│  Block Header    │     Payload (Kullanıcı)  │
│  (64 byte)       │     (İstenen boyut)      │
└──────────────────┴──────────────────────────┘
▲                  ▲
│                  │
header ptr         payload ptr
                   (allocator_block_to_payload)
(allocator_payload_to_block)
```

---

## İki Temel List Yapısı

### 1. Free List (Serbest Blok Listesi)

- **Amaç**: Tahsis için kullanılabilir bloklar
- **Sırasız**: Bloklar eklenip çıkarıldığı sırada
- **Operasyon**: O(n) arama
- **Optimizasyon**: Best-fit ile fragmentation azaltabilir

```
Free List Head
    │
    ▼
[Block₂:64KB]  ◄─►  [Block₄:128KB]  ◄─►  [Block₆:32KB]  ◄─►  NULL
```

### 2. All Blocks List (Tüm Bloklar Listesi)

- **Amaç**: Tüm blokları (tahsis+serbest) takip etmek
- **Sıralı**: Bellek adresine göre sıra
- **Operasyon**: Coalescing ve istatistikler
- **Önem**: Fragmentation, leak detection

```
Block List Head
    │
    ▼
[Block₁:used]  ◄─►  [Block₂:free]  ◄─►  [Block₃:used]  ◄─►  [Block₄:free]
```

---

## Tahsis Akışı (Allocation Flow)

### Adım 1: Boyut Doğrulama ve Alignment

```
my_malloc(size)
    │
    ├─ size == 0? ► NULL döndür
    │
    └─ allocator_align_size(size)
       ├─ Alignment sınırına yuvarla (16-byte)
       ├─ Minimum blok boyutuna kontrol et
       └─ aligned_size döndür
```

**Örnek**:
- İstek: 25 byte
- Alignment sonrası: 32 byte (2 x 16)
- Min kontrol: 32 >= 16 ✓
- Sonuç: 32 byte

### Adım 2: Free Block Bulma

```
Free List'te uygun blok ara
    │
    ├─ Strateji = best-fit?
    │   └─ Tüm free list gezilir, en küçük uygun seçilir
    │
    └─ Strateji = first-fit (varsayılan)?
        └─ İlk uygun blok seçilir
```

### Adım 3: Block Splitting (Bölme)

```
Bulundu: 512 byte blok, İstek: 256 byte

Splitting öncesi:
[Header₁][================ 512 byte payload ================]

Splitting sonrası:
[Header₁][======= 256 byte payload ======][Header₂][== 256 serbest ==]
```

**Koşullar**:
- `remaining_size ≥ (Header_size + MIN_BLOCK_SIZE)`
- Aksi takdirde splitting yapılmaz (maliyetli ve yararsız)

### Adım 4: Blok Tahsis Markaması

```
selected_block->is_free = 0;
selected_block->magic = ALLOCATOR_MAGIC_ALLOC;
İstatistikler güncellenir:
  - total_allocated_memory += size
  - active_block_count++
```

### Adım 5: Payload Döndürme

```
return allocator_block_to_payload(block);
// Blok headerının hemen ardından gelen adresi döndür
```

---

## Serbest Bırakma Akışı (Free Flow)

### Adım 1: Pointer Doğrulama

```
my_free(ptr)
    │
    ├─ ptr == NULL? ► Yoksay
    │
    └─ allocator_payload_to_block(ptr)
       ├─ Magic number kontrol et
       ├─ Bozulmuş mu? ► Hata mesajı, dön
       └─ Blok header döndür
```

### Adım 2: Double-Free Tespiti

```
block->is_free == 1?
    │
    └─ Evet ► Hata: "double-free detected!"
             İşlem sonlandırılır
```

### Adım 3: Blok Serbest Bırakma

```
allocator_add_to_free_list(block)
    │
    ├─ block->is_free = 1
    ├─ block->magic = ALLOCATOR_MAGIC_FREE
    └─ Free list başına ekle
```

### Adım 4: Coalescing (Blok Birleştirme)

```
allocator_coalesce_blocks(block)
    │
    ├─ Önceki blok serbest mi?
    │   └─ Evet ► Birleştir (prev_block büyütül)
    │
    └─ Sonraki blok serbest mi?
        └─ Evet ► Birleştir (block büyütül)
```

**Coalescing Sonrası**:
```
Coalesce öncesi: [serbest₁][KULLANIMDA][serbest₂]
Coalesce sonrası: [====== Birleştirilmiş serbest blok ======]
```

---

## Yerleştirme Stratejileri Karşılaştırması

| Kriter | First-Fit | Best-Fit |
|--------|-----------|----------|
| **Arama Süresi** | O(n), erken sonlanma | O(n), tüm list |
| **Seçilen Blok** | İlk uygun | En küçük uygun |
| **Internal Frag.** | Yüksek (kalan kısmı sık boşta) | Düşük |
| **External Frag.** | Yüksek | Düşük |
| **Pratik** | Hızlı, basit | Veri tabanı, long-running |

---

## Thread Safety

Çok-threadli ortamda emniyeti sağlamak için mutex kullanılır:

```c
// Tüm kritik bölümlerde:
pthread_mutex_lock(&allocator_mutex);
  // Tahsis/serbest bırakma işlemi
pthread_mutex_unlock(&allocator_mutex);
```

---

## Fragmentation Hesaplama

### External Fragmentation

```
External Frag = (Toplam Serbest - En Büyük Serbest) / Toplam
              = (64KB - 62KB) / 64KB
              = 3.1%
```

**Anlamı**: Serbest alan parçalara bölünmüştür, büyük tahsisler başarısız olabilir.

### Internal Fragmentation

```
Internal Frag = (Tahsis Edilen - İstenen) / Tahsis Edilen
             = (256B - 200B) / 256B
             = 21.8%
```

**Anlamı**: Tahsis edilen blokta boş kalan alan.

---

## Hata Kontrol Mekanizmaları

### Magic Numbers

```c
#define ALLOCATOR_MAGIC_ALLOC 0xC0FFEE01  // Tahsis edilmiş
#define ALLOCATOR_MAGIC_FREE  0xC0FFEE02  // Serbest
```

**Kullanım**: Her free/invalid işlemde magic kontrol edilir.

### Olası Hatalar

1. **Bellek Bozulması**: magic != ALLOC ve magic != FREE
2. **Double-Free**: block->is_free == 1 ve my_free çağrılması
3. **Geçersiz Pointer**: Stack pointerini free'ye geçmek
4. **Buffer Overflow**: Header bozulup magic değiştirilmesi

---

## İstatistik Takibi

Küresel sayaçlar:
- `total_reserved_memory`: OS'ten alınan toplam bellek
- `total_allocated_memory`: Şu an tahsis edilen toplam
- `active_block_count`: Şu an tahsis edilmiş blok sayısı

Bu sayaçlar her `my_malloc()` ve `my_free()` çağrısında güncellenir.
