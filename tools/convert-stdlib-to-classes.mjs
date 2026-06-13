#!/usr/bin/env node
/** Repair stdlib modules broken by single-line-only conversion; re-convert with full bodies. */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cppPath = path.join(root, 'src', 'far_stdlib_modules.cpp');
const builtinsPath = path.join(root, 'src', 'builtins.cpp');

const TYPE_MAP = { D: 'f64', I: 'i64', L: 'i64', B: 'i64', A: 'i64', V2: 'dvec2', R: 'drect', S: 'string' };
const PARAM_NAMES = ['x', 'y', 'z', 'w', 'v', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'];

function facadeName(moduleName) {
  return moduleName.charAt(0).toUpperCase() + moduleName.slice(1);
}

function parseBuiltins(source) {
  const entries = [];
  const re = /^    B(\d)\("([^"]+)", "[^"]+", ([^,\)]+(?:, [^,\)]+)*)\),/gm;
  let m;
  while ((m = re.exec(source)) !== null) {
    const nargs = Number(m[1]);
    const name = m[2];
    const types = m[3].split(',').map((s) => s.trim());
    const ret = TYPE_MAP[types[0]] ?? 'i64';
    const argTypes = types.slice(1).map((t) => TYPE_MAP[t] ?? 'i64');
    entries.push({ name, ret, argTypes, nargs });
  }
  return entries;
}

function genMathClass(builtins) {
  const lines = ['public class Math {'];
  const seen = new Set();
  for (const e of builtins) {
    if (seen.has(e.name)) continue;
    seen.add(e.name);
    const params = e.argTypes.map((t, i) => `${PARAM_NAMES[i] ?? 'a' + i}: ${t}`).join(', ');
    const args = e.argTypes.map((_, i) => PARAM_NAMES[i] ?? 'a' + i).join(', ');
    const sig = e.nargs === 0 ? '' : params;
    const call = e.nargs === 0 ? `${e.name}()` : `${e.name}(${args})`;
    lines.push(`  public static fun ${e.name}(${sig}) -> ${e.ret} { return ${call} }`);
  }
  lines.push('  public static fun clamp(x: f64, lo: f64, hi: f64) -> f64 { return clamp_d(x, lo, hi) }');
  lines.push('  public static fun deg(v: f64) -> f64 { return rad_to_deg(v) }');
  lines.push('  public static fun rad(v: f64) -> f64 { return deg_to_rad(v) }');
  lines.push('}');
  return lines.join('\n');
}

function extractTopLevelItems(source) {
  const classes = [];
  const funs = [];
  const lines = source.split('\n');
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    if (/^public class \w+/.test(line)) {
      const block = [line];
      i++;
      let depth = (line.match(/\{/g) ?? []).length - (line.match(/\}/g) ?? []).length;
      while (i < lines.length && depth > 0) {
        block.push(lines[i]);
        depth += (lines[i].match(/\{/g) ?? []).length - (lines[i].match(/\}/g) ?? []).length;
        i++;
      }
      classes.push(block.join('\n'));
      continue;
    }
    if (/^public (fun|def) /.test(line)) {
      const block = [line.replace(/^public def /, 'public fun ')];
      i++;
      if (line.includes('{') && line.includes('}')) {
        funs.push(block[0].replace(/^public fun /, ''));
        continue;
      }
      let depth = (line.match(/\{/g) ?? []).length - (line.match(/\}/g) ?? []).length;
      while (i < lines.length && depth > 0) {
        block.push(lines[i]);
        depth += (lines[i].match(/\{/g) ?? []).length - (lines[i].match(/\}/g) ?? []).length;
        i++;
      }
      funs.push(block.join('\n').replace(/^public fun /, ''));
      continue;
    }
    i++;
  }
  return { classes, funs };
}

function funToStatic(lineOrBlock) {
  const text = lineOrBlock.trimEnd();
  if (/^static fun /.test(text)) return '  public ' + text;
  if (/^fun /.test(text)) return '  public static ' + text;
  return '  public static fun ' + text;
}

