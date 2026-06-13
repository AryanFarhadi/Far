#!/usr/bin/env node
/** Second-pass fixes for OOP migration edge cases */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const PREFIXES = [
  ['make_guard', 'Memsafe.make_guard'], ['is_valid', 'Memsafe.is_valid'], ['wipe', 'Memsafe.wipe'], ['guarded_size', 'Memsafe.guarded_size'],
  ['index_ok', 'Boundscheck.index_ok'], ['slice_ok', 'Boundscheck.slice_ok'], ['clamp_index', 'Boundscheck.clamp_index'],
  ['add_safe', 'Overflow.add_safe'], ['mul_safe', 'Overflow.mul_safe'], ['sub_safe', 'Overflow.sub_safe'], ['overflowed', 'Overflow.overflowed'],
  ['acquire', 'Concurrency.acquire'], ['release', 'Concurrency.release'], ['try_acquire', 'Concurrency.try_acquire'], ['is_owned', 'Concurrency.is_owned'],
  ['grant', 'Permission.grant'], ['revoke', 'Permission.revoke'], ['has_perm', 'Permission.has_perm'], ['bits', 'Permission.bits'],
  ['open_sandbox', 'Sandbox.open_sandbox'], ['close_sandbox', 'Sandbox.close_sandbox'], ['is_active', 'Sandbox.is_active'], ['allow_path', 'Sandbox.allow_path'], ['can_access', 'Sandbox.can_access'],
  ['digest', 'Crypto.digest'], ['encrypt', 'Crypto.encrypt'], ['secure_eq', 'Crypto.secure_eq'], ['token', 'Crypto.token'],
  ['target_triple', 'Native.target_triple'], ['compiled_native', 'Native.compiled_native'], ['version', 'Llvm.version'], ['opt_level', 'Llvm.opt_level'],
  ['width', 'Simd.width'], ['add4', 'Simd.add4'], ['enabled', 'Vectorize.enabled'], ['hint', 'Vectorize.hint'], ['dot4', 'Vectorize.dot4'],
  ['count', 'Threads.count'], ['current_id', 'Threads.current_id'],
  ['dot3', 'Vectors.dot3'], ['dot_components', 'Vectors.dot_components'], ['det2', 'Matrices.det2'], ['trace2', 'Matrices.trace2'],
  ['mean', 'Statistics.mean'], ['stddev', 'Statistics.stddev'], ['median', 'Statistics.median'], ['forward', 'Fft.forward'],
  ['grad_step', 'Optimization.grad_step'], ['parabola_vertex', 'Optimization.parabola_vertex'],
  ['sigmoid', 'Ml.sigmoid'], ['relu', 'Ml.relu'], ['dot_vecs', 'Ml.dot_vecs'],
  ['integrate_trapz', 'Numerical.integrate_trapz'], ['finite_diff', 'Numerical.finite_diff'],
  ['kinetic', 'Physics.kinetic'], ['potential', 'Physics.potential'], ['projectile_range', 'Physics.projectile_range'],
  ['flush_out', 'Console.flush_out'], ['flush_err', 'Console.flush_err'], ['write_err', 'Console.write_err'],
  ['day', 'Date.day'], ['month', 'Date.month'], ['year', 'Date.year'],
  ['fields', 'Inference.fields'], ['infer', 'Inference.infer'],
  ['net_connect', 'Net.connect'], ['net_close', 'Net.close'], ['net_send', 'Net.send'], ['net_recv', 'Net.recv'],
  ['fs_remove', 'Fs.remove'], ['read_text', 'Fs.read_text'], ['write_text', 'Fs.write_text'],
  ['yaml_get_value', 'Yaml.yaml_get_value'], ['tag', 'Xml.tag'],
  ['stringify_i64', 'Json.stringify_i64'],
];

const IMPORTS = {
  Memsafe: 'security', Boundscheck: 'security', Overflow: 'security', Concurrency: 'security',
  Permission: 'security', Sandbox: 'security', Crypto: 'crypto', Native: 'perf', Llvm: 'perf',
  Simd: 'perf', Vectorize: 'perf', Threads: 'perf', Vectors: 'vectors', Matrices: 'matrices',
  Statistics: 'science', Fft: 'science', Optimization: 'science', Ml: 'science', Numerical: 'science',
  Physics: 'science', Console: 'io', Date: 'date', Inference: 'modern', Net: 'net',
  Fs: 'fs', Yaml: 'yaml', Xml: 'xml', Json: 'json', Proc: 'proc',
};

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

function prefixCall(text, from, to) {
  const re = new RegExp(`(?<!${to.split('.')[0]}\\.)(?<![\\w.])(\\b${from})\\s*\\(`, 'g');
  return text.replace(re, `${to}(`);
}

function fixFile(file) {
  let text = fs.readFileSync(file, 'utf8');
  let orig = text;
  text = text.replace(/from std\.process import Proc/g, 'from proc import Proc');
  text = text.replace(/import std\.process/g, 'from proc import Proc');
  text = text.replace(/from std\.compress import Pack/g, 'from compress import Compress');
  text = text.replace(/from std\.crypto import Secure/g, 'from crypto import Crypto');
  const needed = new Set();
  for (const [fn, full] of PREFIXES) {
    if (new RegExp(`\\b${fn}\\s*\\(`).test(text)) needed.add(full.split('.')[0]);
    text = prefixCall(text, fn, full);
  }
  const lines = text.split('\n');
  for (const cls of needed) {
    const mod = IMPORTS[cls];
    if (!mod) continue;
    if (new RegExp(`from ${mod.replace('.', '\\.')} import ${cls}`).test(text)) continue;
    if (!new RegExp(`\\b${cls}\\.`).test(text)) continue;
    let at = 0;
    for (let i = 0; i < lines.length; i++) {
      if (/^(import|from|#)/.test(lines[i])) at = i + 1;
    }
    lines.splice(at, 0, `from ${mod} import ${cls}`);
  }
  text = lines.join('\n');
  if (text !== orig) fs.writeFileSync(file, text);
}

for (const f of [...walk(path.join(root, 'examples')), path.join(root, 'program.far')]) {
  if (fs.existsSync(f)) fixFile(f);
}
console.log('Second-pass migration done');
