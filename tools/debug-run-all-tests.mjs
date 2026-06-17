#!/usr/bin/env node
/**
 * Run all Far language samples and write NDJSON results to debug-b34052.log
 */
import { spawnSync } from 'child_process';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const logPath = path.join(root, 'debug-b34052.log');
const SESSION = 'b34052';

const FAR_CANDIDATES = [
  path.join(root, 'far.exe'),
  path.join(process.env.LOCALAPPDATA || '', 'Programs', 'Far', 'far.exe'),
  'far.exe',
];

function resolveClang() {
  const installJson = path.join(process.env.APPDATA || '', 'Far', 'install.json');
  if (fs.existsSync(installJson)) {
    try {
      const j = JSON.parse(fs.readFileSync(installJson, 'utf8'));
      if (j.clangExe && fs.existsSync(j.clangExe)) return j.clangExe;
    } catch { /* ignore */ }
  }
  const wingetRoot = path.join(process.env.LOCALAPPDATA || '', 'Microsoft', 'WinGet', 'Packages');
  if (fs.existsSync(wingetRoot)) {
    for (const pkg of fs.readdirSync(wingetRoot)) {
      if (!pkg.includes('LLVM-MinGW')) continue;
      const bin = path.join(wingetRoot, pkg, 'llvm-mingw-20260602-ucrt-x86_64', 'bin', 'clang.exe');
      if (fs.existsSync(bin)) return bin;
      // search one level for version folder
      const full = path.join(wingetRoot, pkg);
      for (const sub of fs.readdirSync(full)) {
        const candidate = path.join(full, sub, 'bin', 'clang.exe');
        if (fs.existsSync(candidate)) return candidate;
      }
    }
  }
  return process.env.FAR_CLANG || null;
}

function resolveFar() {
  const installJson = path.join(process.env.APPDATA || '', 'Far', 'install.json');
  if (fs.existsSync(installJson)) {
    try {
      const j = JSON.parse(fs.readFileSync(installJson, 'utf8'));
      if (j.farExe && fs.existsSync(j.farExe)) return j.farExe;
    } catch { /* ignore */ }
  }
  for (const p of FAR_CANDIDATES) {
    if (p === 'far.exe') {
      const r = spawnSync('where', ['far.exe'], { encoding: 'utf8', shell: true });
      if (r.status === 0 && r.stdout.trim()) return r.stdout.trim().split(/\r?\n/)[0];
      continue;
    }
    if (fs.existsSync(p)) return p;
  }
  return null;
}

function appendLog(entry) {
  fs.appendFileSync(logPath, JSON.stringify({ sessionId: SESSION, timestamp: Date.now(), ...entry }) + '\n');
}

function categorize(rel) {
  const base = path.basename(rel, '.far');
  if (rel === 'program.far') return 'user';
  if (base.startsWith('suite_')) return 'suite';
  if (base.startsWith('types_operators') || base.startsWith('types_compare') || base.startsWith('types_bitwise') || base.startsWith('types_compound')) return 'operations';
  if (base.startsWith('types_string') || base.startsWith('types_char')) return 'strings';
  if (base.startsWith('types_cast') || base.startsWith('types_chain')) return 'casts';
  if (base.startsWith('types_vector') || base.startsWith('types_math') || base.startsWith('types_float')) return 'math-vectors';
  if (base.startsWith('types_import') || base.startsWith('types_stdlib') || base.startsWith('types_modules')) return 'stdlib';
  if (base.startsWith('types_io')) return 'io';
  if (base.startsWith('types_control') || base.startsWith('types_loop') || base.startsWith('types_return')) return 'syntax-control';
  if (base.startsWith('types_class') || base.startsWith('types_object') || base.startsWith('types_construct')) return 'oop';
  if (base.startsWith('types_collection') || base.startsWith('types_array') || base.startsWith('types_index')) return 'collections';
  if (base.startsWith('types_generics') || base.startsWith('types_traits') || base.startsWith('types_comptime')) return 'advanced';
  if (rel.includes('benchmark')) return 'benchmark';
  return 'syntax-types';
}

function walkTests(dir, out = []) {
  if (!fs.existsSync(dir)) return out;
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) walkTests(full, out);
    else if (name.endsWith('.far')) out.push(full);
  }
  return out.sort();
}

