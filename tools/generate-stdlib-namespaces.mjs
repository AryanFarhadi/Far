#!/usr/bin/env node
/**
 * Flatten stdlib import paths: math, vectors, network (no std. prefix).
 * Merges category modules and rewrites far_stdlib_modules.cpp map + resolver.
 *
 * Run after generate-geom-classes.mjs:
 *   node tools/generate-geom-classes.mjs
 *   node tools/generate-stdlib-namespaces.mjs
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const cppPath = path.join(root, 'src', 'far_stdlib_modules.cpp');

const GEOM_BEGIN = '// === GEOM_MODULES_BEGIN ===';
const GEOM_END = '// === GEOM_MODULES_END ===';
const MAP_BEGIN = '// === FLAT_MODULE_MAP_BEGIN ===';
const MAP_END = '// === FLAT_MODULE_MAP_END ===';

/** Old primary map key (std.X) -> new flat key or merge group */
const FLAT_KEY = {
  'std.bench': 'bench',
  'std.cli': 'cli',
  'std.compress': 'compress',
  'std.crypto': 'crypto',
  'std.csv': 'csv',
  'std.date': 'date',
  'std.env': 'env',
  'std.fs': 'fs',
  'std.hash': 'hash',
  'std.i18n': 'i18n',
  'std.json': 'json',
  'std.log': 'log',
  'std.math': 'math',
  'std.proc': 'proc',
  'std.random': 'random',
  'std.regex': 'regex',
  'std.test': 'test',
  'std.time': 'time',
  'std.xml': 'xml',
  'std.yaml': 'yaml',
  'std.net': 'network',
};

const MERGE_GROUPS = {
  io: ['std.console', 'std.input', 'std.output', 'std.terminal'],
  dev: [
    'std.debug', 'std.deps', 'std.docs', 'std.format', 'std.hotreload', 'std.immutable',
    'std.inference', 'std.lint', 'std.live', 'std.lsp', 'std.nullable', 'std.pattern',
    'std.pkg', 'std.profile', 'std.readonly', 'std.repl', 'std.shell',
  ],
  network: [
    'std.client', 'std.tcp', 'std.udp', 'std.http', 'std.https', 'std.rest', 'std.rpc',
    'std.grpc', 'std.serialize', 'std.websocket', 'std.pack', 'std.net',
  ],
  science: [
    'std.fft', 'std.ml', 'std.numerical', 'std.optimization', 'std.physics', 'std.statistics',
  ],
  security: [
    'std.boundscheck', 'std.concurrency', 'std.secure', 'std.memsafe', 'std.overflow',
    'std.permission', 'std.sandbox',
  ],
  perf: [
    'std.incremental', 'std.llvm', 'std.lowmem', 'std.native', 'std.predictable', 'std.simd',
    'std.startup', 'std.threads', 'std.vectorize',
  ],
};

const MERGE_ORDER = {
  network: MERGE_GROUPS.network,
  io: MERGE_GROUPS.io,
  dev: MERGE_GROUPS.dev,
  science: MERGE_GROUPS.science,
  security: MERGE_GROUPS.security,
  perf: MERGE_GROUPS.perf,
};

const GEOM_FLAT = {
  'std.bounds': 'bounds',
  'std.color': 'colors',
  'std.color32': 'colors',
  'std.dmat2': 'matrices',
  'std.dmat3': 'matrices',
  'std.dmat4': 'matrices',
  'std.dpoint': 'points',
  'std.dquat': 'quaternions',
  'std.drect': 'rects',
  'std.dvec2': 'vectors',
  'std.dvec3': 'vectors',
  'std.dvec4': 'vectors',
  'std.point': 'points',
  'std.rect': 'rects',
  'std.vec2': 'vectors',
  'std.vec3': 'vectors',
  'std.vec4': 'vectors',
  'std.ivec2': 'vectors',
  'std.ivec3': 'vectors',
  'std.ivec4': 'vectors',
  'std.mat2': 'matrices',
  'std.mat3': 'matrices',
  'std.mat4': 'matrices',
  'std.quat': 'quaternions',
  'std.transform': 'transforms',
};

function parseModules(cpp) {
  const re = /static const char (kStdlibMod_\w+)\[\] = R"FAR_STDLIB\(([\s\S]*?)\)FAR_STDLIB";/g;
  const modules = new Map();
  let m;
  while ((m = re.exec(cpp)) !== null) {
    modules.set(m[1], m[2]);
  }
  return modules;
}

