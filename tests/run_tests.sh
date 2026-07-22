#!/usr/bin/env bash
#
# Regression test harness for compiler_experiment.
#
# Layout (uniform: each test is two files, no third sidecar):
#   tests/pass/<name>.c          source snippet
#   tests/pass/<name>.expected   exact stdout the program must produce
#
#   tests/fail/<name>.c          source snippet that must NOT typecheck
#   tests/fail/<name>.expected   extended regex matched against stderr
#
# Pass test:
#   compile -> link with runtime.o -> run -> stdout must equal .expected
#   AND program must exit 0.
#
# Fail test:
#   compile (no link, no run); compiler must exit non-zero AND its stderr
#   must match the regex in .expected.
#

set -uo pipefail   # NOT -e: failures are normal control flow here

COMPILER="./build/cc"
RUNTIME="./build/libruntime.a"

PASS=0
FAIL=0
FAILED=()

if [[ ! -x "$COMPILER" ]]; then
    echo "ERROR: $COMPILER not found -- run 'make' first" >&2
    exit 1
fi
if [[ ! -f "$RUNTIME" ]]; then
    echo "ERROR: $RUNTIME not found -- run 'make' first" >&2
    exit 1
fi

# -------------------------------------------------------------------------
# run_pass <src.c>
# -------------------------------------------------------------------------
run_pass() {
    local src=$1
    local expected="${src%.c}.expected"
    local name="${src#tests/}"

    if [[ ! -f "$expected" ]]; then
        echo "SKIP  $name (no .expected)"
        return
    fi

    local obj exe out
    obj=$(mktemp /tmp/cc_test_XXXXXX.o)
    exe=$(mktemp /tmp/cc_test_XXXXXX)
    out=$(mktemp /tmp/cc_test_XXXXXX.out)

    # compile (suppress the compiler's own stdout/stderr -- they're noisy)
    if ! "$COMPILER" "$src" "$obj" >/dev/null 2>&1; then
        echo "FAIL  $name (compile error)"
        FAIL=$((FAIL + 1)); FAILED+=("$name: compile")
        rm -f "$obj" "$exe" "$out"; return
    fi
    # link (clang++: the runtime is C++ now, so libstdc++ must come along)
    if ! clang++ "$obj" "$RUNTIME" -o "$exe" >/dev/null 2>&1; then
        echo "FAIL  $name (link error)"
        FAIL=$((FAIL + 1)); FAILED+=("$name: link")
        rm -f "$obj" "$exe" "$out"; return
    fi
    # run
    local exit=0
    "$exe" > "$out" 2>&1 || exit=$?

    local failed=0
    if [[ "$exit" != "0" ]]; then
        echo "FAIL  $name (exit $exit, expected 0)"
        failed=1
    fi
    if ! diff -q "$expected" "$out" >/dev/null 2>&1; then
        echo "FAIL  $name (stdout mismatch)"
        echo "  expected: $(cat "$expected")"
        echo "  actual:   $(cat "$out")"
        failed=1
    fi

    if [[ $failed -eq 0 ]]; then
        echo "PASS  $name"
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1)); FAILED+=("$name")
    fi
    rm -f "$obj" "$exe" "$out"
}

# -------------------------------------------------------------------------
# run_fail <src.c>
# -------------------------------------------------------------------------
run_fail() {
    local src=$1
    local expected="${src%.c}.expected"
    local name="${src#tests/}"

    if [[ ! -f "$expected" ]]; then
        echo "SKIP  $name (no .expected)"
        return
    fi

    local obj err
    obj=$(mktemp /tmp/cc_test_XXXXXX.o)
    err=$(mktemp /tmp/cc_test_XXXXXX.err)

    # Compile must fail. Discard stdout (scope dumps); keep stderr for matching.
    "$COMPILER" "$src" "$obj" >/dev/null 2>"$err"
    local rc=$?
    rm -f "$obj"

    local regex
    regex=$(cat "$expected")

    if [[ $rc -eq 0 ]]; then
        echo "FAIL  $name (compiler accepted; expected non-zero exit)"
        FAIL=$((FAIL + 1)); FAILED+=("$name: accepted")
    elif ! grep -E -q -- "$regex" "$err"; then
        echo "FAIL  $name (stderr did not match)"
        echo "  regex:  $regex"
        echo "  stderr: $(cat "$err")"
        FAIL=$((FAIL + 1)); FAILED+=("$name: regex")
    else
        echo "PASS  $name"
        PASS=$((PASS + 1))
    fi
    rm -f "$err"
}

# -------------------------------------------------------------------------
# walk both directories
# -------------------------------------------------------------------------
shopt -s nullglob
for f in tests/pass/*.c; do run_pass "$f"; done
for f in tests/fail/*.c; do run_fail "$f"; done

echo ""
echo "Results: $PASS passed, $FAIL failed, out of $((PASS + FAIL)) tests"
if [[ $FAIL -gt 0 ]]; then
    echo "Failures:"
    for n in "${FAILED[@]}"; do echo "  $n"; done
    exit 1
fi
