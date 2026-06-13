#include "far_modern.h"

#include "error.h"
#include "type_desc.h"

#include <unordered_map>

namespace far {

namespace {

#define S ModernTy::Str
#define I ModernTy::I64
#define D ModernTy::F64
#define B ModernTy::Bool
#define V ModernTy::Void

#define M0(n, rt, r) \
  { n, rt, r, 0, {} }
#define M1(n, rt, r, a0) \
  { n, rt, r, 1, { a0 } }
#define M2(n, rt, r, a0, a1) \
  { n, rt, r, 2, { a0, a1 } }
#define M3(n, rt, r, a0, a1, a2) \
  { n, rt, r, 3, { a0, a1, a2 } }

static const ModernFn kModern[] = {
    // Type inference
    M1("infer_kind", "far_mod_infer_kind", I, I),
    M1("infer_fields", "far_mod_infer_fields", I, I),
    M1("infer_label", "far_mod_infer_label", S, I),

    // Nullable safety
    M1("null_some", "far_mod_null_some", I, I),
    M0("null_none", "far_mod_null_none", I),
    M1("null_is_some", "far_mod_null_is_some", B, I),
    M2("null_unwrap_or", "far_mod_null_unwrap_or", I, I, I),
    M3("null_map_or", "far_mod_null_map_or", I, I, I, I),

    // Pattern matching
    M2("pat_eq", "far_mod_pat_eq", B, I, I),
    M0("pat_wildcard", "far_mod_pat_wildcard", I),
    M3("pat_in_range", "far_mod_pat_in_range", B, I, I, I),

    // Immutable variables
    M1("immut_seal", "far_mod_immut_seal", I, I),
    M1("immut_value", "far_mod_immut_value", I, I),
    M1("immut_is_sealed", "far_mod_immut_is_sealed", B, I),

    // Readonly types
    M1("readonly_wrap", "far_mod_readonly_wrap", I, I),
    M1("readonly_get", "far_mod_readonly_get", I, I),
    M1("readonly_is", "far_mod_readonly_is", B, I),

    // Hot reload
    M1("hot_mtime", "far_mod_hot_mtime", I, S),
    M2("hot_stale", "far_mod_hot_stale", B, S, I),

    // Live coding
    M0("live_generation", "far_mod_live_generation", I),
    M0("live_bump", "far_mod_live_bump", I),
    M1("live_tick", "far_mod_live_tick", B, I),

    // Package manager
    M1("pkg_read", "far_mod_pkg_read", S, S),
    M1("pkg_name", "far_mod_pkg_name", S, S),
    M1("pkg_version", "far_mod_pkg_version", S, S),

    // Dependency manager
    M1("dep_count", "far_mod_dep_count", I, S),
    M2("dep_at", "far_mod_dep_at", S, S, I),
    M2("dep_satisfies", "far_mod_dep_satisfies", B, S, S),

    // LSP support
    M1("lsp_hover", "far_mod_lsp_hover", S, S),
    M1("lsp_kind", "far_mod_lsp_kind", I, S),

    // Debugger
    M1("dbg_break", "far_mod_dbg_break", B, I),
    M1("dbg_is_break", "far_mod_dbg_is_break", B, I),
    M0("dbg_step", "far_mod_dbg_step", I),

    // Profiler
    M0("prof_start", "far_mod_prof_start", I),
    M1("prof_elapsed", "far_mod_prof_elapsed", I, I),
    M0("prof_mem_kb", "far_mod_prof_mem_kb", I),

    // Formatter
    M1("fmt_trim", "far_mod_fmt_trim", S, S),
    M2("fmt_indent", "far_mod_fmt_indent", S, S, I),

    // Linter
    M1("lint_valid_ident", "far_mod_lint_valid_ident", B, S),
    M1("lint_count_issues", "far_mod_lint_count_issues", I, S),

    // REPL
    M1("repl_eval", "far_mod_repl_eval", I, S),
    M1("repl_history_add", "far_mod_repl_history_add", I, S),
    M0("repl_history_count", "far_mod_repl_history_count", I),

    // Interactive shell
    M1("shell_prompt", "far_mod_shell_prompt", S, S),
    M1("shell_read", "far_mod_shell_read", S, S),
};

#undef M0
#undef M1
#undef M2
#undef M3
#undef S
#undef I
#undef D
#undef B
#undef V

static FarTypeId modernToFar(ModernTy ty) {
  switch (ty) {
    case ModernTy::F64:
      return FarTypeId::F64;
    case ModernTy::Str:
      return FarTypeId::String;
    case ModernTy::Bool:
    case ModernTy::Void:
    case ModernTy::I64:
    default:
      return FarTypeId::I64;
  }
}

}  // namespace

const ModernFn* lookupModern(const std::string& name) {
  static const std::unordered_map<std::string, const ModernFn*> map = [] {
    std::unordered_map<std::string, const ModernFn*> m;
    for (const auto& f : kModern)
      m[f.name] = &f;
    return m;
  }();
  auto it = map.find(name);
  return it == map.end() ? nullptr : it->second;
}

std::string modernArgLlvm(ModernTy ty) {
  if (ty == ModernTy::F64)
    return "double";
  if (ty == ModernTy::Str)
    return "i8*";
  return "i64";
}

std::string modernRetLlvm(ModernTy ty) {
  if (ty == ModernTy::F64)
    return "double";
  if (ty == ModernTy::Str)
    return "i8*";
  return "i64";
}

void declareModernRuntime(std::ostringstream& out) {
  for (const auto& f : kModern) {
    std::ostringstream sig;
    sig << (f.ret == ModernTy::Void ? "void" : modernRetLlvm(f.ret)) << " @" << f.rt_name << "(";
    for (int i = 0; i < f.nargs; ++i) {
      if (i > 0)
        sig << ", ";
      sig << modernArgLlvm(f.args[i]);
    }
    sig << ")";
    out << "declare " << sig.str() << "\n";
  }
}

TypeDesc modernRetType(const ModernFn* fn) {
  if (fn->ret == ModernTy::Bool)
    return TypeDesc::prim(FarTypeId::I64);
  return TypeDesc::prim(modernToFar(fn->ret));
}

bool checkModernArgs(const ModernFn* fn, const std::vector<CallArg>& args,
                     const std::function<TypeDesc(Expr&)>& typecheck_expr) {
  if (static_cast<int>(args.size()) != fn->nargs) {
    throw FarError(std::string(fn->name) + "() expects " + std::to_string(fn->nargs) + " argument(s)");
  }
  for (int i = 0; i < fn->nargs; ++i) {
    TypeDesc td = typecheck_expr(*args[static_cast<size_t>(i)].value);
    if (!isPrimitiveDesc(td))
      throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
    FarTypeId at = td.primitive;
    FarTypeId want = modernToFar(fn->args[i]);
    if (canAssign(at, want))
      continue;
    if (want == FarTypeId::F64 && (isIntegerType(at) || at == FarTypeId::F32))
      continue;
    throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
  }
  return true;
}

}  // namespace far
