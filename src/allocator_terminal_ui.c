#include "allocator_terminal_ui.h"

#include "allocator.h"
#include "allocator_threadsafe.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define UI_BODY_WIDTH 76
#define PROGRESS_WIDTH 10
#define MEMORY_BAR_WIDTH 30
#define MAX_TERMINAL_ALLOCS 64
#define MAX_DEMO_ALLOCS 32
#define UI_REFRESH_MS 200

#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_DIM "\033[2m"
#define ANSI_BLACK_BG "\033[40m"
#define ANSI_PANEL "\033[48;5;236m"
#define ANSI_CYAN "\033[1;36m"
#define ANSI_BLUE "\033[1;34m"
#define ANSI_MAGENTA "\033[1;35m"
#define ANSI_YELLOW "\033[1;33m"
#define ANSI_GREEN "\033[1;32m"
#define ANSI_RED "\033[1;31m"
#define ANSI_WHITE "\033[1;37m"
#define ANSI_GRAY "\033[90m"

/*
 * Modern ANSI TUI.
 *
 * Çerçevenin sağ kenarının kaymaması için ANSI kaçış kodları genişlik hesabına
 * katılmaz. Satırlar parça parça çizilir; her renkli metin için sadece ekranda
 * görünen UTF-8 karakter sayısı izlenir. Böylece Türkçe karakterler ve renk
 * kodları çerçeveyi bozmaz.
 */
typedef enum TerminalTab {
    TERM_TAB_DASHBOARD = 0,
    TERM_TAB_MEMORY_MAP,
    TERM_TAB_LOGS
} TerminalTab;

typedef struct UiStats {
    size_t total_blocks;
    size_t free_blocks;
    size_t used_blocks;
    size_t total_free;
    size_t largest_free;
    size_t reserved;
    size_t allocated;
    size_t active_blocks;
    double usage_percent;
    double fragmentation_percent;
} UiStats;

static void *terminal_allocations[MAX_TERMINAL_ALLOCS];
static size_t terminal_allocation_count = 0;
static char terminal_status[192] = "Terminal arayüz hazır";
static pthread_mutex_t terminal_status_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_t demo_thread;
static pthread_mutex_t demo_mutex = PTHREAD_MUTEX_INITIALIZER;
static int demo_running = 0;
static int demo_thread_started = 0;
static void *demo_allocations[MAX_DEMO_ALLOCS];
static size_t demo_allocation_count = 0;
static size_t demo_step = 0;

static void ui_set_status(const char *format, ...)
{
    va_list args;

    pthread_mutex_lock(&terminal_status_mutex);
    va_start(args, format);
    vsnprintf(terminal_status, sizeof(terminal_status), format, args);
    va_end(args);
    pthread_mutex_unlock(&terminal_status_mutex);
}

static void ui_get_status(char *buffer, size_t buffer_size)
{
    pthread_mutex_lock(&terminal_status_mutex);
    snprintf(buffer, buffer_size, "%s", terminal_status);
    pthread_mutex_unlock(&terminal_status_mutex);
}

static int utf8_width(const char *text)
{
    int width = 0;

    while (*text != '\0') {
        unsigned char ch = (unsigned char)*text;

        if ((ch & 0xC0U) != 0x80U) {
            width++;
        }
        text++;
    }

    return width;
}

static void ui_clear_screen(void)
{
    printf("\033[?25l");
    printf("\033[H\033[J");
}

static void ui_flush_line(void)
{
    int ch;

    while ((ch = getchar()) != '\n' && ch != EOF) {
    }
}

static void ui_sleep_ms(long milliseconds)
{
    struct timespec delay;

    delay.tv_sec = milliseconds / 1000L;
    delay.tv_nsec = (milliseconds % 1000L) * 1000000L;
    nanosleep(&delay, NULL);
}

static int ui_read_command_with_timeout(int timeout_ms)
{
    fd_set read_set;
    struct timeval timeout;
    int ready;
    int command;

    FD_ZERO(&read_set);
    FD_SET(STDIN_FILENO, &read_set);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    ready = select(STDIN_FILENO + 1, &read_set, NULL, NULL, &timeout);
    if (ready <= 0) {
        return 0;
    }

    command = getchar();
    if (command == EOF) {
        return EOF;
    }
    if (command != '\n') {
        ui_flush_line();
    }

    return command;
}

