#!/usr/bin/env python3
"""Build a versioned compiler and a manifest identifying source and artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def source_fingerprint():
    files = [ROOT / "VERSION", ROOT / "Makefile", Path(__file__).resolve()]
    for directory in ("src", "runtime"):
        files.extend(p for p in (ROOT / directory).glob("*") if p.suffix in (".cpp", ".h"))
    digest = hashlib.sha256()
    for path in sorted(files):
        data = path.read_bytes()
        digest.update(str(path.relative_to(ROOT)).encode() + b"\0")
        digest.update(str(len(data)).encode() + b"\0" + data)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--output", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = (ROOT / "VERSION").read_text().strip()
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    manifest = {
        "version": version,
        "compiler_commit": revision,
        "source_sha256": source_fingerprint(),
        "runtime_abi": "ce-function-regions-v1",
        "features": ["first-order-hm", "function-regions"],
    }
    sources = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "src").glob("*.cpp")
                     if p.name not in ("codegen.cpp", "main.cpp"))
    command = [args.emcc, "-O2", "-std=c++17", *sources,
               "-DCE_BUILD_VERSION=" + json.dumps(version),
               "-DCE_BUILD_INFO=" + json.dumps(json.dumps(manifest, sort_keys=True)),
               "-o", str(output / "compiler.js"),
               "-sEXPORTED_FUNCTIONS=_ce_compile,_ce_version,_ce_build_info",
               "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap",
               "-sMODULARIZE=1", "-sEXPORT_NAME=CECompiler", "-sALLOW_MEMORY_GROWTH=1"]
    subprocess.run(command, cwd=ROOT, check=True)
    manifest["artifacts"] = {}
    for filename in ("compiler.js", "compiler.wasm"):
        data = (output / filename).read_bytes()
        manifest["artifacts"][filename] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    (output / "version.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"Built {version} from {revision[:12]} / source {manifest['source_sha256'][:12]}")


if __name__ == "__main__":
    main()
