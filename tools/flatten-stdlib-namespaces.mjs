#!/usr/bin/env node
/**
 * Flatten nested stdlib import paths (std.net.client -> std.client) in far_stdlib_modules.cpp
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const cppPath = path.join(root, 'src', 'far_stdlib_modules.cpp');

const SPECIAL = {
  'std.net.compress': 'std.pack',
  'std.sec.crypto': 'std.secure',
};

const GROUPS = ['io', 'net', 'sci', 'modern', 'sec', 'perf'];

function flattenPath(primary) {
  if (SPECIAL[primary]) return SPECIAL[primary];
  for (const g of GROUPS) {
    const prefix = `std.${g}.`;
    if (primary.startsWith(prefix)) return `std.${primary.slice(prefix.length)}`;
  }
  return primary;
}

function legacyAliases(flat) {
  const out = new Set();
  for (const g of GROUPS) {
    const suffix = flat.slice(4);
    out.add(`std.${g}.${suffix}`);
    out.add(`${g}.${suffix}`);
  }
  if (flat === 'std.pack') {
    out.add('std.net.compress');
    out.add('net.compress');
  }
  if (flat === 'std.secure') {
    out.add('std.sec.crypto');
    out.add('sec.crypto');
  }
  return [...out].sort();
}

let cpp = fs.readFileSync(cppPath, 'utf8');

for (const g of GROUPS) {
  cpp = cpp.replaceAll(`package std.${g}\n`, 'package std\n');
}

const mapRe = /\{"([^"]+)",\s*(kStdlibMod_[^}]+)\}/g;
const renames = new Map();
let m;
while ((m = mapRe.exec(cpp)) !== null) {
  const oldKey = m[1];
  const flat = flattenPath(oldKey);
  if (flat !== oldKey) renames.set(oldKey, flat);
}

for (const [oldKey, flat] of renames) {
  cpp = cpp.replace(`{"${oldKey}",`, `{"${flat}",`);
}

const legacyEntries = [];
for (const [, flat] of renames) {
  for (const legacy of legacyAliases(flat)) {
    if (legacy === flat) continue;
    legacyEntries.push(`    {"${legacy}", "${flat}"},`);
  }
}
legacyEntries.sort();

const normalizeFn = `static std::string flattenLegacyStdlibPath(const std::string& path) {
  static const std::unordered_map<std::string, std::string> legacy = {
${legacyEntries.join('\n')}
  };
  auto it = legacy.find(path);
  return it == legacy.end() ? path : it->second;
}

static std::string normalizeStdlibImport(const std::string& import_path) {
  if (import_path.rfind("std.", 0) == 0) return flattenLegacyStdlibPath(import_path);
  if (import_path.rfind("core.", 0) == 0) return flattenLegacyStdlibPath("std." + import_path.substr(5));
  if (import_path.rfind("io.", 0) == 0) return flattenLegacyStdlibPath("std." + import_path);
  if (import_path.rfind("net.", 0) == 0) return flattenLegacyStdlibPath("std." + import_path);
  if (import_path.rfind("sci.", 0) == 0) return flattenLegacyStdlibPath("std." + import_path);
  if (import_path.rfind("modern.", 0) == 0) return flattenLegacyStdlibPath("std." + import_path);
  if (import_path.rfind("sec.", 0) == 0) return flattenLegacyStdlibPath("std." + import_path);
  if (import_path.rfind("perf.", 0) == 0) return flattenLegacyStdlibPath("std." + import_path);
  if (import_path.find('.') == std::string::npos && !import_path.empty()) return "std." + import_path;
  return "";
}`;

cpp = cpp.replace(
  /static std::string normalizeStdlibImport[\s\S]*?return "";\n\}/,
  normalizeFn
);

fs.writeFileSync(cppPath, cpp);
console.log(`Flattened ${renames.size} stdlib module paths in far_stdlib_modules.cpp`);
