#include "far_security.h"

#include "error.h"
#include "type_desc.h"

#include <unordered_map>

namespace far {

namespace {

#define S SecTy::Str
#define I SecTy::I64
#define D SecTy::F64
#define B SecTy::Bool
#define V SecTy::Void

#define X0(n, rt, r) \
  { n, rt, r, 0, {} }
#define X1(n, rt, r, a0) \
  { n, rt, r, 1, { a0 } }
#define X2(n, rt, r, a0, a1) \
  { n, rt, r, 2, { a0, a1 } }
#define X3(n, rt, r, a0, a1, a2) \
  { n, rt, r, 3, { a0, a1, a2 } }

static const SecFn kSec[] = {
    // Memory safety
    X1("mem_guard", "far_sec_mem_guard", I, I),
    X1("mem_valid", "far_sec_mem_valid", B, I),
    X1("mem_scrub", "far_sec_mem_scrub", I, I),
    X1("mem_size", "far_sec_mem_size", I, I),

    // Bounds checking
    X2("bounds_check", "far_sec_bounds_check", B, I, I),
    X3("bounds_slice", "far_sec_bounds_slice", B, I, I, I),
    X2("bounds_clamp", "far_sec_bounds_clamp", I, I, I),

    // Integer overflow
    X2("i64_add_safe", "far_sec_i64_add_safe", I, I, I),
    X2("i64_mul_safe", "far_sec_i64_mul_safe", I, I, I),
    X2("i64_sub_safe", "far_sec_i64_sub_safe", I, I, I),
    X0("i64_overflowed", "far_sec_i64_overflowed", B),

    // Safe concurrency
    X1("safe_lock", "far_sec_safe_lock", B, I),
    X1("safe_unlock", "far_sec_safe_unlock", B, I),
    X1("safe_try_lock", "far_sec_safe_try_lock", B, I),
    X1("safe_owned", "far_sec_safe_owned", B, I),

    // Permission system
    X2("perm_grant", "far_sec_perm_grant", I, I, I),
    X2("perm_revoke", "far_sec_perm_revoke", I, I, I),
    X2("perm_check", "far_sec_perm_check", B, I, I),
    X1("perm_bits", "far_sec_perm_bits", I, I),

    // Sandboxing
    X1("sandbox_enter", "far_sec_sandbox_enter", I, I),
    X0("sandbox_exit", "far_sec_sandbox_exit", I),
    X0("sandbox_active", "far_sec_sandbox_active", B),
    X1("sandbox_allow", "far_sec_sandbox_allow", I, S),
    X1("sandbox_can", "far_sec_sandbox_can", B, S),

    // Cryptography API
    X1("crypto_digest", "far_sec_crypto_digest", S, S),
    X2("crypto_encrypt", "far_sec_crypto_encrypt", S, S, S),
    X2("crypto_verify", "far_sec_crypto_verify", B, S, S),
    X1("crypto_token", "far_sec_crypto_token", S, I),
};

#undef X0
#undef X1
#undef X2
#undef X3
#undef S
#undef I
#undef D
#undef B
#undef V

static FarTypeId secToFar(SecTy ty) {
  switch (ty) {
    case SecTy::F64:
      return FarTypeId::F64;
    case SecTy::Str:
      return FarTypeId::String;
    case SecTy::Bool:
    case SecTy::Void:
    case SecTy::I64:
    default:
      return FarTypeId::I64;
  }
}

}  // namespace

const SecFn* lookupSec(const std::string& name) {
  static const std::unordered_map<std::string, const SecFn*> map = [] {
    std::unordered_map<std::string, const SecFn*> m;
    for (const auto& f : kSec)
      m[f.name] = &f;
    return m;
  }();
  auto it = map.find(name);
  return it == map.end() ? nullptr : it->second;
}

std::string secArgLlvm(SecTy ty) {
  if (ty == SecTy::F64)
    return "double";
  if (ty == SecTy::Str)
    return "i8*";
  return "i64";
}

std::string secRetLlvm(SecTy ty) {
  if (ty == SecTy::F64)
    return "double";
  if (ty == SecTy::Str)
    return "i8*";
  return "i64";
}

void declareSecRuntime(std::ostringstream& out) {
  for (const auto& f : kSec) {
    std::ostringstream sig;
    sig << (f.ret == SecTy::Void ? "void" : secRetLlvm(f.ret)) << " @" << f.rt_name << "(";
    for (int i = 0; i < f.nargs; ++i) {
      if (i > 0)
        sig << ", ";
      sig << secArgLlvm(f.args[i]);
    }
    sig << ")";
    out << "declare " << sig.str() << "\n";
  }
}

TypeDesc secRetType(const SecFn* fn) {
  if (fn->ret == SecTy::Bool)
    return TypeDesc::prim(FarTypeId::I64);
  return TypeDesc::prim(secToFar(fn->ret));
}

bool checkSecArgs(const SecFn* fn, const std::vector<CallArg>& args,
                  const std::function<TypeDesc(Expr&)>& typecheck_expr) {
  if (static_cast<int>(args.size()) != fn->nargs) {
    throw FarError(std::string(fn->name) + "() expects " + std::to_string(fn->nargs) + " argument(s)");
  }
  for (int i = 0; i < fn->nargs; ++i) {
    TypeDesc td = typecheck_expr(*args[static_cast<size_t>(i)].value);
    if (!isPrimitiveDesc(td))
      throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
    FarTypeId at = td.primitive;
    FarTypeId want = secToFar(fn->args[i]);
    if (canAssign(at, want))
      continue;
    if (want == FarTypeId::F64 && (isIntegerType(at) || at == FarTypeId::F32))
      continue;
    throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
  }
  return true;
}

}  // namespace far
