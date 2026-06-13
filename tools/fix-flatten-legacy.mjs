import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const cppPath = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'src', 'far_stdlib_modules.cpp');
let cpp = fs.readFileSync(cppPath, 'utf8');

const replacement = `static std::string flattenLegacyStdlibPath(const std::string& path) {
  if (path == "std.net.compress" || path == "net.compress") return "std.pack";
  if (path == "std.sec.crypto" || path == "sec.crypto") return "std.secure";
  static const char* groups[] = {"io", "net", "sci", "modern", "sec", "perf"};
  for (const char* g : groups) {
    std::string prefix = std::string("std.") + g + ".";
    if (path.rfind(prefix, 0) == 0) return "std." + path.substr(prefix.size());
  }
  return path;
}`;

cpp = cpp.replace(/static std::string flattenLegacyStdlibPath[\s\S]*?\n\}/, replacement);
fs.writeFileSync(cppPath, cpp);
console.log('fixed flattenLegacyStdlibPath');
