#!/usr/bin/env node
/** Update nested stdlib import paths to flat std.* names in source and vscode data. */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const REPLACEMENTS = [
  ['std.sec.crypto', 'std.secure'],
  ['std.net.compress', 'std.pack'],
  ['std.io.input', 'std.input'],
  ['std.io.output', 'std.output'],
  ['std.io.terminal', 'std.terminal'],
  ['std.net.client', 'std.client'],
  ['std.net.grpc', 'std.grpc'],
  ['std.net.http', 'std.http'],
  ['std.net.https', 'std.https'],
  ['std.net.rest', 'std.rest'],
  ['std.net.rpc', 'std.rpc'],
  ['std.net.serialize', 'std.serialize'],
  ['std.net.tcp', 'std.tcp'],
  ['std.net.udp', 'std.udp'],
  ['std.net.websocket', 'std.websocket'],
  ['std.modern.debug', 'std.debug'],
  ['std.modern.deps', 'std.deps'],
  ['std.modern.docs', 'std.docs'],
  ['std.modern.format', 'std.format'],
  ['std.modern.hotreload', 'std.hotreload'],
  ['std.modern.immutable', 'std.immutable'],
  ['std.modern.inference', 'std.inference'],
  ['std.modern.lint', 'std.lint'],
  ['std.modern.live', 'std.live'],
  ['std.modern.lsp', 'std.lsp'],
  ['std.modern.nullable', 'std.nullable'],
  ['std.modern.pattern', 'std.pattern'],
  ['std.modern.pkg', 'std.pkg'],
  ['std.modern.profile', 'std.profile'],
  ['std.modern.readonly', 'std.readonly'],
  ['std.modern.repl', 'std.repl'],
  ['std.modern.shell', 'std.shell'],
  ['std.perf.incremental', 'std.incremental'],
  ['std.perf.llvm', 'std.llvm'],
  ['std.perf.lowmem', 'std.lowmem'],
  ['std.perf.native', 'std.native'],
  ['std.perf.predictable', 'std.predictable'],
  ['std.perf.simd', 'std.simd'],
  ['std.perf.startup', 'std.startup'],
  ['std.perf.threads', 'std.threads'],
  ['std.perf.vectorize', 'std.vectorize'],
  ['std.sci.fft', 'std.fft'],
  ['std.sci.matrices', 'std.matrices'],
  ['std.sci.ml', 'std.ml'],
  ['std.sci.numerical', 'std.numerical'],
  ['std.sci.optimization', 'std.optimization'],
  ['std.sci.physics', 'std.physics'],
  ['std.sci.statistics', 'std.statistics'],
  ['std.sci.vectors', 'std.vectors'],
  ['std.sec.boundscheck', 'std.boundscheck'],
  ['std.sec.concurrency', 'std.concurrency'],
  ['std.sec.memsafe', 'std.memsafe'],
  ['std.sec.overflow', 'std.overflow'],
  ['std.sec.permission', 'std.permission'],
  ['std.sec.sandbox', 'std.sandbox'],
];

function applyReplacements(text) {
  let out = text;
  for (const [from, to] of REPLACEMENTS) {
    out = out.split(from).join(to);
  }
  return out;
}

function walk(dir, out = []) {
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) {
      if (name === 'node_modules' || name === '.git') continue;
      walk(full, out);
    } else if (/\.(far|js|json|md)$/.test(name)) {
      out.push(full);
    }
  }
  return out;
}

const targets = [
  ...walk(path.join(root, 'examples')),
  path.join(root, 'program.far'),
  path.join(root, 'vscode', 'test-intellisense.mjs'),
  path.join(root, 'vscode', 'data', 'stdlib-classes.json'),
].filter((p) => fs.existsSync(p));

let changed = 0;
for (const file of targets) {
  const before = fs.readFileSync(file, 'utf8');
  const after = applyReplacements(before);
  if (after !== before) {
    fs.writeFileSync(file, after);
    changed++;
    console.log('updated', path.relative(root, file));
  }
}
console.log(`Done. ${changed} files updated.`);
