# Custom Memory Allocator

C ve POSIX/Linux API ile geliştirilmiş, kullanıcı alanında çalışan basit bir bellek yöneticisi.

## Amaç

Standart `malloc`, `free` ve `calloc` mantığını taklit eden özel bir allocator geliştirmek. Proje; serbest blok listesi, placement strategy, fragmentation raporu, hata kontrolü, thread safety ve test programı içerir.

## Tasarım

Bellek blokları şu düzende tutulur:

```text
[ block_header_t ][ kullanıcı payload alanı ]
```

Her blok header'ında blok boyutu, boş/dolu durumu, magic number ve bağlı liste işaretçileri bulunur. Allocator iki liste kullanır:

- Tüm blok listesi: heap üzerindeki bütün blokları gezmek için.
- Serbest blok listesi: boş blokları takip edip allocation sırasında uygun blok bulmak için.

`my_malloc`, first-fit/best-fit stratejisiyle uygun bloğu bulur ve gerekirse splitting yapar. `my_free`, double-free ve geçersiz pointer kontrollerinden sonra bloğu serbest listeye ekler ve komşu boş blokları coalescing ile birleştirir. `my_calloc`, tahsis edilen belleği sıfırlar.

Thread safety, mevcut çekirdek dosyalara dokunmadan `allocator_threadsafe.c` içinde wrapper fonksiyonlarla sağlanır. Makefile, `allocator_ops.c` içindeki ham fonksiyonları `allocator_raw_*` sembolleriyle derler; public `my_malloc`, `my_free`, `my_calloc` çağrıları mutex ile korunur.

## Kullanılan Sistem Programlama Kavramları

- C dili ve POSIX/Linux API
- `sbrk` ile heap alanı genişletme
- pointer aritmetiği
- dinamik bellek yönetimi
- bağlı liste veri yapıları
- first-fit ve best-fit stratejileri
- block splitting ve coalescing
- `pthread_create`, `pthread_join`
- `pthread_mutex_t` ile senkronizasyon
- hata mesajları, `perror`, log dosyası
- ANSI kaçış kodlarıyla terminal arayüzü

## Proje Yapısı

```text
include/
  allocator.h
  allocator_threadsafe.h
  allocator_terminal_ui.h

src/
  allocator_core.c
  allocator_ops.c
  allocator_safety.c
  allocator_debug.c
  allocator_threadsafe.c
  allocator_terminal_ui.c

tests/
  test_allocator.c

Makefile
README.md
```

## Çalıştırma Adımları

Linux ortamında gerekli paket:

```bash
sudo apt update
sudo apt install build-essential
```

Derleme ve çalıştırma:

```bash
make clean
make
make test
make run
```

Makefile komutları:

```bash
make        # derler
make test   # testleri arayüz açmadan çalıştırır
make run    # testlerden sonra terminal arayüzünü açar
make clean  # derleme çıktılarını temizler
make help   # komutları listeler
```

## Testler

Test programı:

```text
tests/test_allocator.c
```

Test edilenler:

- `my_malloc`, `my_free`, `my_calloc`
- `my_calloc` sonrası belleğin sıfırlanması
- 0 byte allocation
- çok büyük allocation
- `NULL` free
- double-free tespiti
- fragmentation raporu
- çok threadli allocation/free testi
- performans ölçümü
- bellek sızıntısı raporu

Örnek:

```bash
make test
```

Önemli çıktılar:

```text
[TEST] Thread safety ve performans
[PERF] 8 thread x 2000 iterasyon: ... us
Sızıntı tespit edilmedi
```

Double-free testinde hata mesajı görülmesi beklenir; bu kontrolün çalıştığını gösterir.

## Terminal Arayüzü

Harici grafik kütüphanesi kullanılmaz. Arayüz, standart terminal çıktısı ve ANSI kaçış kodlarıyla çizilir.

Başlatmak için:

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

Canlı demo modunda arka planda bir thread sürekli `my_malloc` ve `my_free` çağırır. Böylece gösterge panelinde ve bellek haritasında allocator durumunun canlı değişimi izlenebilir.

## Performans Değerlendirmesi

`make test` sırasında 8 thread oluşturulur. Her thread 2000 kez allocation/free işlemi yapar ve toplam süre mikrosaniye cinsinden yazdırılır.

Örnek:

```text
[PERF] 8 thread x 2000 iterasyon: 2558 us
```

Bu ölçüm, allocator'ın çok threadli kullanım altında çalıştığını ve mutex ile korunduğunu gösterir.

## Hata Yönetimi ve Loglama

Projede hata yönetimi için `perror`, `fprintf(stderr, ...)` ve özel allocator hata mesajları kullanılır. Test çıktıları ayrıca `allocator_test.log` dosyasına yazılır.

`allocator_test.log` çalışma çıktısıdır; GitHub'a yüklenmemelidir.

## Karşılaşılan Problemler

Mevcut çekirdek `.c` ve `.h` dosyalarına dokunulmaması gerektiği için mutex doğrudan `allocator_ops.c` içine eklenmedi. Bunun yerine Makefile ile raw sembol yaklaşımı kullanıldı ve thread-safe public API `allocator_threadsafe.c` içinde yazıldı.

Terminal arayüzünde ANSI renk kodları sağ çerçeveyi bozabiliyordu. Bu nedenle görünür UTF-8 karakter genişliği hesaplanarak çizim yapıldı; ANSI kaçış kodları genişlik hesabına dahil edilmedi.

Canlı demo sırasında arka plandaki thread allocation/free yaparken arayüz allocator durumunu okuyabilir. Bu nedenle arayüz blok listesini okurken `allocator_mutex` kullanır.