function runOne(far, file, clang) {
  const rel = path.relative(root, file).replace(/\\/g, '/');
  const env = { ...process.env };
  if (clang) env.FAR_CLANG = clang;
  const r = spawnSync(far, ['run', file], { encoding: 'utf8', cwd: root, timeout: 120000, env });
  const stderr = (r.stderr || '').trim();
  const stdout = (r.stdout || '').trim();
  const errLine = stderr.split(/\r?\n/).find((l) => /error/i.test(l)) || stderr.split(/\r?\n/)[0] || '';
  return {
    file: rel,
    exitCode: r.status ?? (r.error ? -1 : 0),
    passed: (r.status ?? 1) === 0,
    error: errLine.slice(0, 500),
    stdout: stdout.slice(0, 200),
    signal: r.signal || null,
  };
}

function main() {
  const far = resolveFar();
  const clang = resolveClang();
  // #region agent log
  appendLog({ hypothesisId: 'SETUP', location: 'debug-run-all-tests.mjs:main', message: 'harness_start', data: { far, clang, logPath } });
  // #endregion

  if (!far) {
    appendLog({ hypothesisId: 'SETUP', location: 'debug-run-all-tests.mjs:main', message: 'far_not_found', data: { candidates: FAR_CANDIDATES } });
    console.error('far.exe not found');
    process.exit(2);
  }

  const files = [
    path.join(root, 'program.far'),
    ...walkTests(path.join(root, 'tests')),
    ...walkTests(path.join(root, 'examples', 'benchmark')),
  ].filter((f, i, a) => a.indexOf(f) === i);

  const includeStress = process.env.FAR_STRESS === '1';
  if (includeStress) {
    const stress = path.join(root, 'tests', 'suite_10000.far');
    if (fs.existsSync(stress) && !files.includes(stress)) files.push(stress);
  }

  const results = { pass: 0, fail: 0, compile: 0, runtime: 0, timeout: 0 };
  const failures = [];
  /** @type {Record<string, { pass: number, fail: number }>} */
  const byCategory = {};

  for (const file of files) {
    const res = runOne(far, file, clang);
    const rel = res.file;
    const cat = categorize(rel);
    if (!byCategory[cat]) byCategory[cat] = { pass: 0, fail: 0 };
    const kind = res.signal === 'SIGTERM' ? 'timeout'
      : /error:/i.test(res.error) ? 'compile'
      : !res.passed ? 'runtime'
      : 'pass';

    if (kind === 'pass') {
      results.pass++;
      byCategory[cat].pass++;
    } else {
      results.fail++;
      byCategory[cat].fail++;
      if (kind === 'compile') results.compile++;
      else if (kind === 'timeout') results.timeout++;
      else results.runtime++;
      failures.push({ ...res, kind });
    }

    // #region agent log
    appendLog({
      hypothesisId: kind === 'pass' ? 'PASS' : kind === 'compile' ? 'D' : kind === 'runtime' ? 'E' : 'SETUP',
      location: 'debug-run-all-tests.mjs:runOne',
      message: kind === 'pass' ? 'test_pass' : 'test_fail',
      data: { file: res.file, kind, exitCode: res.exitCode, error: res.error, stdout: res.stdout },
    });
    // #endregion
  }

  // #region agent log
  appendLog({
    hypothesisId: 'SUMMARY',
    location: 'debug-run-all-tests.mjs:main',
    message: 'harness_complete',
    data: { total: files.length, ...results, byCategory, failureFiles: failures.map((f) => f.file) },
  });
  // #endregion

  console.log(`Far: ${far}`);
  console.log(`Clang: ${clang || '(not set — may fail on Windows)'}`);
  console.log(`Total: ${files.length}  Pass: ${results.pass}  Fail: ${results.fail}`);
  console.log(`  compile errors: ${results.compile}  runtime failures: ${results.runtime}  timeouts: ${results.timeout}`);
  console.log('\n--- By category ---');
  for (const [cat, c] of Object.entries(byCategory).sort((a, b) => a[0].localeCompare(b[0]))) {
    console.log(`  ${cat}: ${c.pass}/${c.pass + c.fail}`);
  }
  if (failures.length) {
    console.log('\n--- Failures ---');
    for (const f of failures) {
      console.log(`[${f.kind}] ${f.file}`);
      if (f.error) console.log(`  ${f.error}`);
    }
  }
  process.exit(results.fail > 0 ? 1 : 0);
}

main();
