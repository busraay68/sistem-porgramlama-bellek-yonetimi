CC := gcc
TARGET := test_allocator
BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests

CPPFLAGS := -D_DEFAULT_SOURCE
CFLAGS := -Wall -Wextra -std=c11 -g -O2 -pthread -I$(INC_DIR)
LDFLAGS := -pthread
LDLIBS := -lm
RAW_ALLOCATOR_FLAGS := -Dmy_malloc=allocator_raw_malloc -Dmy_free=allocator_raw_free -Dmy_calloc=allocator_raw_calloc

SRC_FILES := \
	$(SRC_DIR)/allocator_core.c \
	$(SRC_DIR)/allocator_debug.c \
	$(SRC_DIR)/allocator_safety.c \
	$(SRC_DIR)/allocator_terminal_ui.c \
	$(SRC_DIR)/allocator_threadsafe.c

TEST_FILES := \
	$(TEST_DIR)/test_allocator.c

SRC_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_FILES))
TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/%.o,$(TEST_FILES))
OBJS := $(SRC_OBJS) $(TEST_OBJS) $(BUILD_DIR)/allocator_ops.o

.PHONY: all run test clean help

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

$(BUILD_DIR)/allocator_ops.o: $(SRC_DIR)/allocator_ops.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(RAW_ALLOCATOR_FLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	./$(TARGET) --no-ui

clean:
	rm -rf $(BUILD_DIR) $(TARGET) allocator_test.log

help:
	@echo "Komutlar:"
	@echo "  make        Projeyi derler"
	@echo "  make test   Testleri terminal arayuzu acmadan calistirir"
	@echo "  make run    Testlerden sonra ANSI terminal arayuzunu acar"
	@echo "  make clean  Derleme ciktilarini temizler"
