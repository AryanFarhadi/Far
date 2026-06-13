#!/usr/bin/env node
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cppPath = path.join(root, 'src', 'far_stdlib_modules.cpp');
const builtinsPath = path.join(root, 'src', 'builtins.cpp');

const TYPE_MAP = { D: 'f64', I: 'i64', L: 'i64', B: 'bool', A: 'arr', V2: 'dvec2', R: 'drect', S: 'string' };
const PARAM_NAMES = ['x', 'y', 'z', 'w', 'v', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'];

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

function genMathModule(builtins) {
  const lines = ['public class math {'];
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
  return `static const char kStdlibMod_math[] = R"FAR_STDLIB(package far

module math

${lines.join('\n')}

export math
)FAR_STDLIB";`;
}

let cpp = fs.readFileSync(cppPath, 'utf8');
const builtins = parseBuiltins(fs.readFileSync(builtinsPath, 'utf8'));
const mathBlock = genMathModule(builtins);

if (/static const char kStdlibMod_math\[\]/.test(cpp)) {
  cpp = cpp.replace(/static const char kStdlibMod_math\[\][\s\S]*?\)FAR_STDLIB";\n?/, mathBlock + '\n\n');
} else {
  cpp = cpp.replace(
    /(static const char kStdlibMod_log\[\])/,
    mathBlock + '\n\n$1',
  );
  if (!cpp.includes('{"math", kStdlibMod_math}')) {
    cpp = cpp.replace(
      /(\{"log", kStdlibMod_log\},)/,
      '{"math", kStdlibMod_math},\n    $1',
    );
  }
}

fs.writeFileSync(cppPath, cpp);
console.log('Inserted kStdlibMod_math');
