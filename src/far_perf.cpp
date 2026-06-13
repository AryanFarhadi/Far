#include "far_perf.h"

#include "error.h"
#include "type_desc.h"

#include <unordered_map>

namespace far {

namespace {

#define S PerfTy::Str
#define I PerfTy::I64
#define D PerfTy::F64
#define B PerfTy::Bool
#define V PerfTy::Void

#define P0(n, rt, r) \
  { n, rt, r, 0, {} }
#define P1(n, rt, r, a0) \
  { n, rt, r, 1, { a0 } }
#define P2(n, rt, r, a0, a1) \
  { n, rt, r, 2, { a0, a1 } }
#define P4(n, rt, r, a0, a1, a2, a3) \
  { n, rt, r, 4, { a0, a1, a2, a3 } }

static const PerfFn kPerf[] = {
    // Native compilation
    P0("native_target", "far_perf_native_target", S),
    P0("is_native", "far_perf_is_native", B),

    // LLVM backend
    P0("llvm_version", "far_perf_llvm_version", S),
    P0("llvm_opt_level", "far_perf_llvm_opt_level", I),

    // SIMD
    P0("simd_width", "far_perf_simd_width", I),
    P4("simd_add4", "far_perf_simd_add4", I, I, I, I, I),

    // Auto vectorization
    P0("vec_enabled", "far_perf_vec_enabled", B),
    P1("vec_hint", "far_perf_vec_hint", I, I),
    P4("vec_dot4", "far_perf_vec_dot4", I, I, I, I, I),

    // Multithreading
    P0("thread_count", "far_perf_thread_count", I),
    P0("current_thread", "far_perf_current_thread", I),

    // Incremental compilation
    P0("cache_generation", "far_perf_cache_generation", I),
    P0("cache_bump", "far_perf_cache_bump", I),
    P1("cache_stamp", "far_perf_cache_stamp", I, S),
    P2("cache_stale", "far_perf_cache_stale", B, S, I),

    // Fast startup
    P0("boot_time_ms", "far_perf_boot_time_ms", I),
    P0("runtime_ready", "far_perf_runtime_ready", B),
    P0("startup_elapsed", "far_perf_startup_elapsed", I),

    // Low memory usage
    P0("heap_kb", "far_perf_heap_kb", I),
    P0("peak_heap_kb", "far_perf_peak_heap_kb", I),

    // Predictable performance
    P0("mark_latency", "far_perf_mark_latency", I),
    P1("latency_ms", "far_perf_latency_ms", I, I),
    P2("jitter_ms", "far_perf_jitter_ms", I, I, I),
    P2("is_deterministic", "far_perf_is_deterministic", B, I, I),
};

#undef P0
#undef P1
#undef P2
#undef P4
#undef S
#undef I
#undef D
#undef B
#undef V

static FarTypeId perfToFar(PerfTy ty) {
  switch (ty) {
    case PerfTy::F64:
      return FarTypeId::F64;
    case PerfTy::Str:
      return FarTypeId::String;
    case PerfTy::Bool:
    case PerfTy::Void:
    case PerfTy::I64:
    default:
      return FarTypeId::I64;
  }
}

}  // namespace

const PerfFn* lookupPerf(const std::string& name) {
  static const std::unordered_map<std::string, const PerfFn*> map = [] {
    std::unordered_map<std::string, const PerfFn*> m;
    for (const auto& f : kPerf)
      m[f.name] = &f;
    return m;
  }();
  auto it = map.find(name);
  return it == map.end() ? nullptr : it->second;
}

std::string perfArgLlvm(PerfTy ty) {
  if (ty == PerfTy::F64)
    return "double";
  if (ty == PerfTy::Str)
    return "i8*";
  return "i64";
}

std::string perfRetLlvm(PerfTy ty) {
  if (ty == PerfTy::F64)
    return "double";
  if (ty == PerfTy::Str)
    return "i8*";
  return "i64";
}

void declarePerfRuntime(std::ostringstream& out) {
  for (const auto& f : kPerf) {
    std::ostringstream sig;
    sig << (f.ret == PerfTy::Void ? "void" : perfRetLlvm(f.ret)) << " @" << f.rt_name << "(";
    for (int i = 0; i < f.nargs; ++i) {
      if (i > 0)
        sig << ", ";
      sig << perfArgLlvm(f.args[i]);
    }
    sig << ")";
    out << "declare " << sig.str() << "\n";
  }
}

TypeDesc perfRetType(const PerfFn* fn) {
  if (fn->ret == PerfTy::Bool)
    return TypeDesc::prim(FarTypeId::I64);
  return TypeDesc::prim(perfToFar(fn->ret));
}

bool checkPerfArgs(const PerfFn* fn, const std::vector<CallArg>& args,
                   const std::function<TypeDesc(Expr&)>& typecheck_expr) {
  if (static_cast<int>(args.size()) != fn->nargs) {
    throw FarError(std::string(fn->name) + "() expects " + std::to_string(fn->nargs) + " argument(s)");
  }
  for (int i = 0; i < fn->nargs; ++i) {
    TypeDesc td = typecheck_expr(*args[static_cast<size_t>(i)].value);
    if (!isPrimitiveDesc(td))
      throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
    FarTypeId at = td.primitive;
    FarTypeId want = perfToFar(fn->args[i]);
    if (canAssign(at, want))
      continue;
    if (want == FarTypeId::F64 && (isIntegerType(at) || at == FarTypeId::F32))
      continue;
    throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
  }
  return true;
}

}  // namespace far
