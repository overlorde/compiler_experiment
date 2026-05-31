CC       := gcc
CFLAGS   := -Wall -Wextra -g $(shell llvm-config-18 --cflags)
LDFLAGS  := $(shell llvm-config-18 --ldflags --libs core) $(shell llvm-config-18 --system-libs)

SRC_DIR     := src
BUILD_DIR   := build
RUNTIME_DIR := runtime
SRCS        := $(wildcard $(SRC_DIR)/*.c)
OBJS        := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET      := $(BUILD_DIR)/cc
RUNTIME_OBJ := $(BUILD_DIR)/runtime.o

.PHONY: all clean test run

all: $(TARGET) $(RUNTIME_OBJ)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Runtime: linked into programs the compiler produces, NOT into cc itself.
# Uses plain gcc -- doesn't need LLVM headers, just <stdio.h>.
$(RUNTIME_OBJ): $(RUNTIME_DIR)/runtime.c | $(BUILD_DIR)
	$(CC) -Wall -Wextra -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

test: $(TARGET) $(RUNTIME_OBJ)
	@bash tests/run_tests.sh

# Usage: make run FILE=tests/phase2/add.c
run: $(TARGET) $(RUNTIME_OBJ)
	@$(TARGET) $(FILE) /tmp/cc_out.o && cc /tmp/cc_out.o $(RUNTIME_OBJ) -o /tmp/cc_out && /tmp/cc_out; echo "exit code: $$?"
