#!/usr/bin/env node
/** Migrate tests/examples from free-function stdlib imports to Class.method syntax */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const MODULE_CLASS = {
  math: 'Math', hash: 'Hash', json: 'Json', fs: 'Fs', http: 'Http', console: 'Console',
  input: 'Input', output: 'Output', threads: 'Threads', compress: 'Compress', crypto: 'Crypto',
  csv: 'Csv', date: 'Date', env: 'Env', debug: 'Debug', deps: 'Deps', docs: 'Docs', format: 'Format',
  secure: 'Secure', memsafe: 'Memsafe', boundscheck: 'Boundscheck', overflow: 'Overflow',
  concurrency: 'Concurrency', permission: 'Permission', sandbox: 'Sandbox', vectors: 'Vectors',
  matrices: 'Matrices', statistics: 'Statistics', fft: 'Fft', optimization: 'Optimization', ml: 'Ml',
  numerical: 'Numerical', physics: 'Physics', native: 'Native', llvm: 'Llvm', simd: 'Simd',
  vectorize: 'Vectorize', incremental: 'Incremental', lowmem: 'Lowmem', predictable: 'Predictable',
  startup: 'Startup', terminal: 'Terminal', pack: 'Pack', grpc: 'Grpc', https: 'Https', rest: 'Rest',
  rpc: 'Rpc', serialize: 'Serialize', tcp: 'Tcp', udp: 'Udp', websocket: 'Websocket', hotreload: 'Hotreload',
  immutable: 'Immutable', inference: 'Inference', lint: 'Lint', live: 'Live', lsp: 'Lsp', nullable: 'Nullable',
  pattern: 'Pattern', pkg: 'Pkg', profile: 'Profile', readonly: 'Readonly', repl: 'Repl', shell: 'Shell',
  log: 'Log', path: 'Path', process: 'Proc', proc: 'Proc', random: 'Random', regex: 'Regex', sort: 'Sort',
  string: 'String', time: 'Time', url: 'Url', xml: 'Xml', yaml: 'Yaml', zip: 'Zip', args: 'Args',
  cli: 'Cli', bench: 'Bench', i18n: 'I18n', test: 'Test', linalg: 'Linalg', net: 'Net',
};

const FN_TO_CLASS = {
  clamp: 'Math', sin: 'Math', cos: 'Math', sqrt: 'Math', imin: 'Math', imax: 'Math', lerp: 'Math',
  stringify_i64: 'Json', stringify_str: 'Json', read_text: 'Fs', write_text: 'Fs', fs_remove: 'Fs',
  net_connect: 'Net', net_close: 'Net', tag: 'Xml', yaml_get_value: 'Yaml', count: 'Csv', field: 'Csv',
  glob_match: 'Regex', glob_find: 'Regex', rle_encode: 'Compress', rle_decode: 'Compress',
  xor_encrypt: 'Crypto', xor_decrypt: 'Crypto', fnv: 'Hash', crc32: 'Hash', pid: 'Proc',
  env_get_var: 'Env', env_set_var: 'Env', env_has_var: 'Env', argc: 'Cli', argv: 'Cli',
  translate: 'I18n', start: 'Bench', elapsed_ms: 'Bench', seed: 'Random', rand_between: 'Random',
  milliseconds: 'Time', seconds: 'Time', year: 'Date', month: 'Date', dot2: 'Linalg',
  assert_eq_i64: 'Test', thread_count: 'Threads', join: 'Threads', cores: 'Threads',
  write: 'Console', writeln: 'Console', input: 'Console', read_line: 'Console',
};

const MATH_GLOBALS = [
  'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'atan2', 'sinh', 'cosh', 'tanh', 'asinh', 'acosh', 'atanh',
  'sqrt', 'cbrt', 'hypot', 'pow', 'exp', 'log', 'log10', 'log2', 'exp2', 'log1p', 'expm1', 'floor', 'ceil',
  'round', 'trunc', 'fabs', 'fmod', 'copysign', 'pi', 'e', 'tau', 'phi', 'sqrt2', 'sqrt3', 'ln2', 'ln10',
  'deg_per_rad', 'rad_per_deg', 'imin', 'imax', 'imin3', 'imax3', 'iabs', 'isign', 'clamp_i', 'is_even',
  'is_odd', 'mod_pos', 'gcd', 'lcm', 'factorial', 'binomial', 'isqrt', 'ipow', 'sum_range', 'sum_range_inclusive',
  'product_range', 'fib', 'fib_iter', 'twice', 'thrice', 'quad', 'deg_to_rad', 'rad_to_deg', 'sin_deg', 'cos_deg',
  'tan_deg', 'asin_deg', 'acos_deg', 'atan_deg', 'atan2_deg', 'normalize_rad', 'normalize_deg', 'sec', 'csc',
  'cot', 'haversine', 'dmin', 'dmax', 'dmin3', 'dmax3', 'clamp_d', 'saturate', 'lerp', 'inv_lerp', 'remap',
  'square', 'cube', 'approx_eq', 'approx_zero', 'dist2', 'dist', 'sign_d', 'round_i', 'floor_i', 'ceil_i',
  'log_n', 'exp10', 'smoothstep', 'mean2', 'mean3', 'variance2', 'stddev2', 'clamp', 'deg', 'rad',
];

