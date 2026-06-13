#!/usr/bin/env node
/**
 * Regenerate vscode/data/core-api.json from the embedded stdlib (far_stdlib_modules.cpp)
 * or from std .far sources when present. Run from repo root:
 *   node tools/generate-core-api.mjs
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const cppPath = path.join(root, 'src', 'far_stdlib_modules.cpp');
const stdDir = path.join(root, 'std');
const apiJson = path.join(root, 'vscode', 'data', 'core-api.json');
const aggregateJson = path.join(root, 'vscode', 'data', 'aggregate-types.json');
const aggregateCpp = path.join(root, 'src', 'aggregate.cpp');

const KEYWORDS = [
  'def', 'fn', 'import', 'export', 'package', 'module', 'if', 'else', 'while', 'for', 'return',
  'struct', 'class', 'true', 'false', 'and', 'or', 'not', 'match', 'spawn', 'parallel',
  'break', 'continue', 'unsafe', 'public', 'private', 'enum', 'union', 'interface', 'trait',
];

const PRIMITIVE_TYPES = [
  'i8', 'i16', 'i32', 'i64', 'i128', 'u8', 'u16', 'u32', 'u64', 'u128',
  'f32', 'f64', 'bool', 'char', 'string', 'void', 'ptr',
];

const TYPE_ALIASES = [
  { name: 'int', mapsTo: 'i32' },
  { name: 'long', mapsTo: 'i64' },
  { name: 'short', mapsTo: 'i16' },
  { name: 'sbyte', mapsTo: 'i8' },
  { name: 'byte', mapsTo: 'u8' },
  { name: 'ushort', mapsTo: 'u16' },
  { name: 'uint', mapsTo: 'u32' },
  { name: 'ulong', mapsTo: 'u64' },
  { name: 'longlong', mapsTo: 'i128' },
  { name: 'ulonglong', mapsTo: 'u128' },
  { name: 'int128', mapsTo: 'i128' },
  { name: 'uint128', mapsTo: 'u128' },
  { name: 'float', mapsTo: 'f32' },
  { name: 'double', mapsTo: 'f64' },
  { name: 'str', mapsTo: 'string' },
  { name: 'pointer', mapsTo: 'ptr' },
];

const GLOBAL_BUILTINS = [
  // IO → io-builtins.json; math → import math / math.sin; aggregates → constructors in core-api.
  { name: 'len', signature: 'def len(x) -> i64', detail: 'built-in' },
];

const SCALAR_NAMES = { F32: 'f32', F64: 'f64', I32: 'i32', U8: 'u8' };

/** Matches src/aggregate.cpp method tables. */
const METHODS_BY_KIND = {
  Vec: [
    ['length', 0, 'f64'],
    ['length2', 0, 'f64'],
    ['dot', 1, 'f64', 'self'],
    ['distance', 1, 'f64', 'self'],
    ['distance2', 1, 'f64', 'self'],
    ['normalize', 0, 'self'],
    ['min', 1, 'self'],
    ['max', 1, 'self'],
    ['clamp', 2, 'self'],
    ['approx_eq', 2, 'bool', 'self'],
    ['cross', 1, 'self', 'self', 3],
  ],
  IVec: [
    ['length', 0, 'f64'],
    ['length2', 0, 'i64'],
    ['dot', 1, 'i64', 'self'],
    ['min', 1, 'self'],
    ['max', 1, 'self'],
    ['cross', 1, 'self', 'self', 3],
  ],
  Point: [
    ['length', 0, 'f64'],
    ['length2', 0, 'f64'],
    ['dot', 1, 'f64', 'self'],
    ['distance', 1, 'f64', 'self'],
    ['distance2', 1, 'f64', 'self'],
    ['normalize', 0, 'self'],
    ['min', 1, 'self'],
    ['max', 1, 'self'],
    ['clamp', 2, 'self'],
    ['approx_eq', 2, 'bool', 'self'],
    ['distance_to', 1, 'f64', 'self'],
    ['translate', 1, 'self'],
  ],
  Rect: [
    ['width', 0, 'f64'],
    ['height', 0, 'f64'],
    ['center', 0, 'point'],
    ['contains', 1, 'bool', 'point'],
    ['intersects', 1, 'bool', 'self'],
    ['expand', 1, 'self'],
    ['area', 0, 'f64'],
  ],
  Mat: [
    ['transpose', 0, 'self'],
    ['determinant', 0, 'f64'],
    ['mul', 1, 'vec'],
  ],
  Quat: [
    ['length', 0, 'f64'],
    ['length2', 0, 'f64'],
    ['dot', 1, 'f64', 'self'],
    ['normalize', 0, 'self'],
    ['mul', 1, 'self'],
  ],
  Color: [
    ['min', 1, 'self'],
    ['max', 1, 'self'],
    ['clamp', 2, 'self'],
    ['approx_eq', 2, 'bool', 'self'],
  ],
  Color32: [['to_color', 0, 'color']],
  Bounds: [
    ['contains', 1, 'bool', 'fvec3'],
    ['intersects', 1, 'bool', 'self'],
    ['expand', 1, 'self'],
    ['center', 0, 'fvec3'],
    ['size', 0, 'fvec3'],
  ],
};

