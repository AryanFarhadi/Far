'use strict';

const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

/** @typedef {{ name: string, kind: string, line: number, col: number, signature?: string, endLine?: number }} SymbolInfo */
/** @typedef {{ name: string, alias?: string }} ImportSymbolEntry */
/** @typedef {{ path: string, alias?: string, symbols: string[], symbolEntries?: ImportSymbolEntry[], fromStar?: boolean, line: number }} ImportInfo */

const KEYWORD_SET = new Set([
  'fun', 'fn', 'import', 'export', 'package', 'module', 'if', 'else', 'while', 'for',
  'return', 'struct', 'class', 'true', 'false', 'and', 'or',
  'not', 'match', 'spawn', 'parallel', 'break', 'continue', 'as', 'unsafe', 'public',
  'private', 'protected', 'internal', 'enum', 'union', 'interface', 'trait', 'impl',
  'extends', 'implements', 'get', 'set', 'property', 'operator', 'macro', 'comptime',
  'const', 'static', 'async', 'await', 'yield', 'defer', 'using', 'type', 'where',
]);

/** Keywords only meaningful at the start of a statement. */
const STMT_KEYWORDS = new Set([
  'fun', 'fn', 'import', 'export', 'package', 'module', 'if', 'else', 'while', 'for',
  'return', 'struct', 'class', 'match', 'spawn', 'parallel', 'break', 'continue',
  'unsafe', 'public', 'private', 'protected', 'internal', 'enum', 'union',
  'interface', 'trait', 'impl', 'extends', 'implements', 'const', 'static',
  'async', 'await', 'yield', 'defer', 'using', 'type', 'macro', 'comptime',
]);

/** Line starts a statement (top-level, block body, or after indent). */
const STMT_START_RE =
  /^(fun|fn|import|export|for|while|if|return|match|parallel|spawn|try|defer|break|continue|struct|class|enum|unsafe|else|elif)\b/;

/** Far syntax snippets — shown when typing statement keywords (including inside `{ ... }`). */
const SYNTAX_SNIPPETS = [
  {
    trigger: 'for',
    label: 'for i in 0..n { }',
    insertText: 'for ${1:i} in ${2:0..10} {\n\t$0\n}',
    detail: 'Range loop',
    doc: 'Iterate from start up to (not including) end: `for i in 0..10 { ... }`',
  },
  {
    trigger: 'for',
    label: 'for item in collection { }',
    insertText: 'for ${1:item} in ${2:collection} {\n\t$0\n}',
    detail: 'Collection loop',
    doc: 'Iterate array/list elements: `for age in ages { print(age) }`',
  },
  {
    trigger: 'parallel',
    label: 'parallel for i in 0..n { }',
    insertText: 'parallel for ${1:i} in ${2:0..10} {\n\t$0\n}',
    detail: 'Parallel range loop',
    doc: 'Run loop body in parallel over a range.',
  },
  {
    trigger: 'while',
    label: 'while (cond) { }',
    insertText: 'while (${1:cond}) {\n\t$0\n}',
    detail: 'While loop',
  },
  {
    trigger: 'if',
    label: 'if (cond) { }',
    insertText: 'if (${1:cond}) {\n\t$0\n}',
    detail: 'If statement',
  },
  {
    trigger: 'if',
    label: 'if (cond) { } else { }',
    insertText: 'if (${1:cond}) {\n\t$1\n} else {\n\t$0\n}',
    detail: 'If / else',
  },
  {
    trigger: 'fun',
    label: 'fun name() { }',
    insertText: 'fun ${1:name}(${2:}) {\n\t$0\n}',
    detail: 'Function',
  },
  {
    trigger: 'fn',
    label: 'fn name() { }',
    insertText: 'fn ${1:name}(${2:}) {\n\t$0\n}',
    detail: 'Function (fn)',
  },
  {
    trigger: 'import',
    label: 'import module',
    insertText: 'import ${1:module}',
    detail: 'Import module',
  },
  {
    trigger: 'import',
    label: 'import module { symbol }',
    insertText: 'import ${1:module} { ${2:symbol} }',
    detail: 'Selective import',
  },
  {
    trigger: 'return',
    label: 'return value',
    insertText: 'return ${1:0}',
    detail: 'Return from function',
  },
  {
    trigger: 'match',
    label: 'match expr { }',
    insertText: 'match ${1:expr} {\n\t$0\n}',
    detail: 'Pattern match',
  },
];

/** IO builtins — always available (matches compiler resolveIoCall). Loaded from data/io-builtins.json. */
const IO_BUILTINS_FALLBACK = [
  { name: 'print', signature: 'fun print(value) -> void', detail: 'built-in', doc: 'Print a value to stdout. No import needed.' },
  { name: 'input', signature: 'fun input() -> string', detail: 'built-in', doc: 'Read a line from stdin. No import needed.' },
  { name: 'input', signature: 'fun input(msg: string) -> string', detail: 'built-in', doc: 'Read a line with prompt. No import needed.' },
  { name: 'len', signature: 'fun len(x) -> i64', detail: 'built-in', doc: 'Length of string or collection.' },
];

let consoleBuiltinsCache = null;
let aggregateTypesCache = null;
let stdlibClassesCache = null;

