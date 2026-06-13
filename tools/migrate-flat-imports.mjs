#!/usr/bin/env node
/**
 * Migrate from std.* imports to flat category modules.
 * Run: node tools/migrate-flat-imports.mjs
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const MODULE_MAP = {
  'std.bench': 'bench', 'std.cli': 'cli', 'std.compress': 'compress', 'std.crypto': 'crypto',
  'std.csv': 'csv', 'std.date': 'date', 'std.env': 'env', 'std.fs': 'fs', 'std.hash': 'hash',
  'std.i18n': 'i18n', 'std.json': 'json', 'std.log': 'log', 'std.math': 'math',
  'std.proc': 'proc', 'std.random': 'random', 'std.regex': 'regex', 'std.test': 'test',
  'std.time': 'time', 'std.xml': 'xml', 'std.yaml': 'yaml',
  'std.console': 'io', 'std.input': 'io', 'std.output': 'io', 'std.terminal': 'io',
  'std.debug': 'dev', 'std.deps': 'dev', 'std.docs': 'dev', 'std.format': 'dev',
  'std.hotreload': 'dev', 'std.immutable': 'dev', 'std.inference': 'dev',
  'std.lint': 'dev', 'std.live': 'dev', 'std.lsp': 'dev', 'std.nullable': 'dev',
  'std.pattern': 'dev', 'std.pkg': 'dev', 'std.profile': 'dev', 'std.readonly': 'dev',
  'std.repl': 'dev', 'std.shell': 'dev',
  'std.client': 'network', 'std.pack': 'network', 'std.grpc': 'network', 'std.http': 'network',
  'std.https': 'network', 'std.rest': 'network', 'std.rpc': 'network', 'std.serialize': 'network',
  'std.tcp': 'network', 'std.udp': 'network', 'std.websocket': 'network', 'std.net': 'network',
  'std.fft': 'science', 'std.ml': 'science', 'std.numerical': 'science',
  'std.optimization': 'science', 'std.physics': 'science', 'std.statistics': 'science',
  'std.boundscheck': 'security', 'std.concurrency': 'security', 'std.secure': 'security',
  'std.memsafe': 'security', 'std.overflow': 'security', 'std.permission': 'security',
  'std.sandbox': 'security',
  'std.incremental': 'perf', 'std.llvm': 'perf', 'std.lowmem': 'perf', 'std.native': 'perf',
  'std.predictable': 'perf', 'std.simd': 'perf', 'std.startup': 'perf', 'std.threads': 'perf',
  'std.vectorize': 'perf',
  'std.bounds': 'bounds', 'std.color': 'colors', 'std.color32': 'colors',
  'std.dmat2': 'matrices', 'std.dmat3': 'matrices', 'std.dmat4': 'matrices',
  'std.dpoint': 'points', 'std.dquat': 'quaternions', 'std.drect': 'rects',
  'std.dvec2': 'vectors', 'std.dvec3': 'vectors', 'std.dvec4': 'vectors',
  'std.point': 'points', 'std.rect': 'rects',
  'std.vec2': 'vectors', 'std.vec3': 'vectors', 'std.vec4': 'vectors',
  'std.ivec2': 'vectors', 'std.ivec3': 'vectors', 'std.ivec4': 'vectors',
  'std.mat2': 'matrices', 'std.mat3': 'matrices', 'std.mat4': 'matrices',
  'std.quat': 'quaternions', 'std.transform': 'transforms',
  'std.linalg': 'vectors', 'std.vectors': 'vectors', 'std.matrices': 'matrices',
};

function flattenPath(p) {
  if (MODULE_MAP[p]) return MODULE_MAP[p];
  if (p.startsWith('std.io.')) return 'io';
  if (p.startsWith('std.net.')) return 'network';
  if (p.startsWith('std.modern.')) return 'dev';
  if (p.startsWith('std.sci.')) return 'science';
  if (p.startsWith('std.sec.')) return 'security';
  if (p.startsWith('std.perf.')) return 'perf';
  if (p.startsWith('std.')) return MODULE_MAP[p] ?? p.slice(4).split('.').pop();
  return p;
}

function migrateSource(text) {
  let out = text;
  out = out.replace(/^from\s+(std\.[\w.]+)\s+import/gm, (_, mod) => `from ${flattenPath(mod)} import`);
  out = out.replace(/^import\s+(std\.[\w.]+)\s*(\{|$)/gm, (_, mod, rest) => `import ${flattenPath(mod)}${rest}`);
  out = out.replace(/^import\s+(std\.[\w.]+)\s*$/gm, (_, mod) => `import ${flattenPath(mod)}`);
  return out;
}

function walk(dir, out = []) {
  if (!fs.existsSync(dir)) return out;
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) walk(full, out);
    else if (name.endsWith('.far')) out.push(full);
  }
  return out;
}

const files = [
  path.join(root, 'program.far'),
  ...walk(path.join(root, 'examples')),
];

let changed = 0;
for (const file of files) {
  const before = fs.readFileSync(file, 'utf8');
  const after = migrateSource(before);
  if (after !== before) {
    fs.writeFileSync(file, after);
    changed++;
  }
}
console.log(`Migrated ${changed} .far files to flat imports`);