const VEC_BY_SCALAR_DIM = {
  f32: { 2: 'vec2', 3: 'vec3', 4: 'vec4' },
  f64: { 2: 'dvec2', 3: 'dvec3', 4: 'dvec4' },
  i32: { 2: 'ivec2', 3: 'ivec3', 4: 'ivec4' },
};

const POINT_BY_SCALAR = { f32: 'fpoint', f64: 'dpoint' };
const COLOR_BY_SCALAR = { f32: 'color', f64: 'color' };

function parseFieldList(raw) {
  return raw.split(',').map((s) => s.trim().replace(/^"|"$/g, '')).filter(Boolean);
}

function resolveMethodArg(argKind, type) {
  if (argKind === 'self') return type.name;
  if (argKind === 'point') return POINT_BY_SCALAR[type.scalar] ?? 'dpoint';
  if (argKind === 'vec') {
    const dim = type.fields.length === 4 && type.kind === 'Mat'
      ? (type.fields.length === 4 ? 2 : type.fields.length === 9 ? 3 : 4)
      : Math.round(Math.sqrt(type.fields.length));
    const matDim = type.name.includes('mat2') || type.name === 'dmat2' ? 2
      : type.name.includes('mat3') || type.name === 'dmat3' ? 3 : 4;
    return VEC_BY_SCALAR_DIM[type.scalar]?.[matDim] ?? 'vec2';
  }
  if (argKind === 'fvec3') return 'fvec3';
  if (argKind === 'color') return 'color';
  return argKind;
}

function resolveMethodReturn(retKind, type) {
  if (retKind === 'self') return type.name;
  if (retKind === 'point') return POINT_BY_SCALAR[type.scalar] ?? 'dpoint';
  if (retKind === 'fvec3') return 'fvec3';
  if (retKind === 'color') return 'color';
  return retKind;
}

function buildMethodSignature(type, method) {
  const [name, argc, retKind, argKind, minDim] = method;
  if (minDim && type.fields.length < minDim) return null;
  const ret = resolveMethodReturn(retKind, type);
  const params = [];
  for (let i = 0; i < argc; i++) {
    const argType = resolveMethodArg(argKind ?? 'self', type);
    params.push(`${i === 0 ? 'other' : i === 1 ? 'hi' : 'arg'}: ${argType}`);
  }
  if (name === 'clamp') {
    params[0] = 'lo: ' + type.name;
    params[1] = 'hi: ' + type.name;
  }
  if (name === 'approx_eq') {
    params[0] = 'other: ' + type.name;
    params[1] = 'eps: f64';
  }
  const paramStr = params.join(', ');
  return `def ${name}(${paramStr}) -> ${ret}`;
}

