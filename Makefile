# Everything is C++ now: compiler sources and runtime alike, built with clang++.
CXX      := clang++
CXXFLAGS := -Wall -Wextra -g -std=c++17 $(shell llvm-config-18 --cxxflags)
# Drop flags llvm-config injects that we don't want:
#   -fno-exceptions / -fno-rtti  -- may be needed by future C++ work
#   -I/usr/lib/llvm-18/include   -- replace with -isystem below so warnings
#                                   inside LLVM headers don't drown ours out
CXXFLAGS := $(filter-out -fno-exceptions -fno-rtti -Wno-maybe-uninitialized -I/usr/lib/llvm-18/include,$(CXXFLAGS))
CXXFLAGS += -isystem /usr/lib/llvm-18/include
LDFLAGS  := $(shell llvm-config-18 --ldflags --libs core target x86 codegen passes) $(shell llvm-config-18 --system-libs)

SRC_DIR      := src
BUILD_DIR    := build
RUNTIME_DIR  := runtime
SRCS         := $(wildcard $(SRC_DIR)/*.cpp)
OBJS         := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
TARGET       := $(BUILD_DIR)/cc
RUNTIME_SRCS := $(wildcard $(RUNTIME_DIR)/*.cpp)
RUNTIME_OBJS := $(patsubst $(RUNTIME_DIR)/%.cpp,$(BUILD_DIR)/runtime/%.o,$(RUNTIME_SRCS))
RUNTIME_LIB  := $(BUILD_DIR)/libruntime.a

# Wasm build: the whole frontend + the textual IR printer, no LLVM.
# codegen.cpp (needs libLLVM) and main.cpp (CLI) are excluded;
# wasm_api.cpp's ce_compile is the exported entry point.
WASM_SRCS := $(filter-out $(SRC_DIR)/codegen.cpp $(SRC_DIR)/main.cpp,$(SRCS))
WASM_OUT  := $(BUILD_DIR)/compiler.js

.PHONY: all clean test run wasm

all: $(TARGET) $(RUNTIME_LIB)

wasm: | $(BUILD_DIR)
	emcc -O2 -std=c++17 $(WASM_SRCS) -o $(WASM_OUT) \
	  -sEXPORTED_FUNCTIONS=_ce_compile \
	  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
	  -sMODULARIZE=1 -sEXPORT_NAME=CECompiler \
	  -sALLOW_MEMORY_GROWTH=1
	@ls -la $(BUILD_DIR)/compiler.js $(BUILD_DIR)/compiler.wasm

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Runtime: linked into programs the compiler produces, NOT into cc itself.
# No LLVM headers needed, so it skips CXXFLAGS' llvm-config baggage.
# One object per runtime/*.cpp, bundled into an archive so the link
# interface stays a single artifact however many files runtime/ grows to.
$(BUILD_DIR)/runtime/%.o: $(RUNTIME_DIR)/%.cpp $(RUNTIME_DIR)/runtime.h | $(BUILD_DIR)/runtime
	$(CXX) -Wall -Wextra -std=c++17 -c $< -o $@

$(RUNTIME_LIB): $(RUNTIME_OBJS)
	ar rcs $@ $^

$(BUILD_DIR) $(BUILD_DIR)/runtime:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

test: $(TARGET) $(RUNTIME_LIB)
	@bash tests/run_tests.sh

# Usage: make run FILE=tests/pass/add.c
# Link with clang++ so libstdc++ (needed by the C++ runtime) comes in.
# User object must come BEFORE the archive: ar members only resolve
# symbols already requested by earlier objects.
run: $(TARGET) $(RUNTIME_LIB)
	@$(TARGET) $(FILE) /tmp/cc_out.o && $(CXX) /tmp/cc_out.o $(RUNTIME_LIB) -o /tmp/cc_out && /tmp/cc_out; echo "exit code: $$?"
