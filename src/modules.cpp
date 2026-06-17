#include "modules.h"

#include "error.h"
#include "far_stdlib.h"
#include "functions.h"
#include "parser.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace far {

namespace fs = std::filesystem;

static constexpr std::size_t kMaxSourceBytes = 64u * 1024u * 1024u;

std::string moduleFullName(const Program& program) {
  if (program.package_name.empty())
    return program.module_name;
  if (program.module_name.empty())
    return program.package_name;
  return program.package_name + "." + program.module_name;
}

std::string dotPathToFilePath(const std::string& dot_path) {
  std::string out;
  for (size_t i = 0; i < dot_path.size(); ++i) {
    if (dot_path[i] == '.')
      out += '/';
    else
      out += dot_path[i];
  }
  return out;
}

bool packageMatches(const std::string& importer_pkg, const std::string& target_pkg) {
  if (importer_pkg.empty() || target_pkg.empty())
    return false;
  return importer_pkg == target_pkg || importer_pkg.rfind(target_pkg + ".", 0) == 0 ||
         target_pkg.rfind(importer_pkg + ".", 0) == 0;
}

bool canImportVisibility(Visibility vis, ImportKind kind, const std::string& importer_pkg,
                         const std::string& target_pkg) {
  if (vis == Visibility::Private)
    return false;
  if (vis == Visibility::Public)
    return true;
  if (vis == Visibility::Internal)
    return kind == ImportKind::Internal && packageMatches(importer_pkg, target_pkg);
  if (vis == Visibility::Protected)
    return (kind == ImportKind::Protected || kind == ImportKind::Internal) &&
           packageMatches(importer_pkg, target_pkg);
  return false;
}

static std::string readFileText(const std::string& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    throw FarError("cannot open import: " + path);
  std::streamsize size = in.tellg();
  if (size < 0)
    throw FarError("cannot read import size: " + path);
  if (static_cast<std::size_t>(size) > kMaxSourceBytes)
    throw FarError("import file exceeds maximum size (64 MiB): " + path);
  in.seekg(0, std::ios::beg);
  std::string content(static_cast<std::size_t>(size), '\0');
  if (!in.read(content.data(), size))
    throw FarError("cannot read import: " + path);
  return content;
}

struct ModuleSource {
  std::string cache_key;
  std::string text;
  std::string resolve_base;
};

static bool importRelPathSafe(const std::string& rel) {
  if (rel.empty())
    return false;
  if (rel.find('\\') != std::string::npos)
    return false;
  fs::path p = fs::path(rel).lexically_normal();
  if (p.is_absolute())
    return false;
  for (const auto& part : p) {
    if (part == "..")
      return false;
  }
  return true;
}

static bool importPathWithinRoot(const fs::path& root, const fs::path& candidate) {
  std::error_code ec;
  fs::path rel = fs::relative(candidate, root, ec);
  if (ec || rel.empty())
    return false;
  std::string s = rel.generic_string();
  return s.rfind("..", 0) != 0 && s.find("/..") == std::string::npos;
}

static std::optional<ModuleSource> resolveModuleSource(const std::string& import_path,
                                                       const fs::path& base_dir) {
  if (const char* embedded = lookupStdlibModuleSource(import_path)) {
    ModuleSource src;
    src.cache_key = std::string("std:") + import_path;
    src.text = embedded;
    src.resolve_base = base_dir.string();
    return src;
  }

  std::string rel = dotPathToFilePath(import_path);
  if (rel.size() >= 4 && rel.substr(rel.size() - 4) != ".far")
    rel += ".far";
  if (!importRelPathSafe(rel))
    return std::nullopt;

  struct ImportCandidate {
    fs::path root;
    fs::path path;
  };
  std::vector<ImportCandidate> candidates;
  candidates.push_back({fs::absolute(base_dir), base_dir / rel});
  candidates.push_back({fs::current_path(), fs::current_path() / rel});
  for (fs::path d = fs::absolute(base_dir); !d.empty() && d != d.root_path(); d = d.parent_path())
    candidates.push_back({d, d / rel});

  for (const auto& cand : candidates) {
    if (!fs::exists(cand.path))
      continue;
    fs::path abs = fs::absolute(cand.path).lexically_normal();
    fs::path root = cand.root.lexically_normal();
    if (!importPathWithinRoot(root, abs))
      continue;
    ModuleSource src;
    src.cache_key = abs.string();
    src.text = readFileText(src.cache_key);
    src.resolve_base = abs.parent_path().string();
    return src;
  }
  return std::nullopt;
}

static bool isExported(const Program& from, const std::string& symbol, Visibility vis, ImportKind kind) {
  if (kind == ImportKind::Internal && vis == Visibility::Internal)
    return true;
  if (kind == ImportKind::Protected && vis == Visibility::Protected)
    return true;
  if (!from.exports.empty()) {
    for (const auto& e : from.exports)
      if (e == symbol)
        return true;
    return false;
  }
  return vis == Visibility::Public;
}