function parseAggregatesFromCpp(cppPath) {
  const cpp = fs.readFileSync(cppPath, 'utf8');
  const aggRe = /\{FarTypeId::\w+,\s*"(\w+)",\s*"[^"]+",\s*AggregateKind::(\w+),\s*FarTypeId::(\w+),\s*(\d+),\s*\{([^}]*)\}/g;
  const ctorRe = /\{"(\w+)",\s*FarTypeId::(\w+),\s*(\d+)\}/g;

  const idToName = new Map();
  /** @type {Map<string, { name: string, kind: string, scalar: string, fields: string[], aliases: string[] }>} */
  const byName = new Map();

  let m;
  while ((m = aggRe.exec(cpp)) !== null) {
    const [, name, kind, scalarId, , fieldsRaw] = m;
    const fields = parseFieldList(fieldsRaw);
    const scalar = SCALAR_NAMES[scalarId] ?? 'f64';
    const entry = { name, kind, scalar, fields, aliases: [] };
    byName.set(name, entry);
    idToName.set(name.toUpperCase(), name);
  }

  // Map FarTypeId enum names to canonical type names from aggregates
  const enumToName = new Map();
  const enumRe = /\{FarTypeId::(\w+),\s*"(\w+)"/g;
  while ((m = enumRe.exec(cpp)) !== null) {
    enumToName.set(m[1], m[2]);
  }

  const constructors = [];
  while ((m = ctorRe.exec(cpp)) !== null) {
    const [, ctorName, typeId, arity] = m;
    const typeName = enumToName.get(typeId);
    if (!typeName || !byName.has(typeName)) continue;
    const type = byName.get(typeName);
    const params = type.fields.slice(0, Number(arity))
      .map((f) => `${f}: ${type.scalar}`)
      .join(', ');
    constructors.push({
      name: ctorName,
      type: typeName,
      signature: `def ${ctorName}(${params}) -> ${typeName}`,
      detail: 'builtin constructor',
    });
    if (ctorName !== typeName && !type.aliases.includes(ctorName)) {
      type.aliases.push(ctorName);
    }
  }

  const types = [];
  for (const type of byName.values()) {
    const members = type.fields.map((f) => ({
      name: f,
      kind: 'field',
      type: type.scalar,
      signature: `${f}: ${type.scalar}`,
    }));
    types.push({
      name: type.name,
      kind: type.kind,
      scalar: type.scalar,
      aliases: type.aliases,
      members,
    });
  }

  types.sort((a, b) => a.name.localeCompare(b.name));
  constructors.sort((a, b) => a.name.localeCompare(b.name));
  return { types, constructors };
}

function aliasPaths(primary) {
  return [primary];
}

function parseModuleMeta(source, moduleId) {
  const symbols = [];
  const pkg = source.match(/^package\s+(\S+)/m)?.[1] ?? 'std';
  const mod = source.match(/^module\s+(\S+)/m)?.[1] ?? moduleId.split('.').pop();
  const defRe = /^public\s+def\s+(\w+)\s*(\([^)]*\))?\s*(?:->\s*(\S+))?/gm;
  let m;
  while ((m = defRe.exec(source)) !== null) {
    const sig = `def ${m[1]}${m[2] ?? '()'} -> ${m[3] ?? 'void'}`;
    symbols.push({
      name: m[1],
      signature: sig,
      detail: `${moduleId}.${m[1]}`,
    });
  }
  const exportMatch = source.match(/^export\s+(.+)$/m);
  if (exportMatch) {
    for (const name of exportMatch[1].split(',').map((s) => s.trim()).filter(Boolean)) {
      if (!symbols.some((s) => s.name === name)) {
        symbols.push({ name, signature: name, detail: `${moduleId}.${name}` });
      }
    }
  }
  const staticRe = /public static fun (\w+)\(([^)]*)\)\s*(?:->\s*(\S+))?/g;
  while ((m = staticRe.exec(source)) !== null) {
    if (symbols.some((s) => s.name === m[1])) continue;
    symbols.push({
      name: m[1],
      signature: `static ${m[1]}(${m[2]}) -> ${m[3] ?? 'void'}`,
      detail: `${moduleId}.${m[1]}`,
    });
  }
  return { id: moduleId, package: pkg, module: mod, symbols };
}