function getStdlibClasses(api) {
  if (api?.stdlibClasses) return api.stdlibClasses;
  if (stdlibClassesCache) return stdlibClassesCache;
  try {
    const p = path.join(__dirname, 'data', 'stdlib-classes.json');
    stdlibClassesCache = JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch {
    stdlibClassesCache = {};
  }
  return stdlibClassesCache;
}

function getStdlibClassRecord(typeName) {
  return getStdlibClasses(null)[typeName] ?? null;
}

function getStdlibClassMembers(typeName) {
  const rec = getStdlibClassRecord(typeName);
  if (!rec) return [];
  const members = (rec.members ?? []).map((m) => ({
    name: m.name,
    kind: m.kind ?? 'method',
    signature: m.signature,
    doc: m.doc,
    line: 0,
  }));
  if (rec.constructor) {
    members.unshift({
      name: typeName,
      kind: 'constructor',
      signature: rec.constructor.signature,
      doc: rec.constructor.doc,
      line: 0,
    });
  }
  return members;
}

/** Matches src/far_stdlib_modules.cpp functionStdlibModules(). */
const FUNCTION_STDLIB_MODULES = new Set([
  'bench', 'cli', 'compress', 'crypto', 'csv', 'date', 'env', 'fs', 'hash', 'i18n', 'json',
  'log', 'math', 'net', 'proc', 'random', 'regex', 'test', 'time', 'xml', 'yaml',
]);

const GEOM_NAMESPACE_MODULES = new Set([
  'vectors', 'matrices', 'points', 'rects', 'quaternions', 'colors', 'bounds', 'transforms',
]);

/** io/console facade — use print() globally or import and call console.write(). */
const IO_FACADE_MODULES = new Set(['io', 'console']);

/** Stdlib facade classes — not global types; import io/console to use. */
const IO_FACADE_CLASS_NAMES = new Set(['console', 'input', 'output', 'terminal']);

function isGeomNamespaceModule(modPath) {
  return GEOM_NAMESPACE_MODULES.has(modPath);
}

function isIoFacadeModule(modPath) {
  return IO_FACADE_MODULES.has(modPath);
}

function isFunctionStdlibModule(modPath) {
  return FUNCTION_STDLIB_MODULES.has(modPath);
}

function isTypeStdlibModule(modPath) {
  return !!modPath && !isFunctionStdlibModule(modPath);
}

function parseReturnTypeFromSignature(sig) {
  const m = sig?.match(/->\s*([\w.<>\[\]|&,]+)/);
  return m ? m[1].trim() : null;
}

/** @param {string|undefined} collType */
function inferCollectionElemType(collType) {
  if (!collType) return 'i64';
  const bracket = collType.match(/^([\w.]+)\[\]$/);
  if (bracket) return bracket[1];
  const list = collType.match(/^List<([\w.]+)>$/);
  if (list) return list[1];
  if (collType === 'array' || collType === 'slice' || collType === 'Slice') return 'i64';
  return 'i64';
}

/** @param {string|undefined} collType */
function isCollectionTypeName(collType) {
  if (!collType) return false;
  return /\[\]$/.test(collType) || /^List</.test(collType)
    || ['array', 'slice', 'Slice', 'List'].includes(collType);
}

function getAggregateTypes(api) {
  if (api?.aggregateTypes?.types?.length) return api.aggregateTypes;
  if (aggregateTypesCache) return aggregateTypesCache;
  try {
    const p = path.join(__dirname, 'data', 'aggregate-types.json');
    aggregateTypesCache = JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch {
    aggregateTypesCache = { types: [], constructors: [] };
  }
  return aggregateTypesCache;
}

/** @type {Map<string, string>} alias -> canonical aggregate type name */
const aggregateAliasMap = new Map();

function refreshAggregateAliasMap(api) {
  aggregateAliasMap.clear();
  for (const t of getAggregateTypes(api).types ?? []) {
    aggregateAliasMap.set(t.name, t.name);
    for (const a of t.aliases ?? []) aggregateAliasMap.set(a, t.name);
  }
}

function resolveAggregateTypeName(typeName, api) {
  return aggregateAliasMap.get(typeName) ?? typeName;
}

/**
 * @param {string} typeName
 * @param {object|null} api
 */
function getAggregateTypeRecord(typeName, api) {
  const canonical = resolveAggregateTypeName(typeName, api);
  return getAggregateTypes(api).types?.find((t) => t.name === canonical) ?? null;
}

/**
 * @param {string} typeName
 * @param {object|null} api
 */
function getAggregateMembers(typeName, api) {
  const rec = getAggregateTypeRecord(typeName, api);
  if (!rec) return [];
  return (rec.members ?? [])
    .filter((m) => m.kind === 'field')
    .map((m) => ({
      name: m.name,
      kind: m.kind ?? 'field',
      type: m.type,
      signature: m.signature,
      line: 0,
    }));
}

/** Geometry facade (lowercase + legacy PascalCase) -> aggregate type name (matches src/geom_class.cpp). */
const GEOM_CLASS_TO_AGG = {
  vec2: 'fvec2', vec3: 'fvec3', vec4: 'fvec4',
  dvec2: 'dvec2', dvec3: 'dvec3', dvec4: 'dvec4',
  ivec2: 'ivec2', ivec3: 'ivec3', ivec4: 'ivec4',
  point: 'fpoint', dpoint: 'dpoint',
  rect: 'frect', drect: 'drect',
  mat2: 'mat2', mat3: 'mat3', mat4: 'mat4',
  dmat2: 'dmat2', dmat3: 'dmat3', dmat4: 'dmat4',
  quat: 'quat', dquat: 'dquat',
  color: 'color', color32: 'color32',
  transform: 'transform', bounds: 'bounds',
  Vec2: 'fvec2', Vec3: 'fvec3', Vec4: 'fvec4',
  DVec2: 'dvec2', DVec3: 'dvec3', DVec4: 'dvec4',
  IVec2: 'ivec2', IVec3: 'ivec3', IVec4: 'ivec4',
  Point: 'fpoint', DPoint: 'dpoint',
  Rect: 'frect', DRect: 'drect',
  Mat2: 'mat2', Mat3: 'mat3', Mat4: 'mat4',
  DMat2: 'dmat2', DMat3: 'dmat3', DMat4: 'dmat4',
  Quat: 'quat', DQuat: 'dquat',
  Color: 'color', Color32: 'color32',
  Transform: 'transform', Bounds: 'bounds',
};

const AGG_PUBLIC_NAME = {
  fvec2: 'vec2', fvec3: 'vec3', fvec4: 'vec4',
  fpoint: 'point', frect: 'rect',
};

function geomPublicType(aggName) {
  return AGG_PUBLIC_NAME[aggName] ?? aggName;
}

/** Instance method tables from aggregate.cpp — used for static facade completions. */
const GEOM_METHODS = {
  Vec: ['length', 'length2', 'dot', 'distance', 'distance2', 'normalize', 'min', 'max', 'clamp', 'approx_eq', 'cross'],
  IVec: ['length', 'length2', 'dot', 'min', 'max', 'cross'],
  Point: ['length', 'length2', 'dot', 'distance', 'distance2', 'normalize', 'min', 'max', 'clamp', 'approx_eq', 'distance_to', 'translate'],
  Rect: ['width', 'height', 'center', 'contains', 'intersects', 'expand', 'area'],
  Mat: ['transpose', 'determinant', 'mul'],
  Quat: ['length', 'length2', 'dot', 'normalize', 'mul'],
  Color32: ['to_color'],
  Bounds: ['contains', 'size', 'expand'],
};

const GEOM_SCALAR_METHODS = new Set([
  'length', 'length2', 'dot', 'distance', 'distance2', 'angle', 'determinant',
  'width', 'height', 'area', 'size', 'distance_to',
]);
const GEOM_BOOL_METHODS = new Set(['approx_eq', 'contains', 'intersects']);

function geomClassForAggregate(typeName, api) {
  const canonical = resolveAggregateTypeName(typeName, api);
  for (const [cls, agg] of Object.entries(GEOM_CLASS_TO_AGG)) {
    if (agg === canonical) return cls;
  }
  return null;
}

function geomStaticSignature(className, pubType, method, aggKind) {
  const t = pubType;
  const zeroArg = ['length', 'length2', 'normalize', 'determinant', 'transpose', 'width', 'height', 'center', 'area', 'size', 'to_color', 'angle'];
  const twoArg = ['clamp', 'approx_eq'];
  if (zeroArg.includes(method)) {
    const ret = GEOM_SCALAR_METHODS.has(method) ? 'f64' : (method === 'to_color' ? 'color' : t);
    return `static ${method}(v: ${t}) -> ${ret}`;
  }
  if (twoArg.includes(method)) {
    const ret = method === 'approx_eq' ? 'bool' : t;
    return `static ${method}(v: ${t}, ...) -> ${ret}`;
  }
  if (method === 'cross' && aggKind === 'IVec') return `static cross(a: ${t}, b: ${t}) -> ivec3`;
  if (method === 'cross') return `static cross(a: ${t}, b: ${t}) -> ${t}`;
  if (GEOM_BOOL_METHODS.has(method)) return `static ${method}(a: ${t}, b: ${t}) -> bool`;
  if (GEOM_SCALAR_METHODS.has(method)) return `static ${method}(a: ${t}, b: ${t}) -> f64`;
  if (method === 'mul' && aggKind === 'Mat') return `static mul(a: ${t}, b: ${t}) -> ${t}`;
  return `static ${method}(a: ${t}, b: ${t}) -> ${t}`;
}

function getGeomClassMembers(className, api) {
  const aggName = GEOM_CLASS_TO_AGG[className];
  if (!aggName) return [];
  const rec = getAggregateTypeRecord(aggName, api);
  if (!rec) return [];
  const pubType = geomPublicType(aggName);
  const methods = GEOM_METHODS[rec.kind] ?? [];
  return methods.map((name) => ({
    name,
    kind: 'method',
    signature: geomStaticSignature(className, pubType, name, rec.kind),
    line: 0,
  }));
}

function aggregateConstructorNameSet(api) {
  const names = new Set();
  for (const c of getAggregateTypes(api).constructors ?? []) names.add(c.name);
  return names;
}

function aggregateConstructorMap(api) {
  /** @type {Map<string, string>} */
  const map = new Map();
  for (const c of getAggregateTypes(api).constructors ?? []) {
    if (c.name && c.type) map.set(c.name, c.type);
  }
  return map;
}

/**
 * Resolve a constructor/callable name to the type it produces.
 * @param {string} calleeName
 * @param {object|null} api
 * @param {ImportInfo[]} [imports]
 */
function resolveImportedSymbolName(name, imports) {
  if (!imports?.length) return name;
  for (const imp of imports) {
    for (const entry of imp.symbolEntries ?? []) {
      if (entry.alias === name || entry.name === name) return entry.name;
    }
    if (imp.symbols.includes(name)) return name;
  }
  return name;
}

function resolveConstructorType(calleeName, api, imports) {
  const name = resolveImportedSymbolName(calleeName, imports);
  if (consoleBuiltinNames().has(name)) return null;
  if (IO_FACADE_CLASS_NAMES.has(name)) return null;
  const agg = aggregateConstructorMap(api).get(name);
  if (agg) return agg;
  if (getStdlibClassRecord(name)) return name;
  return null;
}

/**
 * @param {string} calleeName
 * @param {object|null} api
 * @param {ImportInfo[]} [imports]
 */
function resolveCallableReturnType(calleeName, api, imports) {
  const name = resolveImportedSymbolName(calleeName, imports);
  if (consoleBuiltinNames().has(name)) {
    for (const b of getConsoleBuiltins()) {
      if (b.name === name && b.signature) {
        const ret = parseReturnTypeFromSignature(b.signature);
        if (ret) return ret;
      }
    }
  }
  const ctorType = resolveConstructorType(calleeName, api, imports);
  if (ctorType) return ctorType;
  for (const b of getGlobalBuiltins(api)) {
    if (b.name === name && b.signature) {
      const ret = parseReturnTypeFromSignature(b.signature);
      if (ret) return ret;
    }
  }
  return null;
}

function isRegisteredCallable(registry, name) {
  if (consoleBuiltinNames().has(name)) return true;
  const lower = name.toLowerCase();
  if (registry.has(lower)) return true;
  for (const key of registry.keys()) {
    if (key.startsWith(`${lower}::`) || key.endsWith(`::${lower}`)) return true;
  }
  return false;
}

function attachParameterHintsCommand(item) {
  item.command = {
    command: 'editor.action.triggerParameterHints',
    title: 'Trigger Parameter Hints',
  };
  return item;
}

function getConsoleBuiltins() {
  if (consoleBuiltinsCache) return consoleBuiltinsCache;
  try {
    const p = path.join(__dirname, 'data', 'io-builtins.json');
    consoleBuiltinsCache = JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch {
    consoleBuiltinsCache = IO_BUILTINS_FALLBACK;
  }
  return consoleBuiltinsCache;
}

const TYPE_ALIASES_FALLBACK = [
  { name: 'int', mapsTo: 'i32' },
  { name: 'long', mapsTo: 'i64' },
  { name: 'short', mapsTo: 'i16' },
  { name: 'byte', mapsTo: 'u8' },
  { name: 'float', mapsTo: 'f32' },
  { name: 'double', mapsTo: 'f64' },
  { name: 'str', mapsTo: 'string' },
];

function getTypeAliases(api) {
  return api?.typeAliases?.length ? api.typeAliases : TYPE_ALIASES_FALLBACK;
}

function getGlobalBuiltins(api) {
  const ioNames = consoleBuiltinNames();
  const extra = api?.globalBuiltins?.length ? api.globalBuiltins : [];
  const seen = new Set();
  const out = [];
  for (const b of getConsoleBuiltins()) {
    const key = `${b.name}::${b.signature ?? ''}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(b);
  }
  for (const b of extra) {
    if (ioNames.has(b.name)) continue;
    const key = `${b.name}::${b.signature ?? ''}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(b);
  }
  return out;
}

function builtinRegistryKey(b) {
  return `${b.name}::${b.signature ?? ''}`.toLowerCase();
}

function consoleBuiltinNames() {
  return new Set(getConsoleBuiltins().map((b) => b.name));
}

function consoleBuiltinNameRegex() {
  return [...consoleBuiltinNames()].sort((a, b) => b.length - a.length).join('|');
}

function getAliasMap(api) {
  const map = new Map();
  for (const a of getTypeAliases(api)) map.set(a.name, a.mapsTo);
  return map;
}

/**
 * Every public stdlib symbol from core-api.json (auto-updated by generate-core-api.mjs).
 * @param {object|null} api
 */
function allStdlibCallables(api) {
  /** @type {{ name: string, signature?: string, detail?: string, modulePath: string, module: string }[]} */
  const out = [];
  if (!api?.modules) return out;
  for (const [modPath, mod] of Object.entries(api.modules)) {
    if (isFunctionStdlibModule(modPath) || isGeomNamespaceModule(modPath) || isIoFacadeModule(modPath)) continue;
    for (const s of mod.symbols ?? []) {
      out.push({
        name: s.name,
        signature: s.signature,
        detail: s.detail ?? mod.id,
        modulePath: modPath,
        module: mod.id ?? modPath,
      });
    }
  }
  return out;
}

/** Primitive and alias types (matches src/types.cpp). */
const PRIMITIVE_TYPES = [
  'i8', 'i16', 'i32', 'i64', 'i128', 'u8', 'u16', 'u32', 'u64', 'u128',
  'f32', 'f64', 'bool', 'char', 'string', 'void', 'byte', 'ptr',
];

/** Instance methods on primitive types (IntelliSense + planned Far string API). */
const PRIMITIVE_TYPE_MEMBERS = {
  string: [
    { name: 'trim', kind: 'method', signature: 'fun trim() -> string', doc: 'Remove leading and trailing whitespace.' },
    { name: 'tolower', kind: 'method', signature: 'fun tolower() -> string', doc: 'Return a lowercase copy.' },
    { name: 'toupper', kind: 'method', signature: 'fun toupper() -> string', doc: 'Return an uppercase copy.' },
    { name: 'split', kind: 'method', signature: 'fun split(sep: string) -> List<string>', doc: 'Split into parts by separator.' },
    { name: 'contains', kind: 'method', signature: 'fun contains(sub: string) -> i64', doc: '1 if substring is found.' },
    { name: 'starts_with', kind: 'method', signature: 'fun starts_with(prefix: string) -> i64', doc: '1 if string starts with prefix.' },
    { name: 'ends_with', kind: 'method', signature: 'fun ends_with(suffix: string) -> i64', doc: '1 if string ends with suffix.' },
  ],
  str: [
    { name: 'trim', kind: 'method', signature: 'fun trim() -> string', doc: 'Remove leading and trailing whitespace.' },
    { name: 'tolower', kind: 'method', signature: 'fun tolower() -> string', doc: 'Return a lowercase copy.' },
    { name: 'toupper', kind: 'method', signature: 'fun toupper() -> string', doc: 'Return an uppercase copy.' },
    { name: 'split', kind: 'method', signature: 'fun split(sep: string) -> List<string>', doc: 'Split into parts by separator.' },
    { name: 'contains', kind: 'method', signature: 'fun contains(sub: string) -> i64', doc: '1 if substring is found.' },
    { name: 'starts_with', kind: 'method', signature: 'fun starts_with(prefix: string) -> i64', doc: '1 if string starts with prefix.' },
    { name: 'ends_with', kind: 'method', signature: 'fun ends_with(suffix: string) -> i64', doc: '1 if string ends with suffix.' },
  ],
};

function getPrimitiveTypeMembers(typeName) {
  const key = resolveTypeName(typeName);
  return PRIMITIVE_TYPE_MEMBERS[key] ?? PRIMITIVE_TYPE_MEMBERS[typeName] ?? [];
}

const TYPE_ALIASES = TYPE_ALIASES_FALLBACK;
const ALIAS_TO_CANONICAL = new Map(TYPE_ALIASES.map((a) => [a.name, a.mapsTo]));

function refreshTypeAliasesFromApi(api) {
  const aliases = getTypeAliases(api);
  ALIAS_TO_CANONICAL.clear();
  for (const a of aliases) ALIAS_TO_CANONICAL.set(a.name, a.mapsTo);
}

/**
 * @param {string} linePrefix
 * @param {string} prefix word at cursor
 * @param {string} trigger snippet trigger keyword
 */
function matchesSyntaxSnippetContext(linePrefix, prefix, trigger) {
  const trimmed = linePrefix.trimStart();
  if (trimmed.length === 0) {
    return !prefix || trigger.startsWith(prefix);
  }
  const first = trimmed.split(/\s+/)[0] ?? '';
  if (first !== trigger && !trigger.startsWith(first)) return false;
  return !prefix || prefix === first || trigger.startsWith(prefix);
}

/**
 * @param {vscode.CompletionItem[]} items
 * @param {string} linePrefix
 * @param {string} prefix
 */
function pushSyntaxSnippets(items, linePrefix, prefix) {
  for (const snip of SYNTAX_SNIPPETS) {
    if (!matchesSyntaxSnippetContext(linePrefix, prefix, snip.trigger)) continue;
    const item = new vscode.CompletionItem(snip.label, vscode.CompletionItemKind.Snippet);
    item.insertText = new vscode.SnippetString(snip.insertText);
    item.detail = snip.detail;
    if (snip.doc) item.documentation = snip.doc;
    item.sortText = rankLabel(0, snip.label);
    item.filterText = `${snip.trigger} ${snip.label}`;
    items.push(item);
  }
}

/**
 * After `for var ` suggest `in collection { }`.
 * @param {vscode.CompletionItem[]} items
 * @param {string} linePrefix
 */
function pushForLoopContinuations(items, linePrefix) {
  const m = linePrefix.match(/^\s*for\s+(\w+)(?:\s+(\w*))?$/);
  if (!m) return;
  const rest = m[2] ?? '';
  if (rest && rest !== 'in' && !'in'.startsWith(rest)) return;
  if (rest === 'in') return;
  const item = new vscode.CompletionItem('in collection { }', vscode.CompletionItemKind.Snippet);
  item.insertText = new vscode.SnippetString('in ${1:collection} {\n\t$0\n}');
  item.detail = 'Continue for-in loop';
  item.documentation = 'Complete: `for item in ages { ... }`';
  item.sortText = rankLabel(0, 'in');
  item.filterText = 'in';
  items.push(item);
}

/**
 * Classify where completion was invoked.
 * @param {vscode.TextDocument} doc
 * @param {vscode.Position} position
 */
function getCompletionContext(doc, position) {
  const line = doc.lineAt(position.line).text;
  const linePrefix = line.slice(0, position.character);
  const trimmed = linePrefix.trimStart();

  const atStatementStart =
    trimmed.length === 0 ||
    /^(else|elif)\b/.test(trimmed) ||
    (/^(public|private|protected|internal|export|unsafe|async|static|const)\s+$/.test(trimmed)) ||
    STMT_START_RE.test(trimmed);

  const inStatementKeyword =
    /^\s*(for|while|if|return|match|parallel|fun|fn|import|export|try|defer|break|continue|struct|class|enum|unsafe)\b[\w]*$/i.test(
      linePrefix,
    );

  const inExpression =
    !atStatementStart &&
    !inStatementKeyword &&
    (/[=+\-*\/%,(\[<:!&|]\s*[\w.]*$/.test(linePrefix) ||
      new RegExp(`\\b(${consoleBuiltinNameRegex()})\\b[\\w.("'\\\`-]*$`).test(linePrefix) ||
      (/^\s+\S/.test(linePrefix) && !STMT_START_RE.test(trimmed)));

  const afterDot = /[\w.]+\.\w*$/.test(linePrefix);
  const inImport = /\bimport\s+[\w.{,\s]*$/.test(linePrefix);
  const inCallParens = /[\w.]+\s*\([^()]*$/.test(linePrefix);
  const inTypePosition =
    /->\s*[\w.]*$/.test(linePrefix) ||
    /(?:^|[\s(,])(?:public|private|protected|internal|static|async|const)\s+[\w.]*$/.test(linePrefix) ||
    /[\w.]+\s*:\s*[\w.]*$/.test(linePrefix) ||
    /[(,]\s*[\w.]+\s*:\s*[\w.]*$/.test(linePrefix);

  return { linePrefix, atStatementStart, inStatementKeyword, inExpression, afterDot, inImport, inTypePosition, inCallParens };
}

/**
 * True when the cursor is inside `{...}` of a $"..." interpolated string.
 * @param {vscode.TextDocument} doc
 * @param {vscode.Position} position
 * @returns {{ exprText: string, line: number } | null}
 */
function getInterpolationContext(doc, position) {
  const offset = doc.offsetAt(position);
  const textBefore = doc.getText(new vscode.Range(new vscode.Position(0, 0), position));

  let start = -1;
  for (let i = textBefore.length - 2; i >= 0; i--) {
    if (textBefore[i] === '$' && textBefore[i + 1] === '"') {
      start = i;
      break;
    }
  }
  if (start < 0) return null;

  let pos = start + 2;
  let inExpr = false;
  let exprDepth = 0;
  let exprStart = -1;

  while (pos < textBefore.length) {
    const ch = textBefore[pos];
    if (!inExpr) {
      if (ch === '\\') {
        pos += 2;
        continue;
      }
      if (ch === '{') {
        inExpr = true;
        exprDepth = 1;
        exprStart = pos + 1;
        pos++;
        continue;
      }
      if (ch === '"') return null;
      pos++;
      continue;
    }
    if (ch === '{') {
      exprDepth++;
      pos++;
      continue;
    }
    if (ch === '}') {
      exprDepth--;
      if (exprDepth === 0) {
        inExpr = false;
        exprStart = -1;
        pos++;
        continue;
      }
      pos++;
      continue;
    }
    pos++;
  }

  if (!inExpr || exprStart < 0) return null;
  return { exprText: textBefore.slice(exprStart), line: position.line + 1 };
}

/**
 * Completions inside $"text {expr}" — variables, members, builtins.
 * @param {vscode.TextDocument} doc
 * @param {vscode.Position} position
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {object|null} api
 * @param {{ exprText: string, line: number }} interpCtx
 * @param {Map<string, { item: vscode.CompletionItem, rank: number }>} registry
 * @param {vscode.CompletionItem[]} items
 */
function addInterpolationCompletions(doc, position, parsed, api, interpCtx, registry, items) {
  const { exprText, line } = interpCtx;
  const wordRange = doc.getWordRangeAtPosition(position, /[\w]+/);
  const replaceRange = wordRange ?? new vscode.Range(position, position);

  const applyRange = (item) => {
    item.range = replaceRange;
    return item;
  };

  const dotAccess = parseDotAccess(exprText);
  if (dotAccess) {
    const { qualifier, memberPrefix } = dotAccess;
    const lastPart = qualifier.split('.').pop() ?? qualifier;

    const modPath = resolveModulePath(qualifier, parsed, api);
    if (modPath) {
      const members = getModuleDotMembers(qualifier, memberPrefix, parsed, api);
      pushModuleDotCompletions(items, members, modPath);
      return;
    }
    if (parsed.importMap.has(qualifier)) return;

    const typeForMembers = resolveInstanceTypeName(lastPart, parsed, line, api);
    if (typeForMembers) {
      for (const m of getTypeMembers(parsed, typeForMembers, api)) {
        if (memberPrefix && !m.name.startsWith(memberPrefix)) continue;
        const kind = m.kind === 'field' || m.kind === 'property'
          ? vscode.CompletionItemKind.Field
          : vscode.CompletionItemKind.Method;
        const item = applyRange(new vscode.CompletionItem(m.name, kind));
        item.detail = `${typeForMembers}.${m.name} (${m.kind})`;
        if (m.signature) item.documentation = m.signature;
        if (m.kind === 'method' || m.kind === 'constructor') {
          item.insertText = m.signature
            ? snippetFromSignature(m.name, m.signature)
            : new vscode.SnippetString(`${m.name}($0)`);
        }
        item.sortText = rankLabel(0, m.name);
        items.push(item);
      }
    }
    return;
  }

  const prefix = exprText.match(/(\w*)$/)?.[1] ?? '';

  const seenVars = new Set();
  for (const v of parsed.variables) {
    if (v.line > line) continue;
    if (prefix && !v.name.startsWith(prefix)) continue;
    if (seenVars.has(v.name)) continue;
    seenVars.add(v.name);
    const item = applyRange(new vscode.CompletionItem(v.name, vscode.CompletionItemKind.Variable));
    item.detail = v.type ? `${v.kind}: ${v.type}` : v.kind;
    item.sortText = rankLabel(0, v.name);
    upsertCompletion(registry, v.name, item, 0, items);
  }

  for (const sym of parsed.symbols) {
    if (sym.kind !== 'function') continue;
    if (sym.line > line) continue;
    if (prefix && !sym.name.startsWith(prefix)) continue;
    const item = applyRange(new vscode.CompletionItem(sym.name, vscode.CompletionItemKind.Function));
    item.detail = sym.kind;
    if (sym.signature) item.documentation = sym.signature;
    item.sortText = rankLabel(1, sym.name);
    upsertCompletion(registry, sym.name, item, 1, items);
  }

  pushGroupedBuiltinCompletions(getConsoleBuiltins(), prefix, 0, registry, items);

  for (const lit of ['true', 'false']) {
    if (prefix && !lit.startsWith(prefix)) continue;
    const item = applyRange(new vscode.CompletionItem(lit, vscode.CompletionItemKind.Value));
    item.detail = 'literal';
    item.sortText = rankLabel(3, lit);
    upsertCompletion(registry, lit, item, 3, items);
  }
}

/**
 * Insert or replace a completion item by label (lower priority loses).
 * @param {Map<string, { item: vscode.CompletionItem, rank: number }>} registry
 * @param {string} key
 * @param {vscode.CompletionItem} item
 * @param {number} rank
 * @param {vscode.CompletionItem[]} items
 */
function upsertCompletion(registry, key, item, rank, items) {
  const k = key.toLowerCase();
  const prev = registry.get(k);
  if (prev && prev.rank <= rank) return;
  if (prev) {
    const idx = items.indexOf(prev.item);
    if (idx >= 0) items.splice(idx, 1);
  }
  registry.set(k, { item, rank });
  items.push(item);
}

function completionItemLabel(item) {
  if (typeof item.label === 'string') return item.label;
  return item.label?.label ?? '';
}

/** Drop duplicate labels (e.g. print from io-builtins vs core-api). Keeps first/highest-priority entry. */
function dedupeCompletionItems(items) {
  const seen = new Set();
  const out = [];
  for (const item of items) {
    const label = completionItemLabel(item);
    if (seen.has(label)) continue;
    seen.add(label);
    out.push(item);
  }
  return out;
}

function rankLabel(rank, name) {
  return `${String(rank).padStart(2, '0')}_${name}`;
}

function overloadCompletionLabel(name, signature) {
  if (!signature) return name;
  const m = signature.match(/\(([^)]*)\)/);
  if (!m) return name;
  const params = m[1].trim();
  if (!params) return `${name}()`;
  const first = params.split(',')[0].trim();
  const short = first.includes(':') ? first.split(':')[0].trim() : first;
  return `${name}(${short})`;
}

function makeBuiltinItem(b, prefix, rank = 1) {
  if (prefix && !b.name.startsWith(prefix)) return null;
  const label = overloadCompletionLabel(b.name, b.signature);
  const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Function);
  item.detail = b.detail ?? 'built-in';
  if (b.doc) {
    const md = new vscode.MarkdownString(b.doc);
    if (b.signature) md.appendCodeblock(b.signature, 'far');
    item.documentation = md;
  } else if (b.signature) {
    item.documentation = b.signature;
  }
  item.insertText = b.signature
    ? snippetFromSignature(b.name, b.signature)
    : new vscode.SnippetString(`${b.name}($0)`);
  item.filterText = b.name;
  item.sortText = rankLabel(rank, b.name);
  return item;
}

/**
 * One completion entry per builtin name; overloads appear in signature help inside ().
 * @param {string} name
 * @param {object[]} overloads
 * @param {number} rank
 */
function makeGroupedBuiltinItem(name, overloads, rank = 0) {
  const primary = overloads[0];
  const multi = overloads.length > 1;
  const label = multi ? `${name}()` : overloadCompletionLabel(name, primary.signature);
  const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Function);
  item.detail = multi
    ? `${primary.detail ?? 'built-in'} · ${overloads.length} overloads`
    : (primary.detail ?? 'built-in');
  if (multi) {
    let docText = 'Choose overload after `(` — use arrow keys in the signature popup.';
    for (const b of overloads) {
      if (b.doc) docText += `\n\n${b.doc}`;
    }
    const md = new vscode.MarkdownString(docText);
    for (const b of overloads) {
      if (b.signature) md.appendCodeblock(b.signature, 'far');
    }
    item.documentation = md;
    item.insertText = new vscode.SnippetString(`${name}($0)`);
    attachParameterHintsCommand(item);
  } else if (primary.doc) {
    const md = new vscode.MarkdownString(primary.doc);
    if (primary.signature) md.appendCodeblock(primary.signature, 'far');
    item.documentation = md;
    item.insertText = primary.signature
      ? snippetFromSignature(name, primary.signature)
      : new vscode.SnippetString(`${name}($0)`);
  } else {
    item.documentation = primary.signature;
    item.insertText = primary.signature
      ? snippetFromSignature(name, primary.signature)
      : new vscode.SnippetString(`${name}($0)`);
  }
  item.filterText = name;
  item.sortText = rankLabel(rank, name);
  if (multi || primary.signature?.includes('(')) attachParameterHintsCommand(item);
  return item;
}

/**
 * @param {object[]} builtins
 * @param {string} prefix
 * @param {number} rank
 * @param {Map<string, { item: vscode.CompletionItem, rank: number }>} registry
 * @param {vscode.CompletionItem[]} items
 */
function pushGroupedBuiltinCompletions(builtins, prefix, rank, registry, items) {
  /** @type {Map<string, object[]>} */
  const groups = new Map();
  for (const b of builtins) {
    if (prefix && !b.name.startsWith(prefix)) continue;
    if (!groups.has(b.name)) groups.set(b.name, []);
    groups.get(b.name).push(b);
  }
  for (const [name, overloads] of groups) {
    const item = makeGroupedBuiltinItem(name, overloads, rank);
    if (item) upsertCompletion(registry, `io::${name}`, item, rank, items);
  }
}

/**
 * Parse class/struct body for fields and methods.
 * @param {string[]} lines
 * @param {number} startIdx
 * @param {string} typeName
 */
function parseTypeBody(lines, startIdx, typeName) {
  /** @type {{ name: string, type?: string, kind: string, signature?: string, line: number }[]} */
  const members = [];
  const end = findBlockEnd(lines, startIdx);

  for (let i = startIdx + 1; i < end - 1; i++) {
    const line = lines[i];

    const prop = line.match(/^\s*(?:public|private|protected|internal)?\s*prop\s+(\w+)\s*:\s*([\w.<>\[\]|&,]+)/);
    if (prop) {
      members.push({
        name: prop[1],
        kind: 'property',
        type: prop[2],
        signature: `prop ${prop[1]}: ${prop[2]}`,
        line: i + 1,
      });
      continue;
    }

    const method = line.match(/^\s*(?:public|private|protected|internal)?\s*(?:async\s+)?fun\s+(\w+)\s*(\([^)]*\))?(?:\s*->\s*([\w.<>\[\]|&,]+))?/);
    if (method) {
      members.push({
        name: method[1],
        kind: 'method',
        signature: buildSignature('fun', method[1], method[2] || '()', method[3]),
        line: i + 1,
      });
      continue;
    }

    const field = line.match(/^\s*(?:public|private|protected|internal|static)?\s*(\w+)\s*:\s*([\w.<>\[\]|&,]+)/);
    if (field && !line.includes('prop ') && field[1] !== 'get' && field[1] !== 'set') {
      members.push({
        name: field[1],
        type: field[2],
        kind: 'field',
        signature: `${field[1]}: ${field[2]}`,
        line: i + 1,
      });
      continue;
    }

    const ctor = line.match(/^\s*(?:public|private|protected|internal)?\s*(\w+)\s*\(([^)]*)\)/);
    if (ctor && ctor[1] === typeName) {
      members.push({
        name: ctor[1],
        kind: 'constructor',
        signature: `${typeName}(${ctor[2]})`,
        line: i + 1,
      });
    }
  }

  return members;
}

/**
 * @param {string} text
 * @param {SymbolInfo[]} symbols
 */
function parseVariables(text, symbols, api = null, imports = []) {
  /** @type {{ name: string, type?: string, kind: string, line: number, scope?: string }[]} */
  const vars = [];
  const lines = text.split(/\r?\n/);
  const reserved = new Set(['if', 'else', 'for', 'while', 'return', 'fun', 'fn', 'class', 'struct', 'enum', 'import', 'from']);
  /** @type {Map<string, string>} */
  const knownTypes = new Map();

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    const arrAssign = line.match(/^\s*(\w+)\s*=\s*\[/);
    if (arrAssign && !reserved.has(arrAssign[1])) {
      const elem = inferArrayLiteralElemType(line);
      const collType = `${elem}[]`;
      vars.push({ name: arrAssign[1], type: collType, kind: 'variable', line: i + 1 });
      knownTypes.set(arrAssign[1], collType);
      continue;
    }

    const dictAssign = line.match(/^\s*(\w+)\s*=\s*\{/);
    if (dictAssign && !reserved.has(dictAssign[1])) {
      const { key, val } = inferDictLiteralTypes(line);
      const dictType = `Dict<${key}, ${val}>`;
      vars.push({ name: dictAssign[1], type: dictType, kind: 'variable', line: i + 1 });
      knownTypes.set(dictAssign[1], dictType);
      continue;
    }

    const forIn = line.match(/^\s*for\s+(\w+)\s+in\s+(\w+)\s*\{/);
    if (forIn && !reserved.has(forIn[1])) {
      const collType = knownTypes.get(forIn[2]);
      vars.push({
        name: forIn[1],
        type: inferCollectionElemType(collType),
        kind: 'loop variable',
        line: i + 1,
      });
      continue;
    }

    const typed = line.match(/^\s*(\w+)\s*:\s*([\w.<>\[\]|&,]+)\s*(?:=|$)/);
    if (typed && !reserved.has(typed[1])) {
      vars.push({ name: typed[1], type: typed[2], kind: 'variable', line: i + 1 });
      knownTypes.set(typed[1], typed[2]);
      continue;
    }

    const ctorAssign = line.match(/^\s*(\w+)\s*=\s*(\w+)\s*\(/);
    if (ctorAssign && !reserved.has(ctorAssign[1])) {
      const ctorType = resolveConstructorType(ctorAssign[2], api, imports);
      if (ctorType) {
        vars.push({ name: ctorAssign[1], type: ctorType, kind: 'variable', line: i + 1 });
        knownTypes.set(ctorAssign[1], ctorType);
        continue;
      }
    }

    const methodAssign = line.match(/^\s*(\w+)\s*=\s*(\w+)\.(\w+)\s*\(/);
    if (methodAssign && !reserved.has(methodAssign[1])) {
      const objType = knownTypes.get(methodAssign[2])
        || resolveInstanceTypeName(methodAssign[2], { variables: vars, symbols, typeMembers: new Map() }, i + 1, api);
      if (objType) {
        const member = getGeomClassMembers(objType, api).find((m) => m.name === methodAssign[3])
          || getStdlibClassMembers(objType).find((m) => m.name === methodAssign[3])
          || getTypeMembers({ typeMembers: new Map() }, objType, api).find((m) => m.name === methodAssign[3]);
        const ret = parseReturnTypeFromSignature(member?.signature);
        vars.push({ name: methodAssign[1], type: ret ?? undefined, kind: 'variable', line: i + 1 });
        if (ret) knownTypes.set(methodAssign[1], ret);
        continue;
      }
    }

    const callAssign = line.match(/^\s*(\w+)\s*=\s*(\w+)\s*\(/);
    if (callAssign && !reserved.has(callAssign[1])) {
      const retType = resolveCallableReturnType(callAssign[2], api, imports);
      vars.push({
        name: callAssign[1],
        type: retType ?? undefined,
        kind: 'variable',
        line: i + 1,
      });
      if (retType) knownTypes.set(callAssign[1], retType);
      continue;
    }

    const simpleAssign = line.match(/^\s*(\w+)\s*=\s*(?!:)/);
    if (simpleAssign && !reserved.has(simpleAssign[1])) {
      if (!line.match(new RegExp(`^\\s*${simpleAssign[1]}\\s*:`))) {
        vars.push({ name: simpleAssign[1], kind: 'variable', line: i + 1 });
      }
    }
  }

  for (const sym of symbols) {
    if (sym.kind !== 'function' || !sym.signature) continue;
    for (const p of parseParamsFromSignature(sym.signature)) {
      const pm = p.match(/^(\w+)\s*:\s*([\w.<>\[\]|&,]+)$/);
      if (pm) {
        vars.push({
          name: pm[1],
          type: pm[2],
          kind: 'parameter',
          line: sym.line,
          scope: sym.name,
        });
      } else {
        const bare = p.match(/^(\w+)$/);
        if (bare) {
          vars.push({ name: bare[1], kind: 'parameter', line: sym.line, scope: sym.name });
        }
      }
    }
  }

  return vars;
}

/** Infer element type from a simple `[1, 2, 3]` literal on one line. */
function inferArrayLiteralElemType(line) {
  const nums = line.match(/=\s*\[([^\]]*)\]/);
  if (nums && /["']/.test(nums[1])) return 'string';
  return 'i32';
}

/** Infer key/value types from a simple `{ "a": 1, "b": 2 }` literal on one line. */
function inferDictLiteralTypes(line) {
  const body = line.match(/=\s*\{([^}]*)\}/);
  if (!body) return { key: 'i32', val: 'i32' };
  const text = body[1];
  if (/["']/.test(text.split(':')[0] || '')) return { key: 'string', val: /["']/.test(text.split(':').slice(1).join(':')) ? 'string' : 'i32' };
  return { key: 'i32', val: 'i32' };
}

function resolveTypeName(typeName) {
  return ALIAS_TO_CANONICAL.get(typeName) || typeName;
}

/**
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {string} typeName
 */
function getTypeMembers(parsed, typeName, api = null) {
  const prim = getPrimitiveTypeMembers(typeName);
  if (prim.length) return prim;
  const stdlib = getStdlibClassMembers(typeName);
  if (stdlib.length) return stdlib;
  const geom = getGeomClassMembers(typeName, api);
  if (geom.length) return geom;
  const fields = getAggregateMembers(typeName, api);
  const geomCls = geomClassForAggregate(typeName, api);
  if (geomCls) return [...fields, ...getGeomClassMembers(geomCls, api)];
  const key = resolveTypeName(typeName);
  const direct = parsed.typeMembers.get(typeName) || parsed.typeMembers.get(key);
  if (direct) return direct;
  if (fields.length) return fields;
  return parsed.typeMembers.get(typeName) || [];
}

/**
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {string} varName
 * @param {number} beforeLine
 */
function inferVariableType(parsed, varName, beforeLine) {
  const hits = parsed.variables.filter((v) => v.name === varName && v.line <= beforeLine && v.type);
  if (hits.length) return hits[hits.length - 1].type;
  return null;
}

/**
 * Resolve the type name for member access on a qualifier's last segment.
 * @param {string} lastPart
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {number} beforeLine
 * @param {object|null} api
 */
function resolveInstanceTypeName(lastPart, parsed, beforeLine, api) {
  const varType = inferVariableType(parsed, lastPart, beforeLine);
  if (varType) return varType;
  if (visibleImportedTypeNames(parsed, api).has(lastPart)) return lastPart;
  if (IO_FACADE_CLASS_NAMES.has(lastPart) && !visibleImportedTypeNames(parsed, api).has(lastPart)) {
    return null;
  }
  if (consoleBuiltinNames().has(lastPart)) return null;
  if (getStdlibClassRecord(lastPart)) return lastPart;
  if (getAggregateTypeRecord(lastPart, api)) return resolveAggregateTypeName(lastPart, api);
  const sym = parsed.symbols?.find(
    (s) => s.name === lastPart && (s.kind === 'class' || s.kind === 'struct' || s.kind === 'enum'),
  );
  if (sym) return lastPart;
  if (/^[A-Z]/.test(lastPart)) return lastPart;
  return null;
}

function allTypeNames(parsed, api) {
  refreshAggregateAliasMap(api);
  /** @type {Set<string>} */
  const names = new Set(PRIMITIVE_TYPES);
  for (const a of getTypeAliases(api)) names.add(a.name);
  for (const ty of api?.types ?? []) names.add(ty);
  for (const t of getAggregateTypes(api).types ?? []) {
    names.add(t.name);
    for (const a of t.aliases ?? []) names.add(a);
  }
  for (const sym of parsed.symbols) {
    if (sym.kind === 'class' || sym.kind === 'struct' || sym.kind === 'enum') {
      names.add(sym.name);
    }
  }
  for (const name of Object.keys(getStdlibClasses(api))) {
    if (consoleBuiltinNames().has(name)) continue;
    if (IO_FACADE_CLASS_NAMES.has(name)) continue;
    names.add(name);
  }
  for (const name of visibleImportedTypeNames(parsed, api)) {
    names.add(name);
  }
  return names;
}

function addTypeCompletions(items, registry, parsed, api, prefix) {
  const typeAliases = getTypeAliases(api);
  for (const ty of PRIMITIVE_TYPES) {
    if (prefix && !ty.startsWith(prefix)) continue;
    const item = new vscode.CompletionItem(ty, vscode.CompletionItemKind.TypeParameter);
    item.detail = 'primitive type';
    item.sortText = rankLabel(0, ty);
    upsertCompletion(registry, ty, item, 0, items);
  }
  for (const alias of typeAliases) {
    if (prefix && !alias.name.startsWith(prefix)) continue;
    const item = new vscode.CompletionItem(alias.name, vscode.CompletionItemKind.TypeParameter);
    item.detail = `alias → ${alias.mapsTo}`;
    item.documentation = `Type alias for \`${alias.mapsTo}\``;
    item.sortText = rankLabel(0, alias.name);
    upsertCompletion(registry, alias.name, item, 0, items);
  }
  for (const ty of api?.types ?? []) {
    if (prefix && !ty.startsWith(prefix)) continue;
    const agg = getAggregateTypeRecord(ty, api);
    const item = new vscode.CompletionItem(ty, vscode.CompletionItemKind.TypeParameter);
    if (agg) {
      item.detail = `${agg.kind} type`;
      item.documentation = `Built-in ${agg.kind.toLowerCase()} with fields: ${agg.members?.filter((m) => m.kind === 'field').map((m) => m.name).join(', ') ?? ''}`;
    } else {
      item.detail = 'stdlib type';
    }
    item.sortText = rankLabel(1, ty);
    upsertCompletion(registry, ty, item, 1, items);
  }
  for (const sym of parsed.symbols) {
    if (sym.kind !== 'class' && sym.kind !== 'struct' && sym.kind !== 'enum') continue;
    if (prefix && !sym.name.startsWith(prefix)) continue;
    const item = new vscode.CompletionItem(sym.name, vscode.CompletionItemKind.Class);
    item.detail = sym.kind;
    item.documentation = sym.signature;
    item.sortText = rankLabel(0, sym.name);
    upsertCompletion(registry, sym.name, item, 0, items);
  }
}
/**
 * Parse a Far source document into symbols and imports (regex-based, fast).
 * @param {string} text
 * @returns {{
 *   imports: ImportInfo[],
 *   symbols: SymbolInfo[],
 *   importMap: Map<string, ImportInfo>,
 *   variables: ReturnType<typeof parseVariables>,
 *   typeMembers: Map<string, { name: string, kind: string, signature?: string, type?: string, line: number }[]>
 * }}
 */
function parseDocument(text, api = null) {
  /** @type {ImportInfo[]} */
  const imports = [];
  /** @type {SymbolInfo[]} */
  const symbols = [];
  const lines = text.split(/\r?\n/);

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const lineNo = i + 1;

    const fromImp = line.match(/^\s*from\s+([\w.]+)\s+import\s+(.+?)\s*$/);
    if (fromImp) {
      const modPath = fromImp[1];
      const rest = fromImp[2].trim();
      if (rest === '*') {
        imports.push({ path: modPath, symbols: [], fromStar: true, line: lineNo });
      } else {
        /** @type {ImportSymbolEntry[]} */
        const symbolEntries = rest.split(',').map((part) => {
          const m = part.trim().match(/^(\w+)(?:\s+as\s+(\w+))?$/);
          return m ? { name: m[1], alias: m[2] || undefined } : null;
        }).filter(Boolean);
        imports.push({
          path: modPath,
          symbols: symbolEntries.map((s) => s.alias || s.name),
          symbolEntries,
          line: lineNo,
        });
      }
      continue;
    }

    const imp = line.match(/^\s*import\s+([\w.]+)(?:\s+as\s+(\w+))?(?:\s*\{([^}]*)\})?/);
    if (imp) {
      const syms = imp[3]
        ? imp[3].split(',').map((s) => s.trim()).filter(Boolean)
        : [];
      const info = { path: imp[1], alias: imp[2] || undefined, symbols: syms, line: lineNo };
      if (!info.alias && syms.length === 0 && !imp[3]) {
        info.alias = imp[1].split('.').pop();
      }
      imports.push(info);
      continue;
    }

    const fun = line.match(/^\s*(?:public\s+|private\s+|export\s+)*fun\s+(\w+)\s*(\([^)]*\))?(?:\s*->\s*([\w.<>\[\],\s]+))?/);
    if (fun) {
      const sig = buildSignature('fun', fun[1], fun[2] || '()', fun[3]);
      symbols.push({
        name: fun[1],
        kind: 'function',
        line: lineNo,
        col: line.indexOf(fun[1]),
        signature: sig,
        endLine: findBlockEnd(lines, i),
      });
      continue;
    }

    const fn = line.match(/^\s*fn\s+(\w+)\s*(\([^)]*\))?(?:\s*->\s*([\w.<>\[\],\s]+))?/);
    if (fn) {
      symbols.push({
        name: fn[1],
        kind: 'function',
        line: lineNo,
        col: line.indexOf(fn[1]),
        signature: buildSignature('fn', fn[1], fn[2] || '()', fn[3]),
        endLine: findBlockEnd(lines, i),
      });
      continue;
    }

    const st = line.match(/^\s*(?:public\s+)?struct\s+(\w+)/);
    if (st) {
      symbols.push({
        name: st[1],
        kind: 'struct',
        line: lineNo,
        col: line.indexOf(st[1]),
        signature: `struct ${st[1]}`,
        endLine: findBlockEnd(lines, i),
      });
      continue;
    }

    const cl = line.match(/^\s*(?:public\s+)?class\s+(\w+)/);
    if (cl) {
      symbols.push({
        name: cl[1],
        kind: 'class',
        line: lineNo,
        col: line.indexOf(cl[1]),
        signature: `class ${cl[1]}`,
        endLine: findBlockEnd(lines, i),
      });
      continue;
    }

    const en = line.match(/^\s*(?:public\s+)?enum\s+(\w+)/);
    if (en) {
      symbols.push({
        name: en[1],
        kind: 'enum',
        line: lineNo,
        col: line.indexOf(en[1]),
        signature: `enum ${en[1]}`,
        endLine: findBlockEnd(lines, i),
      });
    }
  }

  const importMap = new Map();
  for (const imp of imports) {
    importMap.set(imp.path, imp);
    if (imp.alias) importMap.set(imp.alias, imp);
    const short = imp.path.split('.').pop();
    if (short) importMap.set(short, imp);
  }

  /** @type {Map<string, { name: string, kind: string, signature?: string, type?: string, line: number }[]>} */
  const typeMembers = new Map();
  for (let i = 0; i < lines.length; i++) {
    const cl = lines[i].match(/^\s*(?:public\s+)?class\s+(\w+)/);
    const st = lines[i].match(/^\s*(?:public\s+)?struct\s+(\w+)/);
    if (cl) {
      typeMembers.set(cl[1], parseTypeBody(lines, i, cl[1]));
    } else if (st) {
      typeMembers.set(st[1], parseTypeBody(lines, i, st[1]));
    }
  }

  const variables = parseVariables(text, symbols, api, imports);

  return { imports, symbols, importMap, variables, typeMembers };
}

function buildSignature(kw, name, params, ret) {
  let sig = `${kw} ${name}${params}`;
  if (ret) sig += ` -> ${ret.trim()}`;
  return sig;
}

function findBlockEnd(lines, startIdx) {
  let depth = 0;
  let started = false;
  for (let i = startIdx; i < lines.length; i++) {
    for (const ch of lines[i]) {
      if (ch === '{') { depth++; started = true; }
      else if (ch === '}') depth--;
    }
    if (started && depth <= 0) return i + 1;
  }
  return startIdx + 1;
}

/**
 * @param {vscode.TextDocument} doc
 */
function getParsed(doc) {
  return parseDocument(doc.getText());
}

/**
 * Parse `qualifier.memberPrefix` at end of text (memberPrefix may be empty).
 * Uses the path immediately before the final dot — e.g. `print(math.` → math, not print(math.
 * @param {string} text
 * @returns {{ qualifier: string, memberPrefix: string } | null}
 */
function parseDotAccess(text) {
  const memberMatch = text.match(/\.(\w*)$/);
  if (!memberMatch) return null;
  const memberPrefix = memberMatch[1];
  const beforeDot = text.slice(0, text.length - memberMatch[0].length);
  const qualMatch = beforeDot.match(/([\w.]+)$/);
  if (!qualMatch) return null;
  return { qualifier: qualMatch[1], memberPrefix };
}

/**
 * Resolve qualifier before dot: std.math, alias, local module path.
 * @param {string} qualifier
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {object|null} api
 */
function resolveModulePath(qualifier, parsed, api) {
  if (api?.modules?.[qualifier]) return qualifier;
  const imp = parsed.importMap.get(qualifier);
  if (imp) return imp.path;
  return null;
}

/**
 * @param {object} api
 * @param {string} modPath
 */
function moduleFlatMethods(api, modPath) {
  const mod = api?.modules?.[modPath];
  if (mod?.symbols?.length) {
    if (isFunctionStdlibModule(modPath)) {
      return mod.symbols.filter((s) => s.name !== modPath && s.signature?.startsWith('static '));
    }
    return mod.symbols.filter((s) => s.signature?.startsWith('static '));
  }
  if (isFunctionStdlibModule(modPath) || isGeomNamespaceModule(modPath)) {
    const rec = getStdlibClassRecord(modPath);
    if (rec?.members?.length) {
      return rec.members
        .filter((m) => m.kind === 'method')
        .map((m) => ({
          name: m.name,
          signature: m.signature,
          detail: `${modPath}.${m.name}`,
        }));
    }
  }
  return [];
}

/**
 * Exported type/class names brought into scope by type-module imports.
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {object|null} api
 */
function visibleImportedTypeNames(parsed, api) {
  /** @type {Set<string>} */
  const names = new Set();
  if (!api?.modules) return names;
  for (const imp of parsed.imports) {
    const mod = api.modules[imp.path];
    if (!mod || isFunctionStdlibModule(imp.path)) continue;
    for (const s of mod.symbols ?? []) {
      if (s.name === imp.path) continue;
      names.add(s.name);
    }
  }
  return names;
}

/**
 * @param {string} qualifier import alias or module path
 * @param {string} memberPrefix
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {object|null} api
 */
function getModuleDotMembers(qualifier, memberPrefix, parsed, api) {
  const modPath = resolveModulePath(qualifier, parsed, api);
  if (!modPath) return [];
  /** @type {{ name: string, signature?: string, detail?: string, kind: string }[]} */
  const out = [];

  if (isFunctionStdlibModule(modPath) || isGeomNamespaceModule(modPath)) {
    for (const s of moduleFlatMethods(api, modPath)) {
      if (memberPrefix && !s.name.startsWith(memberPrefix)) continue;
      out.push({
        name: s.name,
        signature: s.signature,
        detail: s.detail ?? `${modPath}.${s.name}`,
        kind: 'method',
      });
    }
    return out;
  }

  const mod = api.modules[modPath];
  for (const s of mod?.symbols ?? []) {
    if (memberPrefix && !s.name.startsWith(memberPrefix)) continue;
    const rec = getStdlibClassRecord(s.name);
    if (rec) {
      out.push({
        name: s.name,
        signature: rec.constructor?.signature ?? s.signature,
        detail: `${modPath}.${s.name}`,
        kind: 'class',
      });
      continue;
    }
    if (s.signature?.startsWith('static ')) {
      out.push({
        name: s.name,
        signature: s.signature,
        detail: s.detail ?? `${modPath}.${s.name}`,
        kind: 'method',
      });
    }
  }
  return out;
}

/**
 * @param {object} api
 * @param {string} modPath
 */
function moduleSymbols(api, modPath) {
  return getModuleDotMembers(modPath, '', { imports: [], importMap: new Map() }, api).map((m) => ({
    name: m.name,
    signature: m.signature,
    detail: m.detail,
  }));
}

function pushModuleDotCompletions(items, members, modLabel) {
  for (const m of members) {
    const kind = m.kind === 'class'
      ? vscode.CompletionItemKind.Class
      : vscode.CompletionItemKind.Method;
    const item = new vscode.CompletionItem(m.name, kind);
    item.detail = m.detail ?? `${modLabel}.${m.name}`;
    if (m.signature) {
      item.documentation = m.signature;
      if (m.kind === 'method' || m.kind === 'class') {
        item.insertText = m.signature.startsWith('static ')
          ? snippetFromSignature(m.name, m.signature.replace(/^static\s+/, 'fun '))
          : snippetFromSignature(m.name, m.signature);
      }
    }
    item.sortText = rankLabel(0, m.name);
    items.push(item);
  }
}

/**
 * Suggest imported module aliases (math, m, vectors) when typing a bare identifier.
 * @param {vscode.CompletionItem[]} items
 * @param {Map<string, { item: vscode.CompletionItem, rank: number }>} registry
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {object|null} api
 * @param {string} prefix
 */
function pushImportedModuleCompletions(items, registry, parsed, api, prefix) {
  const seen = new Set();
  for (const imp of parsed.imports) {
    const alias = imp.alias || imp.path.split('.').pop();
    if (!alias || seen.has(alias)) continue;
    seen.add(alias);
    if (prefix && !alias.startsWith(prefix)) continue;

    const item = new vscode.CompletionItem(alias, vscode.CompletionItemKind.Module);
    if (isFunctionStdlibModule(imp.path)) {
      item.detail = `import ${imp.path} — ${alias}.method(...)`;
      item.documentation = new vscode.MarkdownString(
        `Function module \`${imp.path}\`. Call static methods as \`${alias}.sqrt(...)\`, etc.`,
      );
      item.insertText = new vscode.SnippetString(`${alias}.$0`);
    } else {
      item.detail = `import ${imp.path}`;
      item.documentation = new vscode.MarkdownString(
        `Type module \`${imp.path}\`. Types such as \`dvec2\`, \`ivec2\` are in scope after import.`,
      );
    }
    item.filterText = alias;
    item.sortText = rankLabel(0, alias);
    upsertCompletion(registry, `mod::${alias}`, item, 0, items);
  }
}

/**
 * Collect stdlib symbols visible in this file (imported modules + builtins).
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {object|null} api
 */
function visibleStdlibSymbols(parsed, api) {
  /** @type {Map<string, { name: string, signature?: string, detail?: string, module: string }>} */
  const out = new Map();

  if (!api?.modules) return out;

  for (const imp of parsed.imports) {
    const mod = api.modules[imp.path];
    if (!mod?.symbols) continue;
    const entries = imp.symbolEntries
      ?? imp.symbols.map((name) => ({ name, alias: undefined }));
    const allowAll = imp.fromStar || (entries.length === 0 && !imp.alias);
    if (allowAll) {
      for (const s of mod.symbols) {
        out.set(s.name, {
          name: s.name,
          signature: s.signature,
          detail: s.detail ?? mod.id,
          module: mod.id,
        });
      }
      continue;
    }
    for (const entry of entries) {
      const s = mod.symbols.find((sym) => sym.name === entry.name);
      if (!s) continue;
      const visibleName = entry.alias || entry.name;
      out.set(visibleName, {
        name: visibleName,
        signature: s.signature,
        detail: s.detail ?? mod.id,
        module: mod.id,
      });
    }
  }

  return out;
}

/**
 * @param {object|null} api
 * @param {string} symbolName
 * @param {ReturnType<typeof parseDocument>} parsed
 */
function findModulePathForSymbol(api, symbolName, parsed) {
  for (const imp of parsed.imports) {
    const mod = api?.modules?.[imp.path];
    if (mod?.symbols?.some((s) => s.name === symbolName)) return imp.path;
  }
  return undefined;
}

/**
 * @param {vscode.TextDocument} doc
 * @param {vscode.Position} position
 * @param {object|null} api
 * @param {{ autoStdlib?: boolean, stdlibMinPrefix?: number }} [options]
 */
function provideCompletions(doc, position, api, options) {
  refreshTypeAliasesFromApi(api);
  refreshAggregateAliasMap(api);
  const autoStdlib = options?.autoStdlib !== false;
  const stdlibMinPrefix = options?.stdlibMinPrefix ?? 1;
  const globalNames = new Set(getGlobalBuiltins(api).map((b) => b.name));
  const text = doc.getText();
  const ctx = getCompletionContext(doc, position);
  const { linePrefix } = ctx;
  const parsed = parseDocument(text, api);
  /** @type {vscode.CompletionItem[]} */
  const items = [];
  /** @type {Map<string, { item: vscode.CompletionItem, rank: number }>} */
  const registry = new Map();

  const wordRange = doc.getWordRangeAtPosition(position, /[\w]+/);
  const prefix = wordRange ? doc.getText(wordRange) : '';
  const interpCtx = getInterpolationContext(doc, position);

  // import std.| or import |
  const importMatch = linePrefix.match(/\bimport\s+([\w.]*)$/);
  if (importMatch) {
    const impPrefix = importMatch[1] ?? '';
    for (const p of api?.importPaths ?? []) {
      if (!impPrefix || p.startsWith(impPrefix)) {
        const item = new vscode.CompletionItem(p, vscode.CompletionItemKind.Module);
        item.insertText = p;
        item.detail = 'stdlib module';
        item.sortText = rankLabel(0, p);
        items.push(item);
      }
    }
    return new vscode.CompletionList(items, false);
  }

  // import mod { |  (not inside $"..." strings)
  const selMatch = !interpCtx
    ? linePrefix.match(/\bimport\s+([\w.]+)\s*\{\s*([\w,\s]*)$/)
    : null;
  if (selMatch) {
    const modPath = selMatch[1];
    const mod = api?.modules?.[modPath];
    if (mod?.symbols) {
      const already = new Set(selMatch[2].split(',').map((s) => s.trim()).filter(Boolean));
      for (const s of mod.symbols) {
        if (already.has(s.name)) continue;
        if (prefix && !s.name.startsWith(prefix)) continue;
        const item = new vscode.CompletionItem(s.name, vscode.CompletionItemKind.Function);
        item.detail = mod.id;
        item.documentation = s.signature;
        item.sortText = rankLabel(0, s.name);
        items.push(item);
      }
    }
    return new vscode.CompletionList(items, false);
  }

  // $"hello {| — interpolated string expression (variables, members, builtins)
  if (interpCtx) {
    addInterpolationCompletions(doc, position, parsed, api, interpCtx, registry, items);
    return new vscode.CompletionList(items, true);
  }

  // print($| → interpolated string snippet
  if (/\$\s*$/.test(linePrefix)) {
    const item = new vscode.CompletionItem('"$..." format string', vscode.CompletionItemKind.Snippet);
    item.insertText = new vscode.SnippetString('"${1:text ${2:name}}"$0');
    item.detail = 'Interpolated string — use {var} for values';
    item.documentation = 'Far format strings: `$"hello {name}"` embeds expressions in `{...}`.';
    item.sortText = rankLabel(0, 'interp');
    items.push(item);
    return new vscode.CompletionList(items, false);
  }

  // member access: obj.method, math.sqrt, print(math.)
  const dotAccess = parseDotAccess(linePrefix);
  if (dotAccess && !linePrefix.match(/\b\d+\.\d*$/)) {
    const { qualifier, memberPrefix } = dotAccess;
    const lastPart = qualifier.split('.').pop() ?? qualifier;

    // Module alias / import path: m.sqrt, math.clamp, network.HttpClient
    const modPath = resolveModulePath(qualifier, parsed, api);
    if (modPath) {
      const members = getModuleDotMembers(qualifier, memberPrefix, parsed, api);
      pushModuleDotCompletions(items, members, modPath);
      return new vscode.CompletionList(items, false);
    }
    if (parsed.importMap.has(qualifier)) {
      return new vscode.CompletionList([], false);
    }

    // instance / class members (w.deposit, dvec2.distance, Wallet.new...)
    const typeForMembers = resolveInstanceTypeName(lastPart, parsed, position.line + 1, api);
    if (typeForMembers) {
      for (const m of getTypeMembers(parsed, typeForMembers, api)) {
        if (memberPrefix && !m.name.startsWith(memberPrefix)) continue;
        const kind = m.kind === 'field' || m.kind === 'property'
          ? vscode.CompletionItemKind.Field
          : vscode.CompletionItemKind.Method;
        const item = new vscode.CompletionItem(m.name, kind);
        item.detail = `${typeForMembers}.${m.name} (${m.kind})`;
        if (m.signature) item.documentation = m.doc ? `${m.signature}\n\n${m.doc}` : m.signature;
        if (m.kind === 'method' || m.kind === 'constructor') {
          item.insertText = m.signature
            ? snippetFromSignature(m.name, m.signature)
            : new vscode.SnippetString(`${m.name}($0)`);
        }
        item.sortText = rankLabel(0, m.name);
        items.push(item);
      }
      if (items.length) return new vscode.CompletionList(items, false);
    }

    // After `alias.` with no resolved members, never fall through to globals.
    return new vscode.CompletionList(items, false);
  }

  // Type position: name: int|, -> i64|, (x: str|
  if (ctx.inTypePosition) {
    addTypeCompletions(items, registry, parsed, api, prefix);
    return new vscode.CompletionList(items, false);
  }

  // Inside foo(| — signature help handles overloads; suppress completion noise.
  if (ctx.inCallParens && !ctx.afterDot) {
    return new vscode.CompletionList([], false);
  }

  // Flat module path completion: from vectors| / import network|
  const importModMatch = linePrefix.match(/\b(?:from|import)\s+([\w.]*)$/);
  if (importModMatch) {
    const modPrefix = importModMatch[1] ?? '';
    for (const p of api?.importPaths ?? []) {
      if (!p.startsWith(modPrefix) || p === modPrefix) continue;
      if (modPrefix && !p.startsWith(modPrefix)) continue;
      const rest = modPrefix ? p.slice(modPrefix.length).split('.')[0] : p;
      if (!rest) continue;
      const insert = modPrefix && !modPrefix.endsWith('.') && p.includes('.')
        ? p.slice(modPrefix.length)
        : (modPrefix ? rest : p);
      const label = modPrefix ? rest : p;
      const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Module);
      item.insertText = insert;
      item.detail = p;
      item.sortText = rankLabel(0, p);
      items.push(item);
    }
    if (items.length) return new vscode.CompletionList(items, false);
  }

  const showKeywords = (ctx.atStatementStart || ctx.inStatementKeyword) && !ctx.inExpression;
  const showCallables = ctx.inExpression || ctx.atStatementStart || ctx.inStatementKeyword;

  // Language syntax snippets (for/while/if/fun/import/…) — including inside function bodies
  pushSyntaxSnippets(items, linePrefix, prefix);
  pushForLoopContinuations(items, linePrefix);

  // for item in collection { — suggest indexable variables after `in`
  const forInColl = linePrefix.match(/\bfor\s+\w+\s+in\s*([\w.]*)$/);
  const inForInCollection = !!forInColl;
  if (forInColl) {
    const collPrefix = forInColl[1] ?? '';
    const seen = new Set();
    for (const v of parsed.variables) {
      if (!isCollectionTypeName(v.type)) continue;
      if (collPrefix && !v.name.startsWith(collPrefix)) continue;
      if (seen.has(v.name)) continue;
      seen.add(v.name);
      const item = new vscode.CompletionItem(v.name, vscode.CompletionItemKind.Variable);
      item.detail = v.type ? `for-in: ${v.type}` : 'for-in collection';
      item.sortText = rankLabel(0, v.name);
      items.push(item);
    }
    if (seen.size > 0) return new vscode.CompletionList(dedupeCompletionItems(items), false);
  }

  // C-style "int x = ..." → suggest Far syntax: x: int = 0
  const cStyleType = linePrefix.match(/^\s+(int|i32|i64|i128|long|short|byte|float|double|str|string|bool|char|uint|ulong)\s*$/i);
  if (cStyleType && showCallables) {
    const ty = cStyleType[1];
    const item = new vscode.CompletionItem(`${ty} variable (Far syntax)`, vscode.CompletionItemKind.Snippet);
    item.insertText = new vscode.SnippetString('${1:name}: ' + ty + ' = ${2:0}');
    item.detail = `Use name: ${ty} = value (not C-style ${ty} name)`;
    item.sortText = rankLabel(0, 'far_decl');
    items.push(item);
  }

  // Variables & parameters in scope (before current line)
  const seenVars = new Set();
  for (const v of parsed.variables) {
    if (v.line > position.line + 1) continue;
    if (prefix && !inForInCollection && !v.name.startsWith(prefix)) continue;
    if (seenVars.has(v.name)) continue;
    seenVars.add(v.name);
    const item = new vscode.CompletionItem(v.name, vscode.CompletionItemKind.Variable);
    item.detail = v.type ? `${v.kind}: ${v.type}` : v.kind;
    item.sortText = rankLabel(0, v.name);
    upsertCompletion(registry, v.name, item, 0, items);
  }

  // Local symbols (functions, types in file)
  for (const sym of parsed.symbols) {
    if (prefix && !sym.name.startsWith(prefix)) continue;
    const kind = sym.kind === 'function'
      ? vscode.CompletionItemKind.Function
      : vscode.CompletionItemKind.Struct;
    const item = new vscode.CompletionItem(sym.name, kind);
    item.detail = sym.kind;
    if (sym.signature) item.documentation = sym.signature;
    if (sym.kind === 'function' && sym.signature) {
      const params = parseParamsFromSignature(sym.signature);
      if (params.length) {
        const placeholders = params.map((p, i) => `\${${i + 1}:${p.split(':')[0].trim()}}`).join(', ');
        item.insertText = new vscode.SnippetString(`${sym.name}(${placeholders})`);
      }
    }
    upsertCompletion(registry, sym.name, item, 1, items);
  }

  if (showCallables && !ctx.afterDot && !ctx.inCallParens) {
    pushImportedModuleCompletions(items, registry, parsed, api, prefix);

    const consoleNames = consoleBuiltinNames();

    // Global IO / builtins — one item per name; overloads in signature help inside (...)
    pushGroupedBuiltinCompletions(getConsoleBuiltins(), prefix, 0, registry, items);

    // Other global builtins (len, aggregate ctors, …)
    const nonIo = getGlobalBuiltins(api).filter((b) => !consoleNames.has(b.name));
    pushGroupedBuiltinCompletions(nonIo, prefix, 1, registry, items);

    // Stdlib — lower priority; needs import on accept
    if (autoStdlib) {
      const includeStdlib = !prefix || prefix.length >= stdlibMinPrefix;
      if (includeStdlib) {
        for (const s of allStdlibCallables(api)) {
          if (consoleNames.has(s.name)) continue;
          if (prefix && !s.name.startsWith(prefix)) continue;
          const label = s.detail && s.detail.includes('.')
            ? `${s.name} (${s.modulePath})`
            : s.name;
          const item = makeCallableCompletionItem(
            { ...s, label, insertName: s.name },
            parsed,
            doc,
            2,
          );
          upsertCompletion(registry, `${s.modulePath}::${s.signature ?? s.name}`, item, 2, items);
        }
      }
    }
  }

  // Explicit imports (already in file — higher rank)
  const stdVisible = visibleStdlibSymbols(parsed, api);
  for (const s of stdVisible.values()) {
    if (prefix && !s.name.startsWith(prefix)) continue;
    const modPath = findModulePathForSymbol(api, s.name, parsed);
    const item = makeCallableCompletionItem(
      { ...s, modulePath: modPath, insertName: s.name },
      parsed,
      doc,
      0,
    );
    upsertCompletion(registry, `imp::${s.name}`, item, 0, items);
  }

  // Types last in expression context (casts only — skip names that are constructors)
  if ((ctx.inExpression || ctx.atStatementStart) && !cStyleType) {
    const ctorNames = aggregateConstructorNameSet(api);
    for (const ty of allTypeNames(parsed, api)) {
      if (prefix && !ty.startsWith(prefix)) continue;
      if (ctorNames.has(ty)) continue;
      if (consoleBuiltinNames().has(ty)) continue;
      if (IO_FACADE_CLASS_NAMES.has(ty) && !visibleImportedTypeNames(parsed, api).has(ty)) continue;
      if (isRegisteredCallable(registry, ty)) continue;
      const item = new vscode.CompletionItem(ty, vscode.CompletionItemKind.TypeParameter);
      item.detail = ALIAS_TO_CANONICAL.has(ty) ? `alias → ${ALIAS_TO_CANONICAL.get(ty)}` : 'type';
      item.sortText = rankLabel(4, ty);
      upsertCompletion(registry, ty, item, 4, items);
    }
  }

  if (showKeywords) {
    for (const kw of api?.keywords ?? []) {
      if (globalNames.has(kw)) continue;
      if (!STMT_KEYWORDS.has(kw) && kw !== 'true' && kw !== 'false' && kw !== 'and' && kw !== 'or' && kw !== 'not') {
        continue;
      }
      if (prefix && !kw.startsWith(prefix)) continue;
      const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
      item.sortText = rankLabel(5, kw);
      upsertCompletion(registry, kw, item, 5, items);
    }
  }

  // Expression-only operators / literals
  if (ctx.inExpression) {
    for (const lit of ['true', 'false']) {
      if (prefix && !lit.startsWith(prefix)) continue;
      const item = new vscode.CompletionItem(lit, vscode.CompletionItemKind.Value);
      item.detail = 'literal';
      item.sortText = rankLabel(6, lit);
      upsertCompletion(registry, lit, item, 6, items);
    }
  }

  return new vscode.CompletionList(dedupeCompletionItems(items), false);
}

/**
 * @param {vscode.TextDocument} doc
 * @param {vscode.Position} position
 * @param {object|null} api
 */
function provideSignatureHelp(doc, position, api) {
  const text = doc.getText();
  const offset = doc.offsetAt(position);
  const before = text.slice(0, offset);

  // Find active call: foo(...|
  const callMatch = before.match(/([\w.]+)\s*\([^()]*$/);
  if (!callMatch) return null;

  const callName = callMatch[1];
  const parsed = parseDocument(text, api);

  let signature = null;
  let label = callName;

  // Local function
  const local = parsed.symbols.find((s) => s.name === callName.split('.').pop());
  if (local?.signature) {
    signature = local.signature;
  }

  // std.module.func, module alias (m.sqrt), or obj.method
  if (!signature && callName.includes('.')) {
    const parts = callName.split('.');
    const fname = parts.pop();
    const modPath = resolveModulePath(parts.join('.'), parsed, api)
      ?? resolveModulePath(parts[0], parsed, api);
    if (modPath) {
      const sym = moduleFlatMethods(api, modPath).find((s) => s.name === fname)
        ?? api?.modules?.[modPath]?.symbols?.find((s) => s.name === fname);
      if (sym?.signature) {
        signature = sym.signature;
        label = sym.detail ?? callName;
      }
    }
    if (!signature) {
      const typeName = resolveInstanceTypeName(parts[parts.length - 1], parsed, position.line + 1, api);
      if (typeName && fname) {
        const member = getTypeMembers(parsed, typeName, api).find((m) => m.name === fname);
        if (member?.signature) {
          signature = member.signature;
          label = `${typeName}.${fname}`;
        }
      }
    }
  }

  // Imported / global builtins (incl. IO overloads inside parentheses)
  if (!signature) {
    const bare = callName.split('.').pop();
    const ioNames = consoleBuiltinNames();
    const overloads = ioNames.has(bare)
      ? getConsoleBuiltins().filter((b) => b.name === bare && b.signature)
      : getGlobalBuiltins(api).filter((b) => b.name === bare && b.signature && !ioNames.has(b.name));
    if (overloads.length) {
      const inner = before.slice(before.lastIndexOf('(') + 1);
      const argc = inner.trim() === '' ? 0 : inner.split(',').length;
      const sigs = overloads.map((b) => {
        const sigLabel = b.signature.replace(/^fun\s+/, '').replace(/^def\s+/, 'fun ');
        const sig = new vscode.SignatureInformation(sigLabel, b.detail ?? bare);
        sig.parameters = parseParamsFromSignature(b.signature)
          .map((p) => new vscode.ParameterInformation(p));
        return sig;
      });
      let active = 0;
      if (sigs.length > 1) {
        active = sigs.findIndex((_, i) => {
          const params = parseParamsFromSignature(overloads[i].signature);
          return argc <= params.length;
        });
        if (active < 0) active = sigs.length - 1;
      }
      return new vscode.SignatureHelp(sigs, active);
    }

    const stdVisible = visibleStdlibSymbols(parsed, api);
    const sym = stdVisible.get(bare);
    if (sym?.signature) signature = sym.signature;

    if (!signature && api?.modules) {
      for (const s of allStdlibCallables(api)) {
        if (s.name === bare && s.signature) {
          signature = s.signature;
          label = s.detail ?? bare;
          break;
        }
      }
    }
  }

  if (!signature) return null;

  const params = parseParamsFromSignature(signature);
  const sig = new vscode.SignatureInformation(signature, label);
  sig.parameters = params.map((p) => new vscode.ParameterInformation(p));
  return new vscode.SignatureHelp([sig], 0);
}

function parseParamsFromSignature(sig) {
  const m = sig.match(/\(([^)]*)\)/);
  if (!m || !m[1].trim()) return [];
  return m[1].split(',').map((p) => p.trim()).filter(Boolean);
}

function snippetFromSignature(name, signature) {
  const params = parseParamsFromSignature(signature);
  if (!params.length) return new vscode.SnippetString(`${name}()`);
  const placeholders = params.map((p, i) => {
    const pn = p.split(':')[0].trim().replace(/=.*$/, '').trim();
    return `\${${i + 1}:${pn}}`;
  }).join(', ');
  return new vscode.SnippetString(`${name}(${placeholders})`);
}

/**
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {string} modulePath
 * @param {string} symbolName
 */
function isSymbolImported(parsed, modulePath, symbolName) {
  const imp = parsed.imports.find((i) => i.path === modulePath);
  if (!imp) return false;
  if (imp.symbols.length === 0) return true;
  return imp.symbols.includes(symbolName);
}

/**
 * @param {vscode.TextDocument} doc
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {string} modulePath
 * @param {string} symbolName
 * @returns {vscode.TextEdit|null}
 */
function importEditForSymbol(doc, parsed, modulePath, symbolName) {
  if (isSymbolImported(parsed, modulePath, symbolName)) return null;

  const existing = parsed.imports.find((i) => i.path === modulePath);
  if (existing) {
    const line = doc.lineAt(existing.line - 1);
    const closeBrace = line.text.lastIndexOf('}');
    if (closeBrace < 0) return null;
    return vscode.TextEdit.insert(new vscode.Position(existing.line - 1, closeBrace), `, ${symbolName}`);
  }

  let insertLine = 0;
  if (parsed.imports.length > 0) {
    insertLine = parsed.imports[0].line - 1;
  } else {
    const pkg = parsed.symbols.find((s) => s.kind === 'function' || s.kind === 'class');
    insertLine = pkg ? Math.max(0, pkg.line - 1) : 0;
  }
  return vscode.TextEdit.insert(
    new vscode.Position(insertLine, 0),
    `import ${modulePath} { ${symbolName} }\n`,
  );
}

/**
 * @param {{ name: string, signature?: string, detail?: string, modulePath?: string, insertName?: string, label?: string }} sym
 * @param {ReturnType<typeof parseDocument>} parsed
 * @param {vscode.TextDocument} doc
 * @param {number} rank
 */
function makeCallableCompletionItem(sym, parsed, doc, rank) {
  const callName = sym.insertName ?? sym.name;
  const label = sym.label ?? sym.name;
  const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Function);
  item.detail = sym.detail ?? 'function';
  if (sym.signature) {
    item.documentation = sym.signature;
    item.insertText = snippetFromSignature(callName, sym.signature);
  }
  item.filterText = callName;
  item.sortText = rankLabel(rank, callName);

  if (sym.modulePath && !isSymbolImported(parsed, sym.modulePath, callName)) {
    const edit = importEditForSymbol(doc, parsed, sym.modulePath, callName);
    if (edit) {
      item.additionalTextEdits = [edit];
      item.detail = `${sym.detail} (+ auto import)`;
    }
  }
  return item;
}

/**
 * @param {vscode.TextDocument} doc
 * @param {vscode.Position} position
 * @param {object|null} api
 * @param {(args: string[], cwd: string) => Promise<{stdout: string}>} runDoc
 */
async function provideHover(doc, position, api, runDoc) {
  const range = doc.getWordRangeAtPosition(position, /[\w.]+/);
  if (!range) return null;

  const word = doc.getText(range);
  const parsed = parseDocument(doc.getText(), api);
  const lineText = doc.lineAt(position.line).text;
  const prefix = lineText.slice(0, range.end.character);

  // obj.method — hover on method name (e.g. cli.get, m.sqrt, print(math.sqrt))
  const dotMethod = parseDotAccess(prefix);
  if (dotMethod?.memberPrefix && dotMethod.memberPrefix === word) {
    const modPath = resolveModulePath(dotMethod.qualifier, parsed, api);
    if (modPath) {
      const sym = getModuleDotMembers(dotMethod.qualifier, dotMethod.memberPrefix, parsed, api)[0]
        ?? moduleFlatMethods(api, modPath).find((s) => s.name === dotMethod.memberPrefix);
      if (sym?.signature) {
        return new vscode.Hover(hoverMarkdown(sym.detail ?? `${modPath}.${word}`, sym.signature), range);
      }
    }
    const objType = resolveInstanceTypeName(dotMethod.qualifier, parsed, position.line + 1, api);
    if (objType) {
      const member = getTypeMembers(parsed, objType, api).find((m) => m.name === word);
      if (member?.signature) {
        const parts = [`**${objType}.${word}**`, '', '```far', member.signature, '```'];
        if (member.doc) parts.push('', member.doc);
        return new vscode.Hover(parts.join('\n'), range);
      }
    }
  }

  // Stdlib class name (HttpClient)
  const stdClass = getStdlibClassRecord(word);
  if (stdClass) {
    const parts = [`**class** \`${word}\` (${stdClass.module})`];
    if (stdClass.doc) parts.push('', stdClass.doc);
    if (stdClass.constructor?.signature) {
      parts.push('', '```far', stdClass.constructor.signature, '```');
    }
    const methods = (stdClass.members ?? []).map((m) => `\`${m.signature}\``).join('\n');
    if (methods) parts.push('', methods);
    return new vscode.Hover(parts.join('\n'), range);
  }

  // Qualified std.module.symbol
  if (word.includes('.')) {
    const parts = word.split('.');
    const fname = parts.pop();
    const modPath = parts.join('.');
    const mod = api?.modules?.[modPath];
    const sym = mod?.symbols?.find((s) => s.name === fname);
    if (sym) {
      return new vscode.Hover(hoverMarkdown(sym.detail ?? word, sym.signature), range);
    }
  }

  // Stdlib symbol (imported or search all modules for qualified hover on bare name)
  if (api?.modules) {
    const stdVisible = visibleStdlibSymbols(parsed, api);
    const vis = stdVisible.get(word);
    if (vis) {
      return new vscode.Hover(hoverMarkdown(vis.detail, vis.signature), range);
    }
    for (const mod of Object.values(api.modules)) {
      const sym = mod.symbols?.find((s) => s.name === word);
      if (sym) {
        return new vscode.Hover(hoverMarkdown(sym.detail ?? sym.name, sym.signature), range);
      }
    }
  }

  if (api?.keywords?.includes(word) && !getGlobalBuiltins(api).some((b) => b.name === word)) {
    return new vscode.Hover(`**keyword** \`${word}\``, range);
  }
  if (api?.types?.includes(word)) {
    const agg = getAggregateTypeRecord(word, api);
    if (agg) {
      const fields = (agg.members ?? []).filter((m) => m.kind === 'field').map((m) => m.signature).join('\n');
      const methods = (agg.members ?? []).filter((m) => m.kind === 'method').map((m) => `\`${m.signature}\``).join('\n');
      const parts = [`**${agg.kind} type** \`${word}\``];
      if (fields) parts.push('', fields);
      if (methods) parts.push('', methods);
      return new vscode.Hover(parts.join('\n'), range);
    }
    return new vscode.Hover(`**type** \`${word}\``, range);
  }

  const aggType = getAggregateTypeRecord(word, api);
  if (aggType) {
    const fields = (aggType.members ?? []).filter((m) => m.kind === 'field').map((m) => m.signature).join('\n');
    return new vscode.Hover(`**${aggType.kind} type** \`${word}\`\n\n${fields}`, range);
  }

  const aggCtor = getAggregateTypes(api).constructors?.find((c) => c.name === word);
  if (aggCtor?.signature) {
    return new vscode.Hover(hoverMarkdown(aggCtor.detail ?? 'builtin constructor', aggCtor.signature), range);
  }
  const globalMatches = getGlobalBuiltins(api).filter((b) => b.name === word);
  if (globalMatches.length) {
    const parts = globalMatches.map((b) => {
      const lines = [`**${b.detail ?? 'builtin'}**`, '', `\`${b.signature}\``];
      if (b.doc) lines.push('', b.doc);
      return lines.join('\n');
    });
    return new vscode.Hover(new vscode.MarkdownString(parts.join('\n\n---\n\n')), range);
  }
  if (api?.builtins?.includes(word)) {
    return new vscode.Hover(`**builtin** \`${word}\``, range);
  }

  const alias = getTypeAliases(api).find((a) => a.name === word);
  if (alias) {
    return new vscode.Hover(`**type alias** \`${word}\` → \`${alias.mapsTo}\``, range);
  }
  if (PRIMITIVE_TYPES.includes(word)) {
    return new vscode.Hover(`**primitive type** \`${word}\``, range);
  }

  const variable = parsed.variables.filter((v) => v.name === word).pop();
  if (variable) {
    const parts = [`**${variable.kind}** \`${variable.name}\``];
    if (variable.type) {
      parts.push('', `type: \`${variable.type}\``);
      if (variable.type === 'string') {
        parts.push('', 'Far string (UTF-8 text). For HTTP clients, this is the raw response body after headers.');
      }
    }
    if (variable.scope) parts.push('', `in \`${variable.scope}\``);
    return new vscode.Hover(parts.join('\n'), range);
  }

  // Local symbol
  const local = parsed.symbols.find((s) => s.name === word);
  if (local) {
    const parts = [`**${local.kind}** \`${local.name}\``];
    if (local.signature) {
      parts.push('', '```far', local.signature, '```');
    }
    try {
      const { stdout } = await runDoc(['doc', word, doc.uri.fsPath], doc.uri.fsPath);
      const docText = stdout?.trim();
      if (docText && !docText.startsWith('Documentation for ')) {
        parts.push('', docText);
      }
    } catch { /* compiler optional */ }
    return new vscode.Hover(parts.join('\n'), range);
  }

  return null;
}

function hoverMarkdown(title, signature) {
  const parts = [`**${title}**`];
  if (signature) parts.push('', '```far', signature, '```');
  return parts.join('\n');
}

/**
 * @param {vscode.TextDocument} doc
 * @param {vscode.Position} position
 * @param {object|null} api
 */
function provideDefinition(doc, position, api) {
  const parsed = parseDocument(doc.getText(), api);

  // Try qualified name under cursor
  const qualRange = doc.getWordRangeAtPosition(position, /[\w.]+/);
  if (qualRange) {
    const qual = doc.getText(qualRange);
    if (qual.includes('.')) {
      const parts = qual.split('.');
      const fname = parts.pop();
      const modPath = parts.join('.');
      const mod = api?.modules?.[modPath];
      if (mod) {
        // Stdlib: no source file; show signature in peek via returning null is ok
        // Return a location in a virtual doc - skip for now
        return null;
      }
    }
  }

  const range = doc.getWordRangeAtPosition(position, /\w+/);
  if (!range) return null;
  const word = doc.getText(range);

  const sym = parsed.symbols.find((s) => s.name === word);
  if (sym) {
    const pos = new vscode.Position(sym.line - 1, sym.col >= 0 ? sym.col : 0);
    return new vscode.Location(doc.uri, new vscode.Range(pos, pos));
  }

  return null;
}

/**
 * @param {vscode.TextDocument} doc
 */
function provideDocumentSymbols(doc) {
  const parsed = parseDocument(doc.getText());
  return parsed.symbols.map((sym) => {
    const kindMap = {
      function: vscode.SymbolKind.Function,
      struct: vscode.SymbolKind.Struct,
      class: vscode.SymbolKind.Class,
      enum: vscode.SymbolKind.Enum,
    };
    const start = new vscode.Position(sym.line - 1, 0);
    const end = new vscode.Position((sym.endLine ?? sym.line) - 1, 0);
    const info = new vscode.DocumentSymbol(
      sym.name,
      sym.signature ?? sym.kind,
      kindMap[sym.kind] ?? vscode.SymbolKind.Variable,
      new vscode.Range(start, end),
      new vscode.Range(start, end),
    );
    return info;
  });
}

/**
 * Parse compiler stderr into VS Code diagnostics (GCC + legacy formats).
 * @param {string} text
 * @param {vscode.Uri} uri
 */
function parseDiagnostics(text, uri) {
  /** @type {vscode.Diagnostic[]} */
  const diags = [];
  const lines = text.split(/\r?\n/);
  const fileBase = uri.fsPath.replace(/\\/g, '/').split('/').pop();

  const gccRe = /^(.+?):(\d+):(\d+):\s*(error|warning):\s*(.*)$/;
  const legacyRe = /^(?:error:\s*)?line\s+(\d+):(\d+):\s*(.*)$/;

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const gm = line.match(gccRe);
    if (gm) {
      const fpath = gm[1].replace(/\\/g, '/');
      if (!fpath.endsWith(fileBase) && fpath !== uri.fsPath && !uri.fsPath.replace(/\\/g, '/').endsWith(fpath)) {
        // might still be our file with different path prefix
      }
      const ln = parseInt(gm[2], 10) - 1;
      const col = parseInt(gm[3], 10) - 1;
      const sev = gm[4] === 'warning'
        ? vscode.DiagnosticSeverity.Warning
        : vscode.DiagnosticSeverity.Error;
      let endCol = col + 1;
      if (i + 1 < lines.length && lines[i + 1].includes('|')) {
        const srcLine = lines[i + 1].replace(/^\s*\d+\s*\|\s*/, '');
        if (i + 2 < lines.length && lines[i + 2].includes('^')) {
          const caret = lines[i + 2].replace(/^\s*\d*\s*\|\s*/, '');
          const caretPos = caret.indexOf('^');
          if (caretPos >= 0) endCol = caretPos + 1;
        }
        const range = new vscode.Range(ln, col, ln, Math.max(endCol, col + 1));
        diags.push(new vscode.Diagnostic(range, gm[5], sev));
        i += 2;
        continue;
      }
      diags.push(new vscode.Diagnostic(
        new vscode.Range(ln, col, ln, col + 1),
        gm[5],
        sev,
      ));
      continue;
    }

    const lm = line.match(legacyRe);
    if (lm) {
      const ln = parseInt(lm[1], 10) - 1;
      const col = parseInt(lm[2], 10) - 1;
      diags.push(new vscode.Diagnostic(
        new vscode.Range(ln, col, ln, col + 1),
        lm[3],
        vscode.DiagnosticSeverity.Error,
      ));
      continue;
    }

    if (line.startsWith('error: ')) {
      diags.push(new vscode.Diagnostic(
        new vscode.Range(0, 0, 0, 1),
        line.slice(7),
        vscode.DiagnosticSeverity.Error,
      ));
    }
  }
  return diags;
}

module.exports = {
  parseDocument,
  parseVariables,
  provideCompletions,
  getCompletionContext,
  pushSyntaxSnippets,
  provideSignatureHelp,
  provideHover,
  provideDefinition,
  provideDocumentSymbols,
  parseDiagnostics,
  getTypeMembers,
  resolveInstanceTypeName,
  resolveConstructorType,
  pushImportedModuleCompletions,
  parseDotAccess,
  resolveModulePath,
  getModuleDotMembers,
  visibleImportedTypeNames,
  inferCollectionElemType,
  isCollectionTypeName,
  KEYWORD_SET,
  allStdlibCallables,
};