function convertModuleBody(body, mathClass) {
  const moduleMatch = body.match(/^module\s+(\S+)/m);
  if (!moduleMatch) return body;
  const mod = moduleMatch[1];

  if (mod === 'math') {
    const header = body.match(/^([\s\S]*?^module math\s*\n)/m)?.[1] ?? 'package std\n\nmodule math\n\n';
    return header + mathClass + '\n\nexport Math\n';
  }

  if (mod === 'client') {
    const headerMatch = body.match(/^([\s\S]*?^module client\s*\n)/m);
    const header = headerMatch?.[1] ?? '';
    const { classes } = extractTopLevelItems(body.slice(header.length));
    const names = classes.map((c) => c.match(/^public class (\w+)/)?.[1]).filter(Boolean);
    return header + classes.join('\n\n') + '\n\nexport ' + names.join(', ') + '\n';
  }

  const headerMatch = body.match(/^([\s\S]*?^module \S+\s*\n)/m);
  const header = headerMatch?.[1] ?? '';
  const rest = body.slice(header.length);
  const exportMatch = rest.match(/^export[\s\S]*$/m);
  const middle = exportMatch ? rest.slice(0, exportMatch.index) : rest;

  const { classes, funs } = extractTopLevelItems(middle.trimEnd());
  if (funs.length === 0) {
    if (classes.length === 0) return body;
    const names = classes.map((c) => c.match(/^public class (\w+)/)?.[1]).filter(Boolean);
    return header + classes.join('\n\n') + '\n\nexport ' + names.join(', ') + '\n';
  }

  const cls = facadeName(mod);
  const staticMethods = funs.map((f) => funToStatic(f)).join('\n\n');
  let extra = '';
  if (mod === 'threads') {
    extra =
      '\n\n  public static fun join(handle: i64) -> i64 { return join(handle) }\n' +
      '  public static fun cores() -> i64 { return cores() }';
  }
  const facade = `public class ${cls} {\n${staticMethods}${extra}\n}`;
  const exports = [cls, ...classes.map((c) => c.match(/^public class (\w+)/)?.[1]).filter(Boolean)];
  const mid = classes.length ? facade + '\n\n' + classes.join('\n\n') + '\n\n' : facade + '\n\n';
  return header + mid + 'export ' + [...new Set(exports)].join(', ') + '\n';
}

function splitBlocks(cpp) {
  const parts = cpp.split(/(static const char kStdlibMod_\w+\[\] = R"FAR_STDLIB\()/);
  const out = [];
  for (let i = 0; i < parts.length; i++) {
    if (parts[i].startsWith('static const char')) {
      const header = parts[i];
      const body = parts[i + 1] ?? '';
      const end = body.indexOf(')FAR_STDLIB"');
      out.push({ header, body: body.slice(0, end), footer: body.slice(end) });
      i++;
    } else if (parts[i]) {
      out.push({ preamble: parts[i] });
    }
  }
  return out;
}

function main() {
  const builtins = parseBuiltins(fs.readFileSync(builtinsPath, 'utf8'));
  const mathClass = genMathClass(builtins);
  let cpp = fs.readFileSync(cppPath, 'utf8');

  // Strip broken facade wrappers before re-converting: unwrap static class with truncated methods
  cpp = cpp.replace(/public class \w+ \{\n(?:  public static fun[^\n]*\{\n)+/g, (m) => '');
  // Remove empty broken class shells
  cpp = cpp.replace(/public class \w+ \{\n\n\}\n\n/g, '');

  const blocks = splitBlocks(cpp);
  let out = '';
  for (const block of blocks) {
    if (block.preamble) {
      out += block.preamble;
      continue;
    }
    out += block.header + convertModuleBody(block.body, mathClass) + block.footer;
  }
  fs.writeFileSync(cppPath, out);
  console.log('Repaired and converted stdlib modules');
}

main();
