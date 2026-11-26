/* Validate the exact release artifacts, embedded identity, and source cases. */
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

async function main() {
  const directory = path.resolve(process.argv[2] || 'build');
  const expected = process.argv[3] || fs.readFileSync(path.join(__dirname, '../VERSION'), 'utf8').trim();
  const manifest = JSON.parse(fs.readFileSync(path.join(directory, 'version.json'), 'utf8'));
  assert.equal(manifest.version, expected, 'release pin differs from manifest');
  assert.match(manifest.compiler_commit, /^[0-9a-f]{40}$/);
  assert.match(manifest.source_sha256, /^[0-9a-f]{64}$/);
  for (const name of ['compiler.js', 'compiler.wasm']) {
    const data = fs.readFileSync(path.join(directory, name));
    assert.equal(data.length, manifest.artifacts[name].bytes, `${name} length`);
    assert.equal(crypto.createHash('sha256').update(data).digest('hex'), manifest.artifacts[name].sha256, `${name} checksum`);
  }
  const factory = require(path.join(directory, 'compiler.js'));
  const compiler = await factory({wasmBinary: fs.readFileSync(path.join(directory, 'compiler.wasm'))});
  assert.equal(compiler.ccall('ce_version', 'string', [], []), expected);
  const embedded = JSON.parse(compiler.ccall('ce_build_info', 'string', [], []));
  const {artifacts, ...identity} = manifest;
  assert.deepEqual(embedded, identity, 'binary identity differs from manifest');
  let checks = 0;
  const compile = source => compiler.ccall('ce_compile', 'string', ['string'], [source]);
  for (const kind of ['pass', 'fail']) {
    const cases = path.join(__dirname, kind);
    for (const file of fs.readdirSync(cases).filter(name => name.endsWith('.ce')).sort()) {
      const result = compile(fs.readFileSync(path.join(cases, file), 'utf8'));
      assert.ok(result.startsWith(kind === 'pass' ? 'OK\n' : 'ERR\n'), `${kind}/${file}: ${result}`);
      if (kind === 'pass') assert.ok(result.includes('@__ce_region_enter'), `${file}: region code missing`);
      checks++;
    }
  }
  // A rejection must not poison the next compilation in the browser session.
  assert.ok(compile('main(){let x=;}').startsWith('ERR\n'));
  assert.ok(compile('id(x){return x;} main(){print_int(id(42));print_bool(id(true));return 0;}').startsWith('OK\n'));
  console.log(`Wasm ${expected}: identity + checksums verified; ${checks} source regressions passed`);
}

main().catch(error => { console.error(error); process.exitCode = 1; });
