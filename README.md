# Custom Memory Allocator - Bellek Yönetimi

Sistem Programlama 2026 - Bellek Yönetimi - Dönem Sonu Projesi

## 📌 Hızlı Başlangıç
Linux ortamında:

```bash
sudo apt update
sudo apt install build-essential

# Projeyi temizle
make clean

# Projeyi derle
make

# Testleri arayüz açmadan çalıştır
make test

# Testlerden sonra terminal arayüzünü aç
make run
```

# İstatistikleri görmek için
allocator_print_stats();

```
> Not: `allocator_print_stats();` terminal komutu değildir. C kodu içinde çağrılan bir fonksiyondur. İstatistikleri görmek için `make test` veya `make run` kullanılabilir.

## 📚 Dokümantasyon

Detaylı dokümantasyon `docs/` klasöründe:

| Doküman | İçerik |
|---------|--------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Sistem mimarisi, bellek düzeni, veri yapıları |
| [API.md](docs/API.md) | Tüm fonksiyonların detaylı API referansı |
| [IMPLEMENTATION.md](docs/IMPLEMENTATION.md) | Person 2'nin implementasyon detayları |

## 🏗️ Proje Yapısı

```text
├── include/
│   ├── allocator.h                 # Ana header dosyası
│   ├── allocator_threadsafe.h      # Mutex ve thread-safe wrapper bildirimi
│   └── allocator_terminal_ui.h     # Terminal arayüz bildirimi
├── src/
│   ├── allocator_core.c            # Çekirdek altyapı (Person 1)
│   │   ├─ allocator_init()
│   │   ├─ Free list yönetimi
│   │   ├─ Block list yönetimi
│   │   └─ OS iletişimi (sbrk)
│   ├── allocator_ops.c             # Tahsis operasyonları (Person 2)
│   │   ├─ my_malloc() + splitting
│   │   ├─ my_free() + coalescing
│   │   ├─ my_calloc()
│   │   ├─ allocator_strategy_first_fit()
│   │   └─ allocator_strategy_best_fit()
│   ├── allocator_safety.c          # Hata kontrolü (Person 2)
│   │   ├─ Double-free tespiti
│   │   ├─ Invalid pointer tespiti
│   │   └─ allocator_report_memory_leaks()
│   ├── allocator_debug.c           # Fragmentation ve istatistik raporu (Person 3)
│   ├── allocator_threadsafe.c      # Thread-safe wrapper fonksiyonlar (Person 3)
│   └── allocator_terminal_ui.c     # ANSI terminal arayüzü ve canlı demo (Person 3)
├── tests/
│   └── test_allocator.c            # Test programı (Person 3)
├── docs/
│   ├── ARCHITECTURE.md
│   ├── API.md
│   └── IMPLEMENTATION.md
├── Makefile
└── README.md
``` 

## ✨ Temel Özellikler

### Tahsis Fonksiyonları
- ✅ `my_malloc()` - Bellek tahsisi (splitting ile)
- ✅ `my_free()` - Bellek serbest bırakma (coalescing ile)
- ✅ `my_calloc()` - Sıfırlanmış tahsis

### Optimizasyonlar
- ✅ **Block Splitting** - Internal fragmentation azaltma
- ✅ **Block Coalescing** - External fragmentation azaltma
- ✅ **Yerleştirme Stratejileri** - first-fit (hızlı) ve best-fit (verimli)

### Güvenlik
- ✅ **Double-free tespiti** - Aynı blok iki kez serbest bırakılmayı engeller
- ✅ **Invalid pointer tespiti** - Geçersiz pointerler tespit edilir
- ✅ **Magic numbers** - Bellek bozulması kontrolü
- ✅ **Boundary checks** - Taşma ve sınır kontrolleri

### İstatistikler & Raporlama
- ✅ **Bellek sızıntısı raporu** - Serbest bırakılmamış blokları listeler
- ✅ **Kapsamlı istatistikler** - Fragmentation, kullanım oranı, vb.
- ✅ En büyük serbest blok bilgisi
- ✅ Serbest/tahsisli blok sayıları
- ✅ Kullanım oranı

### Terminal Arayüzü
- ✅ Harici grafik kütüphanesi yoktur
- ✅ ANSI kaçış kodlarıyla sabit ekranlı TUI
- ✅ Renkli bellek haritası
- ✅ Canlı demo thread'i ile arka planda `my_malloc` / `my_free`

## 💻 Kullanım Örneği

```c
#include "allocator.h"
#include <stdio.h>