static void ui_repeat(const char *text, int count)
{
    for (int i = 0; i < count; ++i) {
        printf("%s", text);
    }
}

static void ui_border_top(void)
{
    printf(ANSI_CYAN "╔");
    ui_repeat("═", UI_BODY_WIDTH + 2);
    printf("╗" ANSI_RESET "\n");
}

static void ui_border_mid(void)
{
    printf(ANSI_CYAN "╠");
    ui_repeat("═", UI_BODY_WIDTH + 2);
    printf("╣" ANSI_RESET "\n");
}

static void ui_border_bottom(void)
{
    printf(ANSI_CYAN "╚");
    ui_repeat("═", UI_BODY_WIDTH + 2);
    printf("╝" ANSI_RESET "\n");
}

static void ui_line_begin(void)
{
    printf(ANSI_CYAN "║" ANSI_RESET " ");
}

static void ui_line_end(int visible_width)
{
    int padding = UI_BODY_WIDTH - visible_width;

    if (padding < 0) {
        padding = 0;
    }

    ui_repeat(" ", padding);
    printf(" " ANSI_CYAN "║" ANSI_RESET "\n");
}

static void ui_put(int *visible_width, const char *color, const char *text)
{
    if (color != NULL) {
        printf("%s", color);
    }
    printf("%s", text);
    if (color != NULL) {
        printf(ANSI_RESET);
    }
    *visible_width += utf8_width(text);
}

static void ui_line_plain(const char *text)
{
    int width = 0;

    ui_line_begin();
    ui_put(&width, NULL, text);
    ui_line_end(width);
}

static void ui_empty_line(void)
{
    ui_line_plain("");
}

static void ui_line_title(const char *icon, const char *title, const char *hint)
{
    int width = 0;

    ui_line_begin();
    ui_put(&width, ANSI_MAGENTA, icon);
    ui_put(&width, NULL, " ");
    ui_put(&width, ANSI_WHITE ANSI_BOLD, title);
    ui_put(&width, ANSI_GRAY, "  •  ");
    ui_put(&width, ANSI_DIM, hint);
    ui_line_end(width);
}

static void ui_tab_line(TerminalTab active_tab)
{
    int width = 0;

    ui_line_begin();
    ui_put(&width, ANSI_YELLOW ANSI_BOLD, "ALLOCATOR ARENA");
    ui_put(&width, ANSI_GRAY, "  •  ");
    ui_put(&width, ANSI_WHITE, "Özel Bellek Yöneticisi");
    ui_line_end(width);

    width = 0;
    ui_line_begin();
    ui_put(&width, active_tab == TERM_TAB_DASHBOARD ? ANSI_GREEN ANSI_BOLD : ANSI_WHITE,
           "[1] Gösterge");
    ui_put(&width, ANSI_GRAY, "   ");
    ui_put(&width, active_tab == TERM_TAB_MEMORY_MAP ? ANSI_GREEN ANSI_BOLD : ANSI_WHITE,
           "[2] Harita");
    ui_put(&width, ANSI_GRAY, "   ");
    ui_put(&width, active_tab == TERM_TAB_LOGS ? ANSI_GREEN ANSI_BOLD : ANSI_WHITE,
           "[3] Kayıt/Sızıntı");
    ui_put(&width, ANSI_GRAY, "   ");
    ui_put(&width, ANSI_DIM, "Sabit ekranlı ANSI TUI");
    ui_line_end(width);
}

static void ui_subtitle_line(void)
{
    int width = 0;

    ui_line_begin();
    ui_put(&width, ANSI_DIM, "Thread-safe malloc/free/calloc • first-fit • mutex korumalı gözlem");
    ui_line_end(width);
}

static void ui_progress_colored(double percent, const char *color, int *visible_width)
{
    int filled = (int)((percent / 100.0) * PROGRESS_WIDTH + 0.5);

    if (filled < 0) {
        filled = 0;
    }
    if (filled > PROGRESS_WIDTH) {
        filled = PROGRESS_WIDTH;
    }

    ui_put(visible_width, NULL, "[");
    printf("%s", color);
    for (int i = 0; i < filled; ++i) {
        printf("█");
        (*visible_width)++;
    }
    printf(ANSI_GRAY);
    for (int i = filled; i < PROGRESS_WIDTH; ++i) {
        printf("░");
        (*visible_width)++;
    }
    printf(ANSI_RESET);
    ui_put(visible_width, NULL, "]");
}

