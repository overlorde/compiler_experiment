# Everything is C++ now: compiler sources and runtime alike, built with clang++.
CXX      := clang++
EMCC     ?= emcc
.DEFAULT_GOAL := all
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
TYPE_TEST    := $(BUILD_DIR)/types_test
FRONTEND_TEST := $(BUILD_DIR)/frontend_test
REGION_TEST  := $(BUILD_DIR)/regions_test
UNIFY_DEMO   := $(BUILD_DIR)/unification_demo

# Wasm build: the whole frontend + the textual IR printer, no LLVM.
# codegen.cpp (needs libLLVM) and main.cpp (CLI) are excluded;
# wasm_api.cpp's ce_compile is the exported entry point.
WASM_SRCS := $(filter-out $(SRC_DIR)/codegen.cpp $(SRC_DIR)/main.cpp,$(SRCS))
WASM_OUT  := $(BUILD_DIR)/compiler.js

.PHONY: all clean test run wasm test-wasm demo-unification
.PHONY: test-region-sanitizers
.PHONY: test-diagnostic-disclosure study-diagnostic-disclosure

DIAGNOSTIC_SRCS := $(filter-out src/main.cpp src/codegen.cpp src/wasm_api.cpp,$(SRCS))
build/diagnostic_checker: experiments/diagnostic_disclosure/checker.cpp $(DIAGNOSTIC_SRCS) $(wildcard src/*.h) | $(BUILD_DIR)
	$(CXX) -Wall -Wextra -g -std=c++17 -Isrc $< $(DIAGNOSTIC_SRCS) -o $@

build/diagnostic_test: experiments/diagnostic_disclosure/diagnostic_test.cpp src/error.cpp src/types.cpp src/error.h src/types.h | $(BUILD_DIR)
	$(CXX) -Wall -Wextra -g -std=c++17 -Isrc $< src/error.cpp src/types.cpp -o $@

test-diagnostic-disclosure: build/diagnostic_checker build/diagnostic_test
	@build/diagnostic_test
	python3 -B experiments/diagnostic_disclosure/test_study.py

study-diagnostic-disclosure: build/diagnostic_checker
	python3 -B experiments/diagnostic_disclosure/study.py

.PHONY: test-diagnostic-repair study-diagnostic-repair
build/diagnostic_repair_checker: experiments/diagnostic_repair_tasks/checker.cpp $(DIAGNOSTIC_SRCS) $(wildcard src/*.h) | $(BUILD_DIR)
	$(CXX) -Wall -Wextra -g -std=c++17 -Isrc $< $(DIAGNOSTIC_SRCS) -o $@

test-diagnostic-repair: build/diagnostic_repair_checker
	python3 -B experiments/diagnostic_repair_tasks/test_tasks.py

study-diagnostic-repair: build/diagnostic_repair_checker
	python3 -B experiments/diagnostic_repair_tasks/study.py

all: $(TARGET) $(RUNTIME_LIB)

wasm: | $(BUILD_DIR)
	python3 -B scripts/build_wasm.py --emcc "$(EMCC)" --output "$(BUILD_DIR)"

test-wasm:
	node tests/wasm_test.cjs "$(BUILD_DIR)" "$$(cat VERSION)"

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# -MMD -MP: emit a .d file per object listing every header it includes, so
# editing a header rebuilds exactly the objects that saw it. Without this,
# a struct change in types.h left stale .o files reading old field offsets.
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Runtime: linked into programs the compiler produces, NOT into cc itself.
# No LLVM headers needed, so it skips CXXFLAGS' llvm-config baggage.
# One object per runtime/*.cpp, bundled into an archive so the link
# interface stays a single artifact however many files runtime/ grows to.
$(BUILD_DIR)/runtime/%.o: $(RUNTIME_DIR)/%.cpp | $(BUILD_DIR)/runtime
	$(CXX) -Wall -Wextra -std=c++17 -MMD -MP -c $< -o $@

$(RUNTIME_LIB): $(RUNTIME_OBJS)
	ar rcs $@ $^

$(BUILD_DIR) $(BUILD_DIR)/runtime:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

# Pull in the compiler-generated header dependencies (absent on first build).
-include $(OBJS:.o=.d) $(RUNTIME_OBJS:.o=.d)

$(TYPE_TEST): tests/types_test.cpp $(SRC_DIR)/types.cpp $(SRC_DIR)/types.h | $(BUILD_DIR)
	$(CXX) -Wall -Wextra -g -std=c++17 -I$(SRC_DIR) tests/types_test.cpp $(SRC_DIR)/types.cpp -o $@

$(UNIFY_DEMO): experiments/unification/demo.cpp $(SRC_DIR)/types.cpp $(SRC_DIR)/types.h | $(BUILD_DIR)
	$(CXX) -Wall -Wextra -g -std=c++17 -I$(SRC_DIR) experiments/unification/demo.cpp $(SRC_DIR)/types.cpp -o $@

demo-unification: $(UNIFY_DEMO)
	@$(UNIFY_DEMO)

$(FRONTEND_TEST): tests/frontend_test.cpp $(filter-out src/main.cpp src/codegen.cpp,$(SRCS)) $(wildcard src/*.h) | $(BUILD_DIR)
	$(CXX) -Wall -Wextra -g -std=c++17 -Isrc $< $(filter-out src/main.cpp src/codegen.cpp,$(SRCS)) -o $@

$(REGION_TEST): tests/regions_test.cpp runtime/memory.cpp runtime/runtime.h | $(BUILD_DIR)
	$(CXX) -Wall -Wextra -g -std=c++17 -DCE_REGION_TEST -Iruntime tests/regions_test.cpp runtime/memory.cpp -o $@

test: $(TARGET) $(RUNTIME_LIB) $(TYPE_TEST) $(UNIFY_DEMO) $(FRONTEND_TEST) $(REGION_TEST)
	@$(TYPE_TEST)
	@$(UNIFY_DEMO) --check
	@$(FRONTEND_TEST)
	@$(REGION_TEST)
	@bash tests/run_tests.sh

test-region-sanitizers: $(TARGET)
	python3 -B tests/run_region_sanitizers.py

# Usage: make run FILE=tests/pass/add.ce
# Link with clang++ so libstdc++ (needed by the C++ runtime) comes in.
# User object must come BEFORE the archive: ar members only resolve
# symbols already requested by earlier objects.
run: $(TARGET) $(RUNTIME_LIB)
	@$(TARGET) $(FILE) /tmp/cc_out.o && $(CXX) /tmp/cc_out.o $(RUNTIME_LIB) -o /tmp/cc_out && /tmp/cc_out; echo "exit code: $$?"