function parseMap(cpp) {
  const re = /\{"([^"]+)",\s*(kStdlibMod_\w+)\}/g;
  const map = [];
  let m;
  while ((m = re.exec(cpp)) !== null) {
    map.push({ key: m[1], var: m[2] });
  }
  return map;
}

function extractClasses(source) {
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

function extractExports(source) {
  const m = source.match(/^export\s+(.+)$/m);
  if (!m) return [];
  return m[1].split(',').map((s) => s.trim()).filter(Boolean);
}

function lowercaseFacade(name) {
  return name.replace(/[A-Z]/g, (c) => c.toLowerCase());
}

/** Constructible network / client classes — PascalCase, not lowercase namespaces. */
const PASCAL_INSTANCE_CLASSES = new Set([
  'TcpClient', 'UdpClient', 'HttpClient', 'HttpsClient',
]);

function facadeName(original) {
  return PASCAL_INSTANCE_CLASSES.has(original) ? original : lowercaseFacade(original);
}

const PASCAL_REMNANTS = {
  Websocket: 'websocket', Serialize: 'serialize', Client: 'client', Pack: 'pack', Net: 'net',
  Console: 'console', Input: 'input', Output: 'output', Terminal: 'terminal',
  Boundscheck: 'boundscheck', Concurrency: 'concurrency', Secure: 'secure', Memsafe: 'memsafe',
  Overflow: 'overflow', Permission: 'permission', Sandbox: 'sandbox',
  Threads: 'threads', Llvm: 'llvm', Simd: 'simd', Native: 'native', Startup: 'startup',
  Vectorize: 'vectorize', Incremental: 'incremental', Lowmem: 'lowmem', Predictable: 'predictable',
  Fft: 'fft', Ml: 'ml', Numerical: 'numerical', Optimization: 'optimization', Physics: 'physics',
  Statistics: 'statistics', Debug: 'debug', Deps: 'deps', Docs: 'docs', Format: 'format',
  Hotreload: 'hotreload', Immutable: 'immutable', Inference: 'inference', Lint: 'lint',
  Live: 'live', Lsp: 'lsp', Nullable: 'nullable', Pattern: 'pattern', Pkg: 'pkg',
  Profile: 'profile', Readonly: 'readonly', Repl: 'repl', Shell: 'shell',
  Math: 'math', Json: 'json', Hash: 'hash', Log: 'log', Fs: 'fs', Time: 'time', Date: 'date',
  Proc: 'proc', Random: 'random', Regex: 'regex', Csv: 'csv', Xml: 'xml', Yaml: 'yaml',
  Bench: 'bench', Cli: 'cli', Compress: 'compress', Crypto: 'crypto', Env: 'env', I18n: 'i18n',
  Test: 'test', Http: 'http', Https: 'https', Tcp: 'tcp', Udp: 'udp', Rest: 'rest', Rpc: 'rpc',
  Grpc: 'grpc',
};

function normalizePascalRemnants(source) {
  let out = source;
  const keys = Object.keys(PASCAL_REMNANTS).sort((a, b) => b.length - a.length);
  for (const old of keys) {
    out = out.replace(new RegExp(`\\b${old}\\b`, 'g'), PASCAL_REMNANTS[old]);
  }
  return out;
}

function renameClassInSource(source, oldName, newName) {
  if (oldName === newName) return source;
  let out = source;
  out = out.replace(`public class ${oldName} {`, `public class ${newName} {`);
  out = out.replace(new RegExp(`\\bpublic\\s+${oldName}\\s*\\(`, 'g'), `public ${newName}(`);
  out = out.replace(new RegExp(`\\b${oldName}\\s*\\(`, 'g'), `${newName}(`);
  out = out.replace(new RegExp(`->\\s*${oldName}\\b`, 'g'), `-> ${newName}`);
  return out;
}

function lowercaseModuleSource(source) {
  const classes = extractClasses(source);
  let out = source;
  const exportNames = [];
  for (const cls of classes) {
    const newName = facadeName(cls.name);
    out = renameClassInSource(out, cls.name, newName);
    exportNames.push(newName);
  }
  if (exportNames.length) {
    out = out.replace(/^export\s+.+$/m, `export ${[...new Set(exportNames)].join(', ')}`);
  }
  return normalizePascalRemnants(out);
}

function buildMergedModule(flatName, parts) {
  const allClasses = [];
  const exports = [];
  const seen = new Set();
  for (const src of parts) {
    for (const cls of extractClasses(src)) {
      const newName = facadeName(cls.name);
      let body = renameClassInSource(cls.body, cls.name, newName);
      if (seen.has(newName)) continue;
      seen.add(newName);
      allClasses.push(body);
      exports.push(newName);
    }
  }
  return normalizePascalRemnants(`package far

module ${flatName}

${allClasses.join('\n\n')}

export ${exports.join(', ')}`);
}

function cIdent(flat) {
  return `kStdlibMod_${flat}`;
}

function emitModuleBlock(flat, body) {
  return `static const char ${cIdent(flat)}[] = R"FAR_STDLIB(${body}
)FAR_STDLIB";`;
}

function flattenPackage(source) {
  return source.replace(/^package std\b/m, 'package far');
}

function retargetModule(source, flatName) {
  return lowercaseModuleSource(flattenPackage(source).replace(/^module \S+/m, `module ${flatName}`));
}

function generateLegacyHints() {
  const lines = [];
  const add = (old, hint) => lines.push(`      {"${old}", "${hint}"},`);

  add('std.math', 'use import math');
  add('core.math', 'use import math');
  add('std.linalg', 'use import vectors');
  add('std.vectors', 'use import vectors');
  add('std.matrices', 'use import matrices');
  add('modern', 'use import dev');
  add('std.modern', 'use import dev');

  for (const [old, flat] of Object.entries(GEOM_FLAT)) {
    add(old, `use import ${flat}`);
  }

  for (const [group, keys] of Object.entries(MERGE_GROUPS)) {
    for (const old of keys) {
      add(old, `use import ${group}`);
      add(old.replace('std.', 'net.'), `use import ${group}`);
      add(old.replace('std.', 'io.'), `use import ${group}`);
      add(`std.${group}.${old.replace(/^std\./, '')}`, `use import ${group}`);
    }
  }

  for (const [old, flat] of Object.entries(FLAT_KEY)) {
    if (old === flat || MERGE_GROUPS[flat]) continue;
    add(old, `use import ${flat}`);
    add(`core.${flat}`, `use import ${flat}`);
  }

  add('std.sec.crypto', 'use import crypto');
  add('sec.crypto', 'use import crypto');
  add('std.net.compress', 'use import network');
  add('net.compress', 'use import network');

  return [...new Set(lines)].sort().join('\n');
}

function generateResolverTail() {
  const hints = generateLegacyHints();
  return `
static const char* lookupLegacyImportHint(const std::string& path) {
  static const std::unordered_map<std::string, const char*> kHints = {
${hints}
  };
  auto it = kHints.find(path);
  return it == kHints.end() ? nullptr : it->second;
}

static void rejectLegacyStdlibImport(const std::string& import_path) {
  if (const char* hint = lookupLegacyImportHint(import_path)) {
    throw FarError(std::string("module '") + import_path + "' was removed; " + hint);
  }
  if (import_path.rfind("std.", 0) == 0 || import_path.rfind("core.", 0) == 0 ||
      import_path.rfind("io.", 0) == 0 || import_path.rfind("net.", 0) == 0 ||
      import_path.rfind("sci.", 0) == 0 || import_path.rfind("modern.", 0) == 0 ||
      import_path.rfind("dev.", 0) == 0 ||
      import_path.rfind("sec.", 0) == 0 || import_path.rfind("perf.", 0) == 0) {
    throw FarError("legacy import '" + import_path +
                   "' is not supported; use Python-style import (e.g. import math, import vectors, "
                   "import network)");
  }
}

static std::string normalizeStdlibImport(const std::string& import_path) {
  rejectLegacyStdlibImport(import_path);
  if (import_path.find('.') != std::string::npos) return "";
  if (import_path.empty()) return "";
  return import_path;
}

const char* lookupStdlibModuleSource(const std::string& import_path) {
  std::string primary = normalizeStdlibImport(import_path);
  if (primary.empty()) return nullptr;
  return primaryStdlibModuleSource(primary);
}

bool isStdlibModuleImport(const std::string& import_path) {
  return lookupStdlibModuleSource(import_path) != nullptr;
}

static const std::unordered_set<std::string>& functionStdlibModules() {
  static const std::unordered_set<std::string> k = {
      "bench", "cli", "compress", "crypto", "csv", "date", "env", "fs", "hash", "i18n", "json",
      "log",  "math", "net",  "proc",  "random", "regex", "test", "time", "xml", "yaml",
  };
  return k;
}

static const std::unordered_set<std::string>& typeStdlibModules() {
  static const std::unordered_set<std::string> k = {
      "vectors",   "matrices", "points",     "rects",   "quaternions", "colors", "bounds",
      "transforms", "network",  "io",        "science", "security",    "perf",   "dev",
  };
  return k;
}

bool isStdlibFunctionModule(const std::string& flat_name) {
  return functionStdlibModules().count(flat_name) > 0;
}

bool isStdlibTypeModule(const std::string& flat_name) {
  return typeStdlibModules().count(flat_name) > 0;
}

std::string stdlibModuleFlatName(const std::string& module_full_name) {
  const size_t dot = module_full_name.rfind('.');
  return dot == std::string::npos ? module_full_name : module_full_name.substr(dot + 1);
}

}  // namespace far
`;
}

function isGeomMapKey(key) {
  return key in GEOM_FLAT || /^std\.(bounds|color|dmat|dpoint|dquat|drect|dvec|point|rect|vec|ivec|mat|quat|transform)/.test(key);
}

function main() {
  let cpp = fs.readFileSync(cppPath, 'utf8');
  const geomMatch = cpp.match(new RegExp(`${GEOM_BEGIN}[\\s\\S]*?${GEOM_END}`));
  const geomBlock = geomMatch ? geomMatch[0] : '';

  const modules = parseModules(cpp);
  const oldMap = parseMap(cpp);

  const mergedFlat = new Set(['io', 'dev', 'network', 'science', 'security', 'perf']);
  const flatSingles = new Map();

  for (const { key, var: v } of oldMap) {
    if (isGeomMapKey(key)) continue;
    if (Object.values(MERGE_GROUPS).flat().includes(key)) continue;

    const src = modules.get(v);
    if (!src) continue;

    let flat = FLAT_KEY[key] ?? key.replace(/^std\./, '');
    if (flat === 'modern') flat = 'dev';
    if (['vectors', 'matrices', 'points', 'rects', 'quaternions', 'colors', 'bounds', 'transforms'].includes(flat)) continue;
    if (MERGE_GROUPS[flat] && !mergedFlat.has(flat)) continue;

    flatSingles.set(flat, retargetModule(src, flat));
  }

  const emitted = [];
  for (const [flat, src] of flatSingles) {
    emitted.push(emitModuleBlock(flat, src));
  }

  const mapEntries = [];
  for (const flat of [...flatSingles.keys()].sort()) {
    mapEntries.push(`    {"${flat}", ${cIdent(flat)}},`);
  }
  for (const g of ['vectors', 'matrices', 'points', 'rects', 'quaternions', 'colors', 'bounds', 'transforms']) {
    mapEntries.push(`    {"${g}", kStdlibMod_${g}},`);
  }

  const geomMapMatch = cpp.match(/\/\/ === GEOM_MODULE_MAP_BEGIN ===[\s\S]*?\/\/ === GEOM_MODULE_MAP_END ===/);
  let geomMapLines = '';
  if (geomMapMatch) {
    geomMapLines = geomMapMatch[0]
      .replace(/\/\/ === GEOM_MODULE_MAP_BEGIN ===\n?/, '')
      .replace(/\n?\/\/ === GEOM_MODULE_MAP_END ===/, '')
      .trim();
  }

  const header = cpp.slice(0, cpp.indexOf('static const char kStdlibMod_'));
  const footerStart = cpp.indexOf('static const char* primaryStdlibModuleSource');
  const oldFooter = cpp.slice(footerStart);

  let newCpp = header;
  newCpp += emitted.join('\n\n') + '\n\n';
  newCpp += geomBlock + '\n\n';
  newCpp += `static const char* primaryStdlibModuleSource(const std::string& primary) {
  static const std::unordered_map<std::string, const char*> map = {
${MAP_BEGIN}
${mapEntries.join('\n')}
${geomMapLines}
${MAP_END}
  };
  auto it = map.find(primary);
  return it == map.end() ? nullptr : it->second;
}
`;
  newCpp += generateResolverTail();

  fs.writeFileSync(cppPath, newCpp);
  console.log(`Flattened stdlib: ${flatSingles.size} modules (lowercase facades)`);
}

main();