function extractClassBlocks(source) {
  const classes = [];
  let i = 0;
  while (i < source.length) {
    const start = source.indexOf('public class ', i);
    if (start === -1) break;
    const nameMatch = source.slice(start).match(/^public class (\w+) \{/);
    if (!nameMatch) {
      i = start + 1;
      continue;
    }
    const name = nameMatch[1];
    let depth = 0;
    let j = start + nameMatch[0].length - 1;
    for (; j < source.length; j++) {
      if (source[j] === '{') depth++;
      else if (source[j] === '}') {
        depth--;
        if (depth === 0) {
          classes.push({ name, body: source.slice(start, j + 1) });
          i = j + 1;
          break;
        }
      }
    }
    if (depth !== 0) break;
  }
  return classes;
}

function parseStdlibClasses(source, moduleId) {
  const out = {};
  for (const cls of extractClassBlocks(source)) {
    const members = [];
    let constructor = null;
    const lines = cls.body.split('\n');
    for (const line of lines) {
      const st = line.match(/^\s*public static fun (\w+)\(([^)]*)\)\s*(?:->\s*([\w.<>\[\]|&,]+))?/);
      if (st) {
        members.push({
          name: st[1],
          kind: 'method',
          signature: `static ${st[1]}(${st[2]}) -> ${st[3] ?? 'void'}`,
        });
        continue;
      }
      const im = line.match(/^\s*public fun (\w+)\(([^)]*)\)\s*(?:->\s*([\w.<>\[\]|&,]+))?/);
      if (im) {
        const sig = `fun ${im[1]}(${im[2]})${im[3] ? ` -> ${im[3]}` : ''}`;
        if (im[1] === cls.name) {
          constructor = { signature: sig };
        } else {
          members.push({ name: im[1], kind: 'method', signature: sig });
        }
        continue;
      }
      const ctor = line.match(/^\s*public (\w+)\(([^)]*)\)/);
      if (ctor && ctor[1] === cls.name) {
        constructor = { signature: `${cls.name}(${ctor[2]})` };
        continue;
      }
      const field = line.match(/^\s*public (\w+)\s*:\s*([\w.<>\[\]|&,]+)/);
      if (field) {
        members.push({
          name: field[1],
          kind: 'field',
          signature: `${field[1]}: ${field[2]}`,
          type: field[2],
        });
      }
    }
    out[cls.name] = {
      module: moduleId,
      members,
      ...(constructor ? { constructor } : {}),
    };
  }
  return out;
}

function parseFromCpp(cppFile) {
  const cpp = fs.readFileSync(cppFile, 'utf8');
  const blocks = new Map();
  const blockRe = /static const char (kStdlibMod_[^\[]+)\[\] = R"FAR_STDLIB\(([\s\S]*?)\)FAR_STDLIB";/g;
  let m;
  while ((m = blockRe.exec(cpp)) !== null) {
    blocks.set(m[1], m[2]);
  }

  const modules = {};
  const stdlibClasses = {};
  const importPaths = new Set();
  const mapRe = /\{"([^"]+)",\s*(kStdlibMod_[^}]+)\}/g;
  while ((m = mapRe.exec(cpp)) !== null) {
    const primary = m[1];
    const varName = m[2].trim();
    const source = blocks.get(varName);
    if (!source) continue;
    modules[primary] = parseModuleMeta(source, primary);
    Object.assign(stdlibClasses, parseStdlibClasses(source, primary));
    for (const p of aliasPaths(primary)) importPaths.add(p);
  }
  return { modules, importPaths, stdlibClasses };
}