int main() {
    // Best-fit stratejisi kullan (daha verimli)
    allocator_set_strategy(allocator_strategy_best_fit);
    
    // Bellek tahsis et
    int *arr = (int *)my_malloc(10 * sizeof(int));
    if (arr == NULL) {
        fprintf(stderr, "Tahsis başarısız\n");
        return 1;
    }
    
    // Sıfırlanmış bellek tahsis et
    char *buffer = (char *)my_calloc(256, 1);
    
    // Belleği kullan
    arr[0] = 42;
    buffer[0] = 'A';
    
    // İstatistikleri yazdır
    allocator_print_stats();
    
    // Serbest bırak
    my_free(arr);
    my_free(buffer);
    
    // Sızıntı kontrol et
    allocator_report_memory_leaks();
    
    return 0;
}
```

## 📊 Görev Dağılımı

| Kişi | Sorumluluk | Durum |
|------|------------|-------|
| **Person 1** | Çekirdek allocator | ✅ Tamamlandı |
| **Person 2** | Tahsis stratejileri, free, calloc, hata kontrol | ✅ Tamamlandı |
| **Person 3** | Thread safety (mutex), test, Makefile, terminal arayüzü | ✅ Tamamlandı |

### Person 1 - Tamamlanan Görevler

✅ `block_header_t` metadata yapısının tasarlanması  
✅ `allocator.h` ortak header dosyasının hazırlanması  
✅ `sbrk` ile işletim sisteminden başlangıç heap havuzu alınması  
✅ `allocator_init()` ile allocator başlangıç durumunun kurulması  
✅ Serbest blok listesi veri yapısının kurulması  
✅ Free list'e blok ekleme ve listeden blok çıkarma fonksiyonları  
✅ Heap üzerindeki tüm blokları takip eden block list yapısı  
✅ Kullanıcı pointer'ı ile block header arasındaki pointer aritmetiği  
✅ `allocator_block_to_payload()` ve `allocator_payload_to_block()` yardımcı fonksiyonları  
✅ Varsayılan first-fit arama altyapısı (`allocator_default_find_free_block`)  
✅ İşletim sisteminden ek bellek isteme altyapısı (`allocator_request_from_os`)  

### Person 2 - Tamamlanan Görevler

✅ Yerleştirme stratejileri (first-fit, best-fit)  
✅ Block splitting (blok bölme)  
✅ Block coalescing (blok birleştirme)  
✅ `my_malloc()` implementasyonu (splitting ile)  
✅ `my_free()` implementasyonu (coalescing ile)  
✅ `my_calloc()` implementasyonu  
✅ Double-free tespiti  
✅ Invalid pointer tespiti  
✅ Bellek sızıntısı raporu (`allocator_report_memory_leaks()`)  
✅ İstatistikler (`allocator_print_stats()`)  

### Person 3 - Tamamlanan Görevler

✅ `pthread_mutex_t allocator_mutex` ile thread-safe wrapper katmanı  
✅ Fragmentation, en büyük serbest blok ve kullanım oranı raporu  
✅ `tests/test_allocator.c` test programı  
✅ Çok threadli allocation/free testi  
✅ Kısa performans ölçümü  
✅ Makefile  
✅ ANSI terminal arayüzü  
✅ Canlı demo thread'i 

## 🔧 Yerleştirme Stratejileri

### First-Fit (Varsayılan)
```c
allocator_set_strategy(allocator_strategy_first_fit);
```
- **Hız**: O(n) ama çoğu zaman erken çıkış
- **Avantaj**: Hızlı tahsis
- **Dezavantaj**: Fragmentation'a yatkın

### Best-Fit
```c
allocator_set_strategy(allocator_strategy_best_fit);
```
- **Hız**: O(n) tam tarama
- **Avantaj**: Internal fragmentation azalır
- **Dezavantaj**: First-fit'ten biraz yavaş

## 🎯 Önemli Özellikler

### Magic Numbers
```
0xC0FFEE01 - Tahsis edilmiş blok
0xC0FFEE02 - Serbest blok
```
Bellek bozulması tespit etmek için kullanılır.

### Alignment
- Tüm payload'lar 16-byte aligned
- Modern CPU cache verimli çalışması

### Fragmentation Kontrolü

**Splitting**: 
```
Tahsis öncesi:  [====== 512 byte ======]
Tahsis sonrası: [== 256 ===][== 256 serbest ==]
```

**Coalescing**:
```
Free öncesi:    [serbest][KULLANILAN][serbest]
Free sonrası:   [======= Birleştirilmiş =======]
```

## 🖥️ Terminal Arayüzü

Arayüzü açmak için:

```bash
make run
```

Komutlar:

```text
1  Gösterge paneli
2  Bellek haritası
3  Kayıtlar ve sızıntılar
a  my_malloc(128)
f  son arayüz tahsisini my_free ile bırak
d  canlı demo thread'ini başlat/durdur
q  çıkış
```

Canlı demo için önerilen akış:

```text
d  demoyu başlat
2  bellek haritasını izle
1  gösterge paneline dön
d  demoyu durdur
q  çık
```


## ⚠️ Bilinen Sınırlamalar

- **No Realloc** - `realloc()` uygulanmamıştır
- **Manual cleanup** - Garbage collection yoktur; bellek manuel olarak `my_free` ile bırakılır.
- **Allocator eğitim amaçlıdır** - Üretim ortamı için tasarlanmamıştır.

## 📈 Performans Değerlendirmesi

Test programı 8 thread oluşturur. Her thread 2000 kez allocation/free işlemi yapar.

Örnek çıktı:

```text
[PERF] 8 thread x 2000 iterasyon: ... us
```

Bu ölçüm, mutex korumalı allocator çağrılarının çok threadli kullanım altında çalıştığını gösterir.

## 📝 Test Senaryoları

`tests/test_allocator.c` içinde bulunan testler:

- Basic allocation/free
- `calloc` sıfırlama kontrolü
- 0 byte allocation
- Çok büyük allocation
- `NULL` free
- Double-free detection
- Memory leak detection
- Thread safety
- Fragmentation analysis
- Performance measurement

Double-free testinde hata mesajı görülmesi normaldir; bu mesaj kontrolün çalıştığını gösterir.

## Hata Yönetimi ve Loglama

- Hatalar `stderr` üzerinden yazdırılır.
- Gerekli yerlerde `perror` kullanılır.
- Test adımları `allocator_test.log` dosyasına yazılır.
- `allocator_test.log` çalışma çıktısıdır, GitHub'a yüklenmesi gerekmez.

## Karşılaşılan Problemler

Mevcut `.c` ve `.h` dosyalarına dokunulmaması gerektiği için mutex doğrudan `allocator_ops.c` içine eklenmedi. Bunun yerine Makefile ile raw sembol yaklaşımı kullanıldı. Public `my_malloc`, `my_free`, `my_calloc` fonksiyonları `allocator_threadsafe.c` içinde mutex ile sarıldı.

Terminal arayüzünde ANSI renk kodları sağ çerçeveyi bozabiliyordu. Bu sorun görünür UTF-8 karakter genişliği hesaplanarak çözüldü.

---

## 📖 Kaynak Kodları

- `include/allocator.h` - Ortak veri yapıları, sabitler ve fonksiyon prototipleri
- `include/allocator_threadsafe.h` - Global mutex ve thread-safe katman bildirimi
- `include/allocator_terminal_ui.h` - Terminal arayüz fonksiyon bildirimi
- `src/allocator_core.c` - Heap başlangıcı, block list, free list ve pointer dönüşümleri
- `src/allocator_ops.c` - `my_malloc`, `my_free`, `my_calloc`, placement strategy, splitting, coalescing
- `src/allocator_safety.c` - Invalid free, double-free ve bellek sızıntısı kontrolleri
- `src/allocator_debug.c` - Fragmentation ve bellek istatistikleri
- `src/allocator_threadsafe.c` - Mutex ile korunan public allocator API wrapper'ları
- `src/allocator_terminal_ui.c` - ANSI terminal arayüzü ve canlı demo thread'i
- `tests/test_allocator.c` - Otomatik testler, thread testi ve performans ölçümü
- `Makefile` - Derleme, test, çalıştırma ve temizleme komutları