static void ui_collect_stats(UiStats *stats)
{
    block_header_t *current;

    memset(stats, 0, sizeof(*stats));

    pthread_mutex_lock(&allocator_mutex);

    current = allocator_get_block_list_head();
    while (current != NULL) {
        stats->total_blocks++;
        if (current->is_free) {
            stats->free_blocks++;
            stats->total_free += current->size;
            if (current->size > stats->largest_free) {
                stats->largest_free = current->size;
            }
        } else {
            stats->used_blocks++;
        }
        current = current->next_all;
    }

    stats->reserved = total_reserved_memory;
    stats->allocated = total_allocated_memory;
    stats->active_blocks = active_block_count;

    pthread_mutex_unlock(&allocator_mutex);

    if (stats->reserved > 0) {
        stats->usage_percent = ((double)stats->allocated / (double)stats->reserved) * 100.0;
    }
    if (stats->total_free > 0) {
        stats->fragmentation_percent =
            (1.0 - ((double)stats->largest_free / (double)stats->total_free)) * 100.0;
    }
}

static int demo_is_running(void)
{
    int running;

    pthread_mutex_lock(&demo_mutex);
    running = demo_running;
    pthread_mutex_unlock(&demo_mutex);

    return running;
}

static size_t demo_get_allocation_count(void)
{
    size_t count;

    pthread_mutex_lock(&demo_mutex);
    count = demo_allocation_count;
    pthread_mutex_unlock(&demo_mutex);

    return count;
}

static void demo_store_allocation(void *ptr)
{
    pthread_mutex_lock(&demo_mutex);
    if (demo_allocation_count < MAX_DEMO_ALLOCS) {
        demo_allocations[demo_allocation_count++] = ptr;
    } else {
        my_free(ptr);
    }
    pthread_mutex_unlock(&demo_mutex);
}

static void demo_free_one(void)
{
    void *ptr = NULL;

    pthread_mutex_lock(&demo_mutex);
    if (demo_allocation_count > 0) {
        ptr = demo_allocations[--demo_allocation_count];
        demo_allocations[demo_allocation_count] = NULL;
    }
    pthread_mutex_unlock(&demo_mutex);

    if (ptr != NULL) {
        my_free(ptr);
    }
}

static void *demo_worker(void *arg)
{
    (void)arg;

    while (demo_is_running()) {
        size_t local_step;
        size_t local_count;
        size_t size;

        pthread_mutex_lock(&demo_mutex);
        local_step = demo_step++;
        local_count = demo_allocation_count;
        pthread_mutex_unlock(&demo_mutex);

        /*
         * Demo iş parçacığı farklı boyutlarda tahsis ve serbest bırakma yapar.
         * Böylece dashboard barları ve bellek haritası gözle görülür şekilde değişir.
         */
        if (local_count == 0 || (local_count < MAX_DEMO_ALLOCS && (local_step % 4) != 3)) {
            void *ptr;

            size = 64 + ((local_step * 73) % 704);
            ptr = my_malloc(size);
            if (ptr != NULL) {
                memset(ptr, (int)(local_step & 0xFF), size);
                demo_store_allocation(ptr);
                ui_set_status("Canlı demo: my_malloc(%zu) çalıştı", size);
            } else {
                ui_set_status("Canlı demo: my_malloc(%zu) başarısız", size);
            }
        } else {
            demo_free_one();
            ui_set_status("Canlı demo: bir blok my_free ile bırakıldı");
        }

        ui_sleep_ms(180);
    }

    return NULL;
}

static void demo_start(void)
{
    int rc;

    pthread_mutex_lock(&demo_mutex);
    if (demo_running) {
        pthread_mutex_unlock(&demo_mutex);
        ui_set_status("Canlı demo zaten çalışıyor");
        return;
    }

    demo_running = 1;
    pthread_mutex_unlock(&demo_mutex);

    rc = pthread_create(&demo_thread, NULL, demo_worker, NULL);
    if (rc != 0) {
        pthread_mutex_lock(&demo_mutex);
        demo_running = 0;
        pthread_mutex_unlock(&demo_mutex);
        ui_set_status("Canlı demo başlatılamadı");
        return;
    }

    demo_thread_started = 1;
    ui_set_status("Canlı demo başladı: arka planda malloc/free yapılıyor");
}