static std::string importLocalName(const ImportDecl& imp, const std::string& symbol) {
  for (const auto& s : imp.symbols) {
    if (s.name == symbol)
      return s.alias.empty() ? s.name : s.alias;
  }
  return symbol;
}

static bool importWantsSymbol(const ImportDecl& imp, const std::string& symbol) {
  if (imp.symbols.empty())
    return true;
  for (const auto& s : imp.symbols)
    if (s.name == symbol)
      return true;
  return false;
}

static bool functionSignaturesEqual(const Function& a, const Function& b) {
  if (a.name != b.name)
    return false;
  if (a.params.size() != b.params.size())
    return false;
  for (size_t i = 0; i < a.params.size(); ++i) {
    if (a.params[i].is_variadic != b.params[i].is_variadic)
      return false;
    if (mangleType(a.params[i].type) != mangleType(b.params[i].type))
      return false;
  }
  return mangleType(a.return_type) == mangleType(b.return_type);
}

static bool isStdlibModuleName(const std::string& module_name) {
  return module_name.rfind("far.", 0) == 0;
}

static bool isStdlibTypeExport(const Program& from, const std::string& symbol) {
  for (const auto& td : from.user_types) {
    if (td.name == symbol)
      return true;
  }
  return false;
}

static std::string facadeClassName(const std::string& mod_name) {
  const size_t dot = mod_name.rfind('.');
  const std::string base = dot == std::string::npos ? mod_name : mod_name.substr(dot + 1);
  if (base.empty())
    return base;
  std::string out = base;
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

static void validateStdlibImport(const Program& from, const ImportDecl& imp, const std::string& mod_name) {
  if (!isStdlibModuleName(mod_name))
    return;
  if (imp.from_import) {
    const std::string flat = stdlibModuleFlatName(mod_name);
    throw FarError("stdlib module '" + flat +
                   "' uses Python-style import only (e.g. import " + flat + "); 'from ... import ...' is not "
                   "supported");
  }
  if (imp.symbols.empty())
    return;
  for (const auto& sym : imp.symbols) {
    const std::string& want = sym.alias.empty() ? sym.name : sym.name;
    if (isStdlibTypeExport(from, sym.name))
      continue;
    throw FarError("cannot import '" + want + "' from " + mod_name +
                   "; use import " + stdlibModuleFlatName(mod_name));
  }
}

static void bindFlatStdlibMethods(ModuleAlias& bind, const Program& into, const std::string& module_name,
                                  const std::string& flat_key) {
  const bool fn_mod = isStdlibFunctionModule(flat_key);
  const bool ty_mod = isStdlibTypeModule(flat_key);
  if (!fn_mod && !ty_mod)
    return;
  for (const auto& td : into.user_types) {
    if (td.module_name != module_name)
      continue;
    if (td.name != flat_key)
      continue;
    for (const auto& m : td.methods) {
      if (!m.is_static)
        continue;
      bind.flat_methods[m.name] = td.name;
    }
  }
}

static bool hasMatchingOverload(const Program& program, const Function& fn) {
  for (const auto& existing : program.functions) {
    if (functionSignaturesEqual(existing, fn))
      return true;
  }
  return false;
}

static bool importerOwnsSymbolName(const Program& into, const std::string& name) {
  for (const auto& fn : into.functions) {
    if (fn.name == name && fn.module_name.empty())
      return true;
  }
  for (const auto& td : into.user_types) {
    if (td.name == name && td.module_name.empty())
      return true;
  }
  return false;
}

static void ensureImportAlias(Program& into, const ImportDecl& imp, const std::string& module_name) {
  if (imp.alias.empty())
    return;
  if (importerOwnsSymbolName(into, imp.alias))
    throw FarError("import alias '" + imp.alias + "' shadows an existing symbol in this module");
  auto it = into.module_aliases.find(imp.alias);
  if (it != into.module_aliases.end() && it->second.module_name != module_name)
    throw FarError("import alias '" + imp.alias + "' already bound to another module");
}

static void mergeFunction(Program& into, Function fn, const ImportDecl& imp, const std::string& module_name) {
  if (isStdlibModuleName(module_name))
    return;
  if (!importWantsSymbol(imp, fn.name))
    return;
  if (hasMatchingOverload(into, fn))
    return;
  fn.name = importLocalName(imp, fn.name);
  fn.link_public = imp.alias.empty();
  if (!imp.alias.empty()) {
    ensureImportAlias(into, imp, module_name);
    into.module_aliases[imp.alias].module_name = module_name;
    into.module_aliases[imp.alias].symbols[fn.name] = fn.name;
  }
  into.functions.push_back(std::move(fn));
}

static bool hasMatchingType(const Program& program, const UserTypeDef& td,
                            const std::string& module_name) {
  for (const auto& t : program.user_types) {
    if (t.module_name == module_name && t.name == td.name)
      return true;
  }
  return false;
}

static void mergeType(Program& into, UserTypeDef td, const ImportDecl& imp, const std::string& module_name) {
  if (!importWantsSymbol(imp, td.name))
    return;
  const std::string local_name = importLocalName(imp, td.name);
  for (const auto& t : into.user_types) {
    if (t.module_name == module_name && t.name == local_name)
      return;
  }
  if (!imp.alias.empty() && !isStdlibTypeModule(stdlibModuleFlatName(module_name))) {
    ensureImportAlias(into, imp, module_name);
    into.module_aliases[imp.alias].module_name = module_name;
    into.module_aliases[imp.alias].symbols[local_name] = td.name;
  }
  td.name = local_name;
  into.user_types.push_back(std::move(td));
}

static void bindModuleAlias(Program& into, const ImportDecl& imp, const std::string& module_name) {
  if (imp.alias.empty())
    return;
  ensureImportAlias(into, imp, module_name);
  ModuleAlias& bind = into.module_aliases[imp.alias];
  bind.module_name = module_name;
  const std::string flat = stdlibModuleFlatName(module_name);
  for (const auto& td : into.user_types) {
    if (td.module_name != module_name)
      continue;
    if (!importWantsSymbol(imp, td.name))
      continue;
    if (isStdlibTypeModule(flat))
      continue;
    bind.symbols[td.name] = td.name;
  }
  bindFlatStdlibMethods(bind, into, module_name, flat);
  if (isStdlibModuleName(module_name))
    return;
  for (const auto& fn : into.functions) {
    if (fn.module_name != module_name)
      continue;
    if (!importWantsSymbol(imp, fn.name))
      continue;
    std::string local = importLocalName(imp, fn.name);
    bind.symbols[local] = fn.name;
  }
}

static void mergeExports(Program& into, Program& from, const ImportDecl& imp) {
  const std::string target_pkg = from.package_name;
  const std::string importer_pkg = into.package_name;
  const std::string mod_name = moduleFullName(from);

  validateStdlibImport(from, imp, mod_name);

  for (auto& fn : from.functions) {
    if (fn.name == "main")
      continue;
    if (!isExported(from, fn.name, fn.visibility, imp.kind))
      continue;
    if (!canImportVisibility(fn.visibility, imp.kind, importer_pkg, target_pkg))
      continue;
    fn.module_name = mod_name;
    mergeFunction(into, std::move(fn), imp, mod_name);
  }

  for (auto& td : from.user_types) {
    if (!isExported(from, td.name, td.visibility, imp.kind))
      continue;
    if (!canImportVisibility(td.visibility, imp.kind, importer_pkg, target_pkg))
      continue;
    td.module_name = mod_name;
    mergeType(into, std::move(td), imp, mod_name);
  }
  bindModuleAlias(into, imp, mod_name);

  if (isStdlibModuleName(mod_name))
    return;

  for (auto& fn : from.functions) {
    if (fn.name.empty() || fn.name == "main" || fn.visibility == Visibility::Private)
      continue;
    if (isExported(from, fn.name, fn.visibility, imp.kind))
      continue;
    if (hasMatchingOverload(into, fn))
      continue;
    fn.link_public = false;
    into.functions.push_back(std::move(fn));
  }
}

struct ImportJob {
  ImportDecl decl;
  std::string base;
};

static void resolveImportsInto(Program& program, const std::string& base_dir,
                               std::unordered_set<std::string>& seen_files,
                               std::unordered_map<std::string, std::string>& loaded_modules) {
  std::vector<ImportJob> jobs;
  for (const auto& imp : program.imports)
    jobs.push_back({imp, base_dir});
  program.imports.clear();

  for (size_t qi = 0; qi < jobs.size(); ++qi) {
    const ImportJob& job = jobs[qi];
    std::optional<ModuleSource> mod = resolveModuleSource(job.decl.path, job.base);
    if (!mod)
      throw FarError("module not found: " + job.decl.path);

    const std::string& cache_key = mod->cache_key;
    if (seen_files.count(cache_key)) {
      std::string virt_path = job.decl.path;
      for (char& ch : virt_path) {
        if (ch == '.')
          ch = '/';
      }
      virt_path += ".far";
      DiagnosticScope diag(std::move(virt_path), mod->text);
      Program imported = parseProgram(mod->text, false);
      resolveImportsInto(imported, mod->resolve_base, seen_files, loaded_modules);
      mergeExports(program, imported, job.decl);
      continue;
    }
    seen_files.insert(cache_key);

    std::string virt_path = job.decl.path;
    for (char& ch : virt_path) {
      if (ch == '.')
        ch = '/';
    }
    virt_path += ".far";
    DiagnosticScope diag(std::move(virt_path), mod->text);
    Program imported = parseProgram(mod->text, false);
    std::string mod_key = moduleFullName(imported);
    if (!mod_key.empty())
      loaded_modules[cache_key] = mod_key;

    resolveImportsInto(imported, mod->resolve_base, seen_files, loaded_modules);
    mergeExports(program, imported, job.decl);
  }
}

Program resolveImports(Program program, const std::string& base_dir) {
  std::unordered_set<std::string> seen_files;
  std::unordered_map<std::string, std::string> loaded_modules;
  resolveImportsInto(program, base_dir, seen_files, loaded_modules);
  return program;
}

}  // namespace far
