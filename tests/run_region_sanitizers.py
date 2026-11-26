#!/usr/bin/env python3
"""Run inferred/pointer-returning code through both emitters under ASan/UBSan.

Rename the generated entry point so a C++ harness can verify that every
compilation returns with zero live regions/blocks, then invoke it a second
time to catch stale runtime state. No third-party Python packages required.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def run(args, **kwargs):
    try:
        return subprocess.run(args, cwd=ROOT, check=True, capture_output=True, text=True, **kwargs)
    except subprocess.CalledProcessError as error:
        print(error.stdout, end="")
        print(error.stderr, end="")
        raise


def main():
    sources = sorted((ROOT / "tests/pass").glob("hm_*.ce"))
    sources += sorted((ROOT / "tests/pass").glob("region_*.ce"))
    with tempfile.TemporaryDirectory(prefix="ce_regions_") as directory:
        temp = Path(directory)
        harness = temp / "harness.cpp"
        harness.write_text('''#include <cassert>
#include <cstdint>
extern "C" int ce_test_entry();
extern "C" std::int64_t __ce_region_live_blocks();
extern "C" std::int64_t __ce_region_live_regions();
extern "C" std::int64_t __ce_region_peak_blocks();
int main() {
    for (int i = 0; i < 2; ++i) {
        assert(ce_test_entry() == 0);
        assert(__ce_region_live_blocks() == 0);
        assert(__ce_region_live_regions() == 0);
        assert(__ce_region_peak_blocks() <= CE_EXPECT_PEAK);
    }
}
''')
        count = 0
        for source in sources:
            for emitter in ("-emit-ir", "-emit-llvm"):
                ir = temp / "program.ll"
                command = [str(ROOT / "build/cc"), "-q", emitter]
                if emitter == "-emit-llvm":
                    command += ["-O1"]
                run(command + [str(source), str(ir)])
                text = ir.read_text()
                assert "@main(" in text
                ir.write_text(text.replace("@main(", "@ce_test_entry("))
                executable = temp / "program"
                run(["clang++", "-g", "-std=c++17", "-fsanitize=address,undefined",
                     "-fno-sanitize-recover=all", "-DCE_REGION_TEST", str(ir),
                     "-DCE_EXPECT_PEAK=" + ("2" if source.name == "region_loop.ce" else "1000000"),
                     "runtime/memory.cpp", "runtime/print.cpp", str(harness), "-o", str(executable)])
                env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1")
                result = run([str(executable)], env=env)
                assert result.stdout == source.with_suffix(".expected").read_text() * 2, source
                count += 1
                print(f"PASS  {source.name} {emitter}")
        print(f"Sanitizers: {count} programs passed through both entry-point invocations")


if __name__ == "__main__":
    main()