static void demo_stop(void)
{
    pthread_mutex_lock(&demo_mutex);
    if (!demo_running) {
        pthread_mutex_unlock(&demo_mutex);
        ui_set_status("Canlı demo zaten durmuş");
        return;
    }
    demo_running = 0;
    pthread_mutex_unlock(&demo_mutex);

    if (demo_thread_started) {
        pthread_join(demo_thread, NULL);
        demo_thread_started = 0;
    }

    ui_set_status("Canlı demo durdu; bloklar ekranda bırakıldı");
}

static void demo_toggle(void)
{
    if (demo_is_running()) {
        demo_stop();
    } else {
        demo_start();
    }
}

static void demo_cleanup(void)
{
    demo_stop();

    while (1) {
        size_t count;

        pthread_mutex_lock(&demo_mutex);
        count = demo_allocation_count;
        pthread_mutex_unlock(&demo_mutex);

        if (count == 0) {
            break;
        }
        demo_free_one();
    }
}

static void ui_metric_line(const char *left_label,
                           size_t left_value,
                           const char *left_color,
                           const char *right_label,
                           size_t right_value,
                           const char *right_color)
{
    int width = 0;
    char value[64];

    ui_line_begin();
    ui_put(&width, ANSI_GRAY, "│ ");
    ui_put(&width, ANSI_WHITE, left_label);
    ui_put(&width, ANSI_GRAY, ": ");
    snprintf(value, sizeof(value), "%zu", left_value);
    ui_put(&width, left_color, value);
    ui_put(&width, ANSI_GRAY, "    │ ");
    ui_put(&width, ANSI_WHITE, right_label);
    ui_put(&width, ANSI_GRAY, ": ");
    snprintf(value, sizeof(value), "%zu", right_value);
    ui_put(&width, right_color, value);
    ui_line_end(width);
}

static void ui_draw_dashboard(void)
{
    UiStats stats;
    char text[128];
    int width;
    size_t demo_count;

    ui_collect_stats(&stats);
    demo_count = demo_get_allocation_count();

    ui_line_title("◆", "Gösterge Paneli", "durum mutex altında okunur");
    ui_empty_line();

    width = 0;
    ui_line_begin();
    ui_put(&width, ANSI_CYAN, "Kullanım      ");
    ui_progress_colored(stats.usage_percent, ANSI_GREEN, &width);
    snprintf(text, sizeof(text), "  %.1f%%  (%zu/%zu byte)",
             stats.usage_percent, stats.allocated, stats.reserved);
    ui_put(&width, ANSI_WHITE, text);
    ui_line_end(width);

    width = 0;
    ui_line_begin();
    ui_put(&width, ANSI_CYAN, "Parçalanma    ");
    ui_progress_colored(stats.fragmentation_percent, ANSI_YELLOW, &width);
    snprintf(text, sizeof(text), "  %.1f%%  en büyük boş: %zu byte",
             stats.fragmentation_percent, stats.largest_free);
    ui_put(&width, ANSI_WHITE, text);
    ui_line_end(width);

    ui_empty_line();
    ui_metric_line("Toplam blok", stats.total_blocks, ANSI_YELLOW,
                   "Aktif blok", stats.active_blocks, ANSI_MAGENTA);
    ui_metric_line("Dolu blok", stats.used_blocks, ANSI_RED,
                   "Boş blok", stats.free_blocks, ANSI_GREEN);
    ui_metric_line("Serbest payload", stats.total_free, ANSI_GREEN,
                   "Arayüz tahsisi", terminal_allocation_count, ANSI_BLUE);
    {
        int demo_width = 0;
        char demo_text[80];

        ui_line_begin();
        snprintf(demo_text, sizeof(demo_text), "Canlı demo: %s",
                 demo_is_running() ? "ÇALIŞIYOR" : "DURDU");
        ui_put(&demo_width, demo_is_running() ? ANSI_GREEN ANSI_BOLD : ANSI_GRAY, demo_text);
        snprintf(demo_text, sizeof(demo_text), "    Demo tahsisi: %zu", demo_count);
        ui_put(&demo_width, ANSI_WHITE, demo_text);
        ui_line_end(demo_width);
    }
    ui_empty_line();
    ui_line_plain("İpucu: [d] canlı demoyu başlatır; bloklar arka planda değişir.");
}