function walk(dir, out = []) {
  if (!fs.existsSync(dir)) return out;
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) walk(full, out);
    else if (name.endsWith('.far')) out.push(full);
  }
  return out.sort();
}

function primaryImportPath(rel) {
  const base = rel.replace(/\\/g, '/').replace(/\.far$/, '');
  const parts = base.split('/');
  if (parts.length === 1) return `std.${parts[0]}`;
  return `std.${parts[parts.length - 1]}`;
}

function parseFromStdDir() {
  const files = walk(stdDir);
  const modules = {};
  const stdlibClasses = {};
  const importPaths = new Set();
  for (const file of files) {
    const rel = path.relative(stdDir, file);
    const primary = primaryImportPath(rel);
    const source = fs.readFileSync(file, 'utf8');
    modules[primary] = parseModuleMeta(source, primary);
    Object.assign(stdlibClasses, parseStdlibClasses(source, primary));
    for (const p of aliasPaths(primary)) importPaths.add(p);
  }
  return { modules, importPaths, stdlibClasses };
}

function main() {
  let modules = {};
  let importPaths = new Set();
  let stdlibClasses = {};
  let source = 'unknown';

  if (fs.existsSync(stdDir) && walk(stdDir).length > 0) {
    ({ modules, importPaths, stdlibClasses } = parseFromStdDir());
    source = 'std/**/*.far';
  } else if (fs.existsSync(cppPath)) {
    ({ modules, importPaths, stdlibClasses } = parseFromCpp(cppPath));
    source = 'src/far_stdlib_modules.cpp';
  } else {
    console.error('No stdlib source found.');
    process.exit(1);
  }

  const modCount = Object.keys(modules).length;
  let symCount = 0;
  for (const mod of Object.values(modules)) symCount += mod.symbols?.length ?? 0;

  let aggregateTypes = { types: [], constructors: [] };
  if (fs.existsSync(aggregateCpp)) {
    aggregateTypes = parseAggregatesFromCpp(aggregateCpp);
  }

  const aggregateTypeNames = [
    ...new Set([
      ...aggregateTypes.types.map((t) => t.name),
      ...aggregateTypes.types.flatMap((t) => t.aliases ?? []),
    ]),
  ].sort();

  const api = {
    generatedAt: new Date().toISOString(),
    generatedFrom: source,
    keywords: KEYWORDS,
    types: [...PRIMITIVE_TYPES, ...aggregateTypeNames],
    typeAliases: TYPE_ALIASES,
    builtins: [
      ...GLOBAL_BUILTINS.map((b) => b.name),
      ...aggregateTypes.constructors.map((c) => c.name),
    ],
    globalBuiltins: [
      ...GLOBAL_BUILTINS,
      ...aggregateTypes.constructors.map((c) => ({
        name: c.name,
        signature: c.signature,
        detail: c.detail,
      })),
    ],
    importPaths: [...importPaths].sort(),
    modules,
    stdlibClasses,
    aggregateTypes,
  };

  fs.mkdirSync(path.dirname(apiJson), { recursive: true });
  fs.writeFileSync(apiJson, JSON.stringify(api, null, 2));
  fs.writeFileSync(path.join(path.dirname(apiJson), 'stdlib-classes.json'), JSON.stringify(stdlibClasses, null, 2));
  fs.writeFileSync(aggregateJson, JSON.stringify({
    generatedAt: api.generatedAt,
    generatedFrom: 'src/aggregate.cpp',
    ...aggregateTypes,
  }, null, 2));
  console.log(`Core API: ${modCount} modules, ${symCount} symbols -> vscode/data/core-api.json`);
  console.log(`Aggregate types: ${aggregateTypes.types.length} types, ${aggregateTypes.constructors.length} constructors -> vscode/data/aggregate-types.json`);
}

main();
