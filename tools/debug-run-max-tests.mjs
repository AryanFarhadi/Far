#!/usr/bin/env node
/**
 * Maximum Far language test run:
 * - Auto-discovers every .far file with def main()
 * - Repeats each file N times (FAR_REPEAT or 5 when FAR_MAX=1)
 * - Extra repeats for stress suites (suite_10000, suite_max)
 * Logs to debug-b34052.log
 *
 * Usage:
 *   node tools/debug-run-max-tests.mjs
 *   FAR_MAX=1 node tools/debug-run-max-tests.mjs
 *   FAR_REPEAT=10 node tools/debug-run-max-tests.mjs
 */
import { spawnSync } from 'child_process';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const logPath = path.join(root, 'debug-b34052.log');
const SESSION = 'b34052';

const SKIP_FILES = new Set(['tempcoderunnerfile.far']);
const STRESS_FILES = new Set(['suite_10000.far', 'suite_max.far']);

function resolveClang() {
  const installJson = path.join(process.env.APPDATA || '', 'Far', 'install.json');
  if (fs.existsSync(installJson)) {
    try {
      const j = JSON.parse(fs.readFileSync(installJson, 'utf8'));
      if (j.clangExe && fs.existsSync(j.clangExe)) return j.clangExe;
    } catch { /* ignore */ }
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
  for (const p of [path.join(root, 'far.exe'), path.join(process.env.LOCALAPPDATA || '', 'Programs', 'Far', 'far.exe')]) {
    if (fs.existsSync(p)) return p;
  }
  return null;
}

function appendLog(entry) {
  fs.appendFileSync(logPath, JSON.stringify({ sessionId: SESSION, timestamp: Date.now(), ...entry }) + '\n');
}

function hasMain(file) {
  try {
    return /\bdef\s+main\s*\(/.test(fs.readFileSync(file, 'utf8'));
  } catch {
    return false;
  }
}

function walkFar(dir, out = []) {
  if (!fs.existsSync(dir)) return out;
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) walkFar(full, out);
    else if (name.endsWith('.far') && !SKIP_FILES.has(name.toLowerCase()) && hasMain(full)) {
      out.push(full);
    }
  }
  return out.sort();
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
  if (rel.includes('benchmark')) return 'benchmark';
  return 'syntax-types';
}

function runOne(far, file, clang) {
  const rel = path.relative(root, file).replace(/\\/g, '/');
  const env = { ...process.env };
  if (clang) env.FAR_CLANG = clang;
  const r = spawnSync(far, ['run', file], { encoding: 'utf8', cwd: root, timeout: 300000, env });
  const stderr = (r.stderr || '').trim();
  const stdout = (r.stdout || '').trim();
  const errLine = stderr.split(/\r?\n/).find((l) => /error/i.test(l)) || stderr.split(/\r?\n/)[0] || '';
  return {
    file: rel,
    exitCode: r.status ?? (r.error ? -1 : 0),
    passed: (r.status ?? 1) === 0,
    error: errLine.slice(0, 500),
    stdout: stdout.slice(0, 120),
    signal: r.signal || null,
  };
}

function main() {
  const far = resolveFar();
  const clang = resolveClang();
  const maxMode = process.env.FAR_MAX === '1' || process.env.FAR_MAX === 'true';
  const baseRepeat = parseInt(process.env.FAR_REPEAT || (maxMode ? '5' : '1'), 10);
  const stressRepeat = parseInt(process.env.FAR_STRESS_REPEAT || (maxMode ? '10' : '2'), 10);

  const files = [
    path.join(root, 'program.far'),
    ...walkFar(path.join(root, 'examples')),
  ].filter((f, i, a) => a.indexOf(f) === i && hasMain(f));

  appendLog({
    hypothesisId: 'SETUP',
    location: 'debug-run-max-tests.mjs:main',
    message: 'max_harness_start',
    data: { far, clang, files: files.length, baseRepeat, stressRepeat, maxMode },
  });

  if (!far) {
    console.error('far.exe not found');
    process.exit(2);
  }

  const results = { runs: 0, pass: 0, fail: 0, compile: 0, runtime: 0, timeout: 0 };
  const failures = [];
  const byCategory = {};

  for (const file of files) {
    const base = path.basename(file);
    const repeats = STRESS_FILES.has(base) ? stressRepeat : baseRepeat;
    const cat = categorize(path.relative(root, file).replace(/\\/g, '/'));
    if (!byCategory[cat]) byCategory[cat] = { runs: 0, pass: 0, fail: 0 };

    for (let attempt = 1; attempt <= repeats; attempt++) {
      results.runs++;
      byCategory[cat].runs++;
      const res = runOne(far, file, clang);
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
        failures.push({ ...res, kind, attempt, repeats });
      }

      appendLog({
        hypothesisId: kind === 'pass' ? 'PASS' : 'FAIL',
        location: 'debug-run-max-tests.mjs:runOne',
        message: kind === 'pass' ? 'test_pass' : 'test_fail',
        data: { file: res.file, kind, attempt, repeats, exitCode: res.exitCode, error: res.error, stdout: res.stdout },
      });
    }
  }

  appendLog({
    hypothesisId: 'SUMMARY',
    location: 'debug-run-max-tests.mjs:main',
    message: 'max_harness_complete',
    data: {
      uniqueFiles: files.length,
      ...results,
      baseRepeat,
      stressRepeat,
      byCategory,
      failureKeys: failures.map((f) => `${f.file}#${f.attempt}`),
    },
  });

  console.log(`Far: ${far}`);
  console.log(`Clang: ${clang || '(missing)'}`);
  console.log(`Mode: ${maxMode ? 'MAX' : 'normal'}  Files: ${files.length}  Total runs: ${results.runs}`);
  console.log(`  repeat: ${baseRepeat}x  stress-repeat: ${stressRepeat}x`);
  console.log(`Pass: ${results.pass}  Fail: ${results.fail}`);
  console.log(`  compile: ${results.compile}  runtime: ${results.runtime}  timeout: ${results.timeout}`);
  console.log('\n--- By category (runs) ---');
  for (const [cat, c] of Object.entries(byCategory).sort((a, b) => a[0].localeCompare(b[0]))) {
    console.log(`  ${cat}: ${c.pass}/${c.runs} passed`);
  }
  if (failures.length) {
    console.log('\n--- Failures ---');
    for (const f of failures) {
      console.log(`[${f.kind}] ${f.file} (try ${f.attempt}/${f.repeats})`);
      if (f.error) console.log(`  ${f.error}`);
      if (f.stdout) console.log(`  stdout: ${f.stdout}`);
    }
  }
  process.exit(results.fail > 0 ? 1 : 0);
}

main();