static void ui_draw_memory_map(void)
{
    block_header_t *current;
    size_t total_payload = 0;
    size_t index = 0;

    ui_line_title("■", "Bellek Haritası", "yeşil boş, kırmızı dolu blok");

    {
        int width = 0;

        ui_line_begin();
        ui_put(&width, ANSI_WHITE, "Lejant  ");
        ui_put(&width, ANSI_GREEN, "████ boş/serbest");
        ui_put(&width, ANSI_GRAY, "   ");
        ui_put(&width, ANSI_RED, "████ dolu/tahsisli");
        ui_line_end(width);
        ui_line_plain("Blok uzunluğu yaklaşık boyutu gösterir; liste heap sırasına göredir.");
    }

    ui_empty_line();

    pthread_mutex_lock(&allocator_mutex);

    current = allocator_get_block_list_head();
    while (current != NULL) {
        total_payload += current->size;
        current = current->next_all;
    }

    current = allocator_get_block_list_head();
    while (current != NULL && index < 10) {
        const char *color = current->is_free ? ANSI_GREEN : ANSI_RED;
        const char *state = current->is_free ? "BOŞ " : "DOLU";
        int units = 1;
        int width = 0;
        char text[96];

        if (total_payload > 0) {
            units = (int)(((double)current->size / (double)total_payload) * MEMORY_BAR_WIDTH + 0.5);
        }
        if (units < 1) {
            units = 1;
        }
        if (units > MEMORY_BAR_WIDTH) {
            units = MEMORY_BAR_WIDTH;
        }

        ui_line_begin();
        snprintf(text, sizeof(text), "#%02zu  %-4s  %8zu byte  ", index, state, current->size);
        ui_put(&width, ANSI_WHITE, text);
        printf("%s", color);
        for (int i = 0; i < units; ++i) {
            printf("█");
            width++;
        }
        printf(ANSI_RESET);
        ui_line_end(width);

        current = current->next_all;
        index++;
    }

    if (current != NULL) {
        ui_line_plain("Not: İlk 10 blok gösteriliyor; ayrıntı için rapor çıktısını kullan.");
    } else if (index == 0) {
        ui_line_plain("Henüz blok yok. [a] ile my_malloc(128) çağırarak haritayı başlat.");
    }

    pthread_mutex_unlock(&allocator_mutex);
}

static void ui_draw_logs(void)
{
    char line[160];
    char status_copy[192];

    ui_get_status(status_copy, sizeof(status_copy));
    ui_line_title("●", "Kayıtlar & Sızıntılar", "son komut ve leak bilgisi");
    ui_empty_line();
    snprintf(line, sizeof(line), "Son işlem          : %s", status_copy);
    ui_line_plain(line);
    snprintf(line, sizeof(line), "Arayüz tahsisi     : %zu", terminal_allocation_count);
    ui_line_plain(line);
    snprintf(line, sizeof(line), "Canlı demo         : %s (%zu blok)",
             demo_is_running() ? "çalışıyor" : "durdu", demo_get_allocation_count());
    ui_line_plain(line);
    ui_line_plain("Leak kontrolü      : Çıkışta allocator_report_memory_leaks() çalışır.");
    ui_line_plain("Log dosyası        : allocator_test.log");
    ui_empty_line();
    ui_line_plain("Demo akışı: [d] başlat, [2] haritada izle, [d] durdur, [q] çık.");
}

static void ui_malloc(size_t size)
{
    void *ptr;

    if (terminal_allocation_count >= MAX_TERMINAL_ALLOCS) {
        ui_set_status("Allocation slotları dolu");
        return;
    }

    ptr = my_malloc(size);
    if (ptr == NULL) {
        ui_set_status("my_malloc(%zu) başarısız", size);
        return;
    }

    terminal_allocations[terminal_allocation_count++] = ptr;
    ui_set_status("my_malloc(%zu) -> %p", size, ptr);
}

