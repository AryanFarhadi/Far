#!/usr/bin/env node
// Generates src/far_stdlib_modules.cpp and vscode/data/core-api.json from std/**/*.far
// If std/ is removed, edit far_stdlib_modules.cpp directly.
// Run from repo root: node tools/generate-stdlib-modules.mjs
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const stdDir = path.join(root, 'std');
const outCpp = path.join(root, 'src', 'far_stdlib_modules.cpp');
const apiJson = path.join(root, 'vscode', 'data', 'core-api.json');

const KEYWORDS = [
  'def', 'fn', 'import', 'export', 'package', 'module', 'if', 'else', 'while', 'for', 'return',
  'struct', 'class', 'print', 'true', 'false', 'and', 'or', 'not', 'match', 'spawn', 'parallel',
];
const TYPES = ['i64', 'f64', 'string', 'bool', 'void', 'vec2', 'vec3', 'arr'];
const BUILTINS = ['sin', 'cos', 'len', 'clamp_d', 'lerp', 'vec2', 'thread_count', 'pi'];

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
  // Nested dirs collapse to a single category word: std/net/client -> std.client
  return `std.${parts[parts.length - 1]}`;
}

function aliasPaths(primary) {
  const aliases = [primary];
  if (primary.startsWith('std.')) aliases.push('core.' + primary.slice(4));
  const suffix = primary.startsWith('std.') ? primary.slice(4) : '';
  if (suffix && !suffix.includes('.')) {
    for (const g of ['io', 'net', 'sci', 'modern', 'sec', 'perf']) {
      aliases.push(`${g}.${suffix}`);
      aliases.push(`std.${g}.${suffix}`);
    }
  }
  if (primary === 'std.pack') aliases.push('net.compress', 'std.net.compress');
  if (primary === 'std.secure') aliases.push('sec.crypto', 'std.sec.crypto');
  return [...new Set(aliases)];
}

function cIdent(s) {
  return 'kStdlibMod_' + s.replace(/[^a-zA-Z0-9]/g, '_');
}

function parseModuleMeta(source, moduleId) {
  const symbols = [];
  const pkg = source.match(/^package\s+(\S+)/m)?.[1] ?? 'std';
  const mod = source.match(/^module\s+(\S+)/m)?.[1] ?? moduleId.split('.').pop();
  const defRe = /^public\s+def\s+(\w+)\s*(\([^)]*\))?\s*(?:->\s*(\S+))?/gm;
  let m;
  while ((m = defRe.exec(source)) !== null) {
    symbols.push({
      name: m[1],
      signature: `def ${m[1]}${m[2] ?? '()'} -> ${m[3] ?? 'void'}`,
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
  return { id: moduleId, package: pkg, module: mod, symbols };
}

function main() {
  const files = walk(stdDir);
  if (!files.length) {
    console.error('No std/**/*.far found — edit src/far_stdlib_modules.cpp directly.');
    process.exit(1);
  }

  const pathToVar = new Map();
  const importPaths = new Set();
  const apiModules = {};

  for (const file of files) {
    const rel = path.relative(stdDir, file);
    const primary = primaryImportPath(rel);
    const source = fs.readFileSync(file, 'utf8');
    const varName = cIdent(primary);
    pathToVar.set(primary, { varName, source });
    apiModules[primary] = parseModuleMeta(source, primary);
    for (const p of aliasPaths(primary)) importPaths.add(p);
  }

  let cpp = `#include "far_stdlib.h"\n\n#include <unordered_map>\n\nnamespace far {\n\n`;
  for (const [primary, { varName, source }] of pathToVar) {
    cpp += `static const char ${varName}[] = R"FAR_STDLIB(${source})FAR_STDLIB";\n\n`;
  }

  cpp += `static const char* primaryStdlibModuleSource(const std::string& primary) {\n`;
  cpp += `  static const std::unordered_map<std::string, const char*> map = {\n`;
  for (const [primary, { varName }] of pathToVar) {
    cpp += `    {"${primary}", ${varName}},\n`;
  }
  cpp += `  };\n  auto it = map.find(primary);\n  return it == map.end() ? nullptr : it->second;\n}\n\n`;

  cpp += `static std::string flattenLegacyStdlibPath(const std::string& path) {\n`;
  cpp += `  if (path == "std.net.compress" || path == "net.compress") return "std.pack";\n`;
  cpp += `  if (path == "std.sec.crypto" || path == "sec.crypto") return "std.secure";\n`;
  cpp += `  static const char* groups[] = {"io", "net", "sci", "modern", "sec", "perf"};\n`;
  cpp += `  for (const char* g : groups) {\n`;
  cpp += `    std::string prefix = std::string("std.") + g + ".";\n`;
  cpp += `    if (path.rfind(prefix, 0) == 0) return "std." + path.substr(prefix.size());\n`;
  cpp += `  }\n`;
  cpp += `  return path;\n`;
  cpp += `}\n\n`;

  cpp += `static std::string normalizeStdlibImport(const std::string& import_path) {\n`;
  cpp += `  if (import_path.rfind("std.", 0) == 0) return flattenLegacyStdlibPath(import_path);\n`;
  cpp += `  if (import_path.rfind("core.", 0) == 0) return flattenLegacyStdlibPath("std." + import_path.substr(5));\n`;
  for (const prefix of ['io.', 'net.', 'sci.', 'modern.', 'sec.', 'perf.']) {
    cpp += `  if (import_path.rfind("${prefix}", 0) == 0) return flattenLegacyStdlibPath("std." + import_path);\n`;
  }
  cpp += `  if (import_path.find('.') == std::string::npos && !import_path.empty()) return "std." + import_path;\n`;
  cpp += `  return "";\n}\n\n`;

  cpp += `const char* lookupStdlibModuleSource(const std::string& import_path) {\n`;
  cpp += `  std::string primary = normalizeStdlibImport(import_path);\n`;
  cpp += `  if (primary.empty()) return nullptr;\n`;
  cpp += `  return primaryStdlibModuleSource(primary);\n`;
  cpp += `}\n\n`;
  cpp += `bool isStdlibModuleImport(const std::string& import_path) {\n`;
  cpp += `  return lookupStdlibModuleSource(import_path) != nullptr;\n`;
  cpp += `}\n\n`;
  cpp += `}  // namespace far\n`;

  fs.writeFileSync(outCpp, cpp);

  const api = {
    generatedAt: new Date().toISOString(),
    keywords: KEYWORDS,
    types: TYPES,
    builtins: BUILTINS,
    importPaths: [...importPaths].sort(),
    modules: apiModules,
  };
  fs.mkdirSync(path.dirname(apiJson), { recursive: true });
  fs.writeFileSync(apiJson, JSON.stringify(api, null, 2));

  console.log(`Embedded ${files.length} stdlib modules -> src/far_stdlib_modules.cpp`);
  console.log(`IntelliSense API -> vscode/data/core-api.json`);
}

main();