function modShort(pathStr) {
  const m = pathStr.match(/(?:std\.|core\.)([\w.]+)/);
  return m ? m[1].split('.').pop() : null;
}

function walk(dir, out = []) {
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) {
      if (name === 'node_modules' || name === '.git') continue;
      walk(full, out);
    } else if (name.endsWith('.far')) out.push(full);
  }
  return out;
}

function insertImport(lines, line) {
  if (lines.some((l) => l.trim() === line.trim())) return;
  let at = 0;
  for (let i = 0; i < lines.length; i++) {
    if (/^(import|from|package|module)\s/.test(lines[i])) at = i + 1;
    else if (lines[i].trim() && !lines[i].trim().startsWith('#')) break;
  }
  lines.splice(at, 0, line);
}

function migrateImports(text) {
  return text
    .replace(/^import\s+(std\.[\w.]+|core\.[\w.]+)(?:\s+as\s+\w+)?\s*\{([^}]+)\}/gm, (_, modPath) => {
      const cls = MODULE_CLASS[modShort(modPath)];
      return cls ? `from ${modPath.replace(/^core\./, 'std.')} import ${cls}` : _;
    })
    .replace(/^import\s+(std\.[\w.]+|core\.[\w.]+)\s*$/gm, (_, modPath) => {
      const cls = MODULE_CLASS[modShort(modPath)];
      return cls ? `from ${modPath.replace(/^core\./, 'std.')} import ${cls}` : _;
    })
    .replace(/^from\s+(std\.[\w.]+|core\.[\w.]+)\s+import\s+(.+)$/gm, (line, modPath, syms) => {
      const cls = MODULE_CLASS[modShort(modPath)];
      if (!cls) return line;
      const names = syms.split(',').map((s) => s.trim().split(/\s+/)[0]).filter(Boolean);
      if (names.length === 1 && names[0] === cls) return line;
      return `from ${modPath.replace(/^core\./, 'std.')} import ${cls}`;
    });
}

function prefixCalls(text, name, cls) {
  const re = new RegExp(`(?<!${cls}\\.)(?<![\\w.])(\\b${name})\\s*\\(`, 'g');
  return text.replace(re, `${cls}.$1(`);
}

function migrateSource(text) {
  let out = migrateImports(text);
  const needed = new Set();

  if (/Math\./.test(out) || MATH_GLOBALS.some((g) => new RegExp(`\\b${g}\\s*\\(`).test(out))) {
    needed.add('Math');
  }

  for (const g of MATH_GLOBALS) {
    out = prefixCalls(out, g, 'Math');
  }

  for (const [fn, cls] of Object.entries(FN_TO_CLASS)) {
    if (new RegExp(`\\b${fn}\\s*\\(`).test(out)) needed.add(cls);
    out = prefixCalls(out, fn, cls);
  }

  for (const [short, cls] of Object.entries(MODULE_CLASS)) {
    out = out.replace(new RegExp(`\\b${short}\\.(\\w+)\\s*\\(`, 'g'), `${cls}.$1(`);
  }

  out = out.replace(/\bThreads\.thread_count\s*\(/g, 'Threads.count(');
  out = prefixCalls(out, 'thread_count', 'Threads');
  out = prefixCalls(out, 'join', 'Threads');
  out = prefixCalls(out, 'cores', 'Threads');

  // Fix accidental rewrites in function declarations
  out = out.replace(/^(\s*(?:public\s+|constexpr\s+|consteval\s+|async\s+)?fun\s+)Math\.(\w+)/gm, '$1$2');
  out = out.replace(/^(\s*(?:public\s+|constexpr\s+|consteval\s+|async\s+)?fun\s+)([A-Z]\w+)\.(\w+)/gm, '$1$3');

  const lines = out.split('\n');
  for (const cls of needed) {
    const mod = Object.entries(MODULE_CLASS).find(([, c]) => c === cls)?.[0];
    if (!mod) continue;
    if (!new RegExp(`\\b${cls}\\.`).test(out)) continue;
    if (!new RegExp(`from std\\.${mod} import ${cls}`).test(out)) {
      insertImport(lines, `from std.${mod} import ${cls}`);
    }
  }
  return lines.join('\n');
}

const files = [...walk(path.join(root, 'examples')), path.join(root, 'program.far')].filter((p) => fs.existsSync(p));
let changed = 0;
for (const file of files) {
  const before = fs.readFileSync(file, 'utf8');
  const after = migrateSource(before);
  if (after !== before) {
    fs.writeFileSync(file, after);
    changed++;
  }
}
console.log('Migrated', changed, 'files');
