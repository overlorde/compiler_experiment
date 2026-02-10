#!/usr/bin/env bash
#
# Test harness for compiler_experiment
#
# For each .c file in tests/phase*/:
#   1. Compile with build/cc → .o
#   2. Link with system cc → executable
#   3. Run and capture stdout + exit code
#   4. Compare stdout against .expected file
#   5. If .exitcode file exists, also check exit code
#

set -euo pipefail

COMPILER="./build/cc"
PASS=0
FAIL=0
ERRORS=""

if [[ ! -x "$COMPILER" ]]; then
    echo "ERROR: Compiler not found at $COMPILER — run 'make' first"
    exit 1
fi

# Collect all test .c files, sorted
TEST_FILES=$(find tests/ -name '*.c' | sort)

if [[ -z "$TEST_FILES" ]]; then
    echo "No test files found."
    exit 0
fi

for src in $TEST_FILES; do
    expected="${src%.c}.expected"
    exitcode_file="${src%.c}.exitcode"
    test_name="${src#tests/}"

    if [[ ! -f "$expected" ]]; then
        echo "SKIP  $test_name (no .expected file)"
        continue
    fi

    # Temporary files for this test
    obj_file=$(mktemp /tmp/cc_test_XXXXXX.o)
    exe_file=$(mktemp /tmp/cc_test_XXXXXX)
    actual_file=$(mktemp /tmp/cc_test_XXXXXX.out)

    # Step 1: Compile .c -> .o with our compiler
    if ! "$COMPILER" "$src" "$obj_file" 2>/dev/null; then
        echo "FAIL  $test_name (compile error)"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  FAIL $test_name: compile error"
        rm -f "$obj_file" "$exe_file" "$actual_file"
        continue
    fi

    # Step 2: Link .o -> executable with system cc
    if ! cc "$obj_file" -o "$exe_file" 2>/dev/null; then
        echo "FAIL  $test_name (link error)"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  FAIL $test_name: link error"
        rm -f "$obj_file" "$exe_file" "$actual_file"
        continue
    fi

    # Step 3: Run and capture output + exit code
    actual_exit=0
    "$exe_file" > "$actual_file" 2>&1 || actual_exit=$?

    # Step 4: Compare stdout
    failed=0
    if ! diff -q "$expected" "$actual_file" > /dev/null 2>&1; then
        echo "FAIL  $test_name (output mismatch)"
        echo "  expected: $(cat "$expected")"
        echo "  actual:   $(cat "$actual_file")"
        failed=1
    fi

    # Step 5: Check exit code if .exitcode file exists
    if [[ -f "$exitcode_file" ]]; then
        expected_exit=$(cat "$exitcode_file" | tr -d '[:space:]')
        if [[ "$actual_exit" != "$expected_exit" ]]; then
            echo "FAIL  $test_name (exit code: expected $expected_exit, got $actual_exit)"
            failed=1
        fi
    fi

    if [[ $failed -eq 0 ]]; then
        echo "PASS  $test_name"
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  FAIL $test_name"
    fi

    rm -f "$obj_file" "$exe_file" "$actual_file"
done

echo ""
echo "Results: $PASS passed, $FAIL failed, out of $((PASS + FAIL)) tests"

if [[ $FAIL -gt 0 ]]; then
    echo -e "\nFailures:$ERRORS"
    exit 1
fi