static void ui_free_last(void)
{
    void *ptr;

    if (terminal_allocation_count == 0) {
        ui_set_status("Serbest bırakılacak arayüz tahsisi yok");
        return;
    }

    ptr = terminal_allocations[--terminal_allocation_count];
    terminal_allocations[terminal_allocation_count] = NULL;
    my_free(ptr);
    ui_set_status("my_free(%p)", ptr);
}

static void ui_command_bar(void)
{
    int width = 0;
    char status_copy[192];

    ui_get_status(status_copy, sizeof(status_copy));

    ui_border_mid();
    ui_line_begin();
    ui_put(&width, ANSI_YELLOW ANSI_BOLD, "KOMUTLAR");
    ui_put(&width, ANSI_GRAY, "  ");
    ui_put(&width, ANSI_WHITE, "[1] Gösterge");
    ui_put(&width, ANSI_GRAY, "  ");
    ui_put(&width, ANSI_WHITE, "[2] Harita");
    ui_put(&width, ANSI_GRAY, "  ");
    ui_put(&width, ANSI_WHITE, "[3] Kayıtlar");
    ui_line_end(width);

    width = 0;
    ui_line_begin();
    ui_put(&width, ANSI_GRAY, "         ");
    ui_put(&width, ANSI_GRAY, "  ");
    ui_put(&width, ANSI_GREEN, "[a] Malloc");
    ui_put(&width, ANSI_GRAY, "  ");
    ui_put(&width, ANSI_RED, "[f] Free");
    ui_put(&width, ANSI_GRAY, "  ");
    ui_put(&width, demo_is_running() ? ANSI_YELLOW ANSI_BOLD : ANSI_YELLOW, "[d] Demo");
    ui_put(&width, ANSI_GRAY, "  ");
    ui_put(&width, ANSI_MAGENTA, "[q] Çıkış");
    ui_line_end(width);

    width = 0;
    ui_line_begin();
    ui_put(&width, ANSI_BLUE ANSI_BOLD, "DURUM");
    ui_put(&width, ANSI_GRAY, "  ");
    ui_put(&width, ANSI_WHITE, status_copy);
    ui_line_end(width);

    ui_border_bottom();
    printf(ANSI_BOLD ANSI_CYAN "Komut girin > " ANSI_RESET);
    fflush(stdout);
}

void run_allocator_terminal_ui(void)
{
    TerminalTab active_tab = TERM_TAB_DASHBOARD;
    int running = 1;

    while (running) {
        int command;

        ui_clear_screen();
        ui_border_top();
        ui_tab_line(active_tab);
        ui_subtitle_line();
        ui_border_mid();

        if (active_tab == TERM_TAB_DASHBOARD) {
            ui_draw_dashboard();
        } else if (active_tab == TERM_TAB_MEMORY_MAP) {
            ui_draw_memory_map();
        } else {
            ui_draw_logs();
        }

        ui_command_bar();

        command = ui_read_command_with_timeout(UI_REFRESH_MS);
        if (command == 0) {
            continue;
        }
        if (command == EOF) {
            break;
        }

        switch (command) {
            case '1':
                active_tab = TERM_TAB_DASHBOARD;
                ui_set_status("Gösterge sekmesine geçildi");
                break;
            case '2':
                active_tab = TERM_TAB_MEMORY_MAP;
                ui_set_status("Bellek Haritası sekmesine geçildi");
                break;
            case '3':
                active_tab = TERM_TAB_LOGS;
                ui_set_status("Kayıtlar & Sızıntılar sekmesine geçildi");
                break;
            case 'a':
            case 'A':
                ui_malloc(128);
                break;
            case 'f':
            case 'F':
                ui_free_last();
                break;
            case 'd':
            case 'D':
                demo_toggle();
                break;
            case 'q':
            case 'Q':
                running = 0;
                break;
            default:
                ui_set_status("Bilinmeyen komut: 1, 2, 3, a, f, d veya q kullanın");
                break;
        }
    }

    demo_cleanup();

    while (terminal_allocation_count > 0) {
        ui_free_last();
    }

    printf(ANSI_RESET "\033[?25h\nTerminal arayüz kapatıldı.\n");
}
