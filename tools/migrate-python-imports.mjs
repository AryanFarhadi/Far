#!/usr/bin/env node
/**
 * Migrate to Python-style stdlib imports and lowercase facade types.
 * Run: node tools/migrate-python-imports.mjs
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const FUNCTION_MODULES = new Set([
  'bench', 'cli', 'compress', 'crypto', 'csv', 'date', 'env', 'fs', 'hash', 'i18n', 'json',
  'log', 'math', 'net', 'proc', 'random', 'regex', 'test', 'time', 'xml', 'yaml',
]);

const TYPE_MODULES = new Set([
  'vectors', 'matrices', 'points', 'rects', 'quaternions', 'colors', 'bounds', 'transforms',
  'network', 'io', 'science', 'security', 'perf', 'dev',
]);

const FACADE_TO_MODULE = {
  Math: 'math', Json: 'json', Hash: 'hash', Log: 'log', Fs: 'fs', Time: 'time', Date: 'date',
  Proc: 'proc', Random: 'random', Regex: 'regex', Csv: 'csv', Xml: 'xml', Yaml: 'yaml',
  Bench: 'bench', Cli: 'cli', Compress: 'compress', Crypto: 'crypto', Env: 'env', I18n: 'i18n',
  Test: 'test', Net: 'net',
  Threads: 'perf', Llvm: 'perf', Simd: 'perf', Native: 'perf', Startup: 'perf', Vectorize: 'perf',
  Incremental: 'perf', Lowmem: 'perf', Predictable: 'perf',
  Console: 'io', Input: 'io', Output: 'io', Terminal: 'io',
  Http: 'network', Https: 'network', Tcp: 'network', Udp: 'network', Rest: 'network',
  Rpc: 'network', Grpc: 'network', Client: 'network', TcpClient: 'network', UdpClient: 'network',
  HttpClient: 'network', HttpsClient: 'network', Websocket: 'network', Serialize: 'network',
  Pack: 'network',
  Fft: 'science', Ml: 'science', Numerical: 'science', Optimization: 'science', Physics: 'science',
  Statistics: 'science',
  Boundscheck: 'security', Concurrency: 'security', Secure: 'security', Memsafe: 'security',
  Overflow: 'security', Permission: 'security', Sandbox: 'security',
  Debug: 'dev', Deps: 'dev', Docs: 'dev', Format: 'dev', Hotreload: 'dev', Immutable: 'dev',
  Inference: 'dev', Lint: 'dev', Live: 'dev', Lsp: 'dev', Nullable: 'dev', Pattern: 'dev',
  Pkg: 'dev', Profile: 'dev', Readonly: 'dev', Repl: 'dev', Shell: 'dev',
};

const PASCAL_TO_LOWER = {
  Vec2: 'vec2', Vec3: 'vec3', Vec4: 'vec4',
  DVec2: 'dvec2', DVec3: 'dvec3', DVec4: 'dvec4',
  IVec2: 'ivec2', IVec3: 'ivec3', IVec4: 'ivec4',
  Mat2: 'mat2', Mat3: 'mat3', Mat4: 'mat4',
  DMat2: 'dmat2', DMat3: 'dmat3', DMat4: 'dmat4',
  Point: 'point', DPoint: 'dpoint', Rect: 'rect', DRect: 'drect',
  Quat: 'quat', DQuat: 'dquat', Color32: 'color32',
  Bounds: 'bounds', Transform: 'transform',
  Math: 'math', Json: 'json', Hash: 'hash', Log: 'log', Fs: 'fs', Time: 'time', Date: 'date',
  Proc: 'proc', Random: 'random', Regex: 'regex', Csv: 'csv', Xml: 'xml', Yaml: 'yaml',
  Bench: 'bench', Cli: 'cli', Compress: 'compress', Crypto: 'crypto', Env: 'env', I18n: 'i18n',
  Test: 'test', Net: 'net',
  Threads: 'threads', Llvm: 'llvm', Simd: 'simd', Native: 'native', Startup: 'startup',
  Vectorize: 'vectorize', Incremental: 'incremental', Lowmem: 'lowmem', Predictable: 'predictable',
  Console: 'console', Input: 'input', Output: 'output', Terminal: 'terminal',
  Http: 'http', Https: 'https', Tcp: 'tcp', Udp: 'udp', Rest: 'rest', Rpc: 'rpc', Grpc: 'grpc',
  Client: 'client', TcpClient: 'TcpClient', UdpClient: 'UdpClient', HttpClient: 'HttpClient',
  HttpsClient: 'HttpsClient', Websocket: 'websocket', Serialize: 'serialize', Pack: 'pack',
  Fft: 'fft', Ml: 'ml', Numerical: 'numerical', Optimization: 'optimization', Physics: 'physics',
  Statistics: 'statistics',
  Boundscheck: 'boundscheck', Concurrency: 'concurrency', Secure: 'secure', Memsafe: 'memsafe',
  Overflow: 'overflow', Permission: 'permission', Sandbox: 'sandbox',
  Debug: 'debug', Deps: 'deps', Docs: 'docs', Format: 'format', Hotreload: 'hotreload',
  Immutable: 'immutable', Inference: 'inference', Lint: 'lint', Live: 'live', Lsp: 'lsp',
  Nullable: 'nullable', Pattern: 'pattern', Pkg: 'pkg', Profile: 'profile', Readonly: 'readonly',
  Repl: 'repl', Shell: 'shell',
};

function walk(dir, out = []) {
  if (!fs.existsSync(dir)) return out;
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) walk(full, out);
    else if (name.endsWith('.far')) out.push(full);
  }
  return out;
}

function moduleForFromImport(path, symbols) {
  if (FUNCTION_MODULES.has(path)) return path;
  if (TYPE_MODULES.has(path) || path === 'modern') return path === 'modern' ? 'dev' : path;
  if (symbols.length === 1 && FACADE_TO_MODULE[symbols[0]]) return FACADE_TO_MODULE[symbols[0]];
  return path;
}

function migrateImports(text) {
  const needed = new Set();
  let out = text;

  out = out.replace(/^from\s+modern\s+import\s+.+$/gm, () => {
    needed.add('import dev');
    return '';
  });
  out = out.replace(/^from\s+(\w+)\s+import\s+(.+)$/gm, (_, mod, syms) => {
    const symbols = syms.split(',').map((s) => s.trim().split(/\s+/)[0]);
    const flat = mod === 'modern' ? 'dev' : mod;
    needed.add(`import ${flat}`);
    return '';
  });

  const lines = out.split('\n').filter((l) => l.trim() !== '');
  const importLines = [...needed].sort();
  let insertAt = 0;
  for (let i = 0; i < lines.length; i++) {
    if (/^(import|from|#|package|module)\s/.test(lines[i])) insertAt = i + 1;
    else if (lines[i].trim() && !lines[i].trim().startsWith('#')) break;
  }
  for (const imp of importLines) {
    if (!lines.some((l) => l.trim() === imp)) lines.splice(insertAt++, 0, imp);
  }
  out = lines.join('\n');

  out = out.replace(/^import\s+modern\s*$/gm, 'import dev');
  out = out.replace(/^import\s+modern\s+as\s+(\w+)\s*$/gm, 'import dev as $1');

  return out;
}

function migrateIdentifiers(text) {
  let out = text;
  const byLen = Object.keys(PASCAL_TO_LOWER).sort((a, b) => b.length - a.length);
  for (const old of byLen) {
    const neu = PASCAL_TO_LOWER[old];
    out = out.replace(new RegExp(`\\b${old}\\.`, 'g'), `${neu}.`);
  }
  return out;
}

function migrateFile(file) {
  const before = fs.readFileSync(file, 'utf8');
  let after = migrateImports(before);
  after = migrateIdentifiers(after);
  if (after !== before) {
    fs.writeFileSync(file, after);
    return true;
  }
  return false;
}

const files = [path.join(root, 'program.far'), ...walk(path.join(root, 'examples'))];
let changed = 0;
for (const f of files) {
  if (migrateFile(f)) changed++;
}
console.log(`Migrated ${changed} .far files to Python-style imports`);
