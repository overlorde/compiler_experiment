CC       := gcc
CFLAGS   := -Wall -Wextra -g $(shell llvm-config-14 --cflags)
LDFLAGS  := $(shell llvm-config-14 --ldflags --libs core) $(shell llvm-config-14 --system-libs)

SRC_DIR  := src
BUILD_DIR := build
SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET   := $(BUILD_DIR)/cc

.PHONY: all clean test run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

test: $(TARGET)
	@bash tests/run_tests.sh

# Usage: make run FILE=tests/phase2/add.c
run: $(TARGET)
	@$(TARGET) $(FILE) /tmp/cc_out.o && cc /tmp/cc_out.o -o /tmp/cc_out && /tmp/cc_out; echo "exit code: $$?"
