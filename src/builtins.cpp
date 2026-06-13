#include "builtins.h"

#include "aggregate.h"
#include "error.h"

#include <sstream>
#include <unordered_map>

namespace far {

namespace {

#define D FarTypeId::F64
#define I FarTypeId::I32
#define L FarTypeId::I64
#define B FarTypeId::Bool
#define A FarTypeId::Arr
#define V2 FarTypeId::DVec2
#define R FarTypeId::DRect

#define B0(n, rt, r) \
  { n, rt, r, 0, {} }
#define B1(n, rt, r, a0) \
  { n, rt, r, 1, { a0 } }
#define B2(n, rt, r, a0, a1) \
  { n, rt, r, 2, { a0, a1 } }
#define B3(n, rt, r, a0, a1, a2) \
  { n, rt, r, 3, { a0, a1, a2 } }
#define B4(n, rt, r, a0, a1, a2, a3) \
  { n, rt, r, 4, { a0, a1, a2, a3 } }
#define B5(n, rt, r, a0, a1, a2, a3, a4) \
  { n, rt, r, 5, { a0, a1, a2, a3, a4 } }

static const BuiltinInfo kBuiltins[] = {
    // libm transcendentals
    B1("sin", "far_sin", D, D),
    B1("cos", "far_cos", D, D),
    B1("tan", "far_tan", D, D),
    B1("asin", "far_asin", D, D),
    B1("acos", "far_acos", D, D),
    B1("atan", "far_atan", D, D),
    B2("atan2", "far_atan2", D, D, D),
    B1("sinh", "far_sinh", D, D),
    B1("cosh", "far_cosh", D, D),
    B1("tanh", "far_tanh", D, D),
    B1("asinh", "far_asinh", D, D),
    B1("acosh", "far_acosh", D, D),
    B1("atanh", "far_atanh", D, D),
    B1("sqrt", "far_sqrt", D, D),
    B1("cbrt", "far_cbrt", D, D),
    B2("hypot", "far_hypot", D, D, D),
    B2("pow", "far_pow", D, D, D),
    B1("exp", "far_exp", D, D),
    B1("log", "far_log", D, D),
    B1("log10", "far_log10", D, D),
    B1("log2", "far_log2", D, D),
    B1("exp2", "far_exp2", D, D),
    B1("log1p", "far_log1p", D, D),
    B1("expm1", "far_expm1", D, D),
    B1("floor", "far_floor", D, D),
    B1("ceil", "far_ceil", D, D),
    B1("round", "far_round", D, D),
    B1("trunc", "far_trunc", D, D),
    B1("fabs", "far_fabs", D, D),
    B2("fmod", "far_fmod", D, D, D),
    B2("copysign", "far_copysign", D, D, D),

    // constants
    B0("pi", "far_pi", D),
    B0("e", "far_e", D),
    B0("tau", "far_tau", D),
    B0("phi", "far_phi", D),
    B0("sqrt2", "far_sqrt2", D),
    B0("sqrt3", "far_sqrt3", D),
    B0("ln2", "far_ln2", D),
    B0("ln10", "far_ln10", D),
    B0("deg_per_rad", "far_deg_per_rad", D),
    B0("rad_per_deg", "far_rad_per_deg", D),

    // integer math
    B2("imin", "far_imin", I, I, I),
    B2("imax", "far_imax", I, I, I),
    B3("imin3", "far_imin3", I, I, I, I),
    B3("imax3", "far_imax3", I, I, I, I),
    B1("iabs", "far_iabs", I, I),
    B1("isign", "far_isign", I, I),
    B3("clamp_i", "far_clamp_i", I, I, I, I),
    B1("is_even", "far_is_even", B, I),
    B1("is_odd", "far_is_odd", B, I),
    B2("mod_pos", "far_mod_pos", I, I, I),
    B2("gcd", "far_gcd", I, I, I),
    B2("lcm", "far_lcm", I, I, I),
    B1("factorial", "far_factorial", I, I),
    B2("binomial", "far_binomial", I, I, I),
    B1("isqrt", "far_isqrt", I, I),
    B2("ipow", "far_ipow", I, I, I),
    B2("sum_range", "far_sum_range", I, I, I),
    B2("sum_range_inclusive", "far_sum_range_inclusive", I, I, I),
    B2("product_range", "far_product_range", I, I, I),
    B1("fib", "far_fib", I, I),
    B1("fib_iter", "far_fib_iter", I, I),
    B1("twice", "far_twice", I, I),
    B1("thrice", "far_thrice", I, I),
    B1("quad", "far_quad", I, I),

    // trig helpers
    B1("deg_to_rad", "far_deg_to_rad", D, D),
    B1("rad_to_deg", "far_rad_to_deg", D, D),
    B1("sin_deg", "far_sin_deg", D, D),
    B1("cos_deg", "far_cos_deg", D, D),
    B1("tan_deg", "far_tan_deg", D, D),
    B1("asin_deg", "far_asin_deg", D, D),
    B1("acos_deg", "far_acos_deg", D, D),
    B1("atan_deg", "far_atan_deg", D, D),
    B2("atan2_deg", "far_atan2_deg", D, D, D),
    B1("normalize_rad", "far_normalize_rad", D, D),
    B1("normalize_deg", "far_normalize_deg", D, D),
    B1("sec", "far_sec", D, D),
    B1("csc", "far_csc", D, D),
    B1("cot", "far_cot", D, D),
    B4("haversine", "far_haversine", D, D, D, D, D),

    // real / double utilities
    B2("dmin", "far_dmin", D, D, D),
    B2("dmax", "far_dmax", D, D, D),
    B3("dmin3", "far_dmin3", D, D, D, D),
    B3("dmax3", "far_dmax3", D, D, D, D),
    B3("clamp_d", "far_clamp_d", D, D, D, D),
    B1("saturate", "far_saturate", D, D),
    B3("lerp", "far_lerp", D, D, D, D),
    B3("inv_lerp", "far_inv_lerp", D, D, D, D),
    B5("remap", "far_remap", D, D, D, D, D, D),
    B1("square", "far_square", D, D),
    B1("cube", "far_cube", D, D),
    B3("approx_eq", "far_approx_eq", B, D, D, D),
    B2("approx_zero", "far_approx_zero", B, D, D),
    B4("dist2", "far_dist2", D, D, D, D, D),
    B4("dist", "far_dist", D, D, D, D, D),
    B1("sign_d", "far_sign_d", D, D),
    B1("round_i", "far_round_i", I, D),
    B1("floor_i", "far_floor_i", I, D),
    B1("ceil_i", "far_ceil_i", I, D),
    B2("log_n", "far_log_n", D, D, D),
    B1("exp10", "far_exp10", D, D),
    B3("smoothstep", "far_smoothstep", D, D, D, D),
    B2("mean2", "far_mean2", D, D, D),
    B3("mean3", "far_mean3", D, D, D, D),
    B2("variance2", "far_variance2", D, D, D),
    B2("stddev2", "far_stddev2", D, D, D),

    // array statistics
    B1("arr_min", "far_arr_min", I, A),
    B1("arr_max", "far_arr_max", I, A),
    B1("arr_sum", "far_arr_sum", I, A),
    B1("arr_mean", "far_arr_mean", I, A),
    B2("arr_count", "far_arr_count", I, A, I),
    B2("arr_index_of", "far_arr_index_of", I, A, I),

    // geometry (dvec2 / drect)
    B3("vec2_lerp", "far_vec2_lerp", V2, V2, V2, D),
    B2("vec2_reflect", "far_vec2_reflect", V2, V2, V2),
    B1("vec2_angle", "far_vec2_angle", D, V2),
    B4("rect_from_xywh", "far_rect_from_xywh", R, D, D, D, D),
    B2("rect_union", "far_rect_union", R, R, R),
};

#undef B0
#undef B1
#undef B2
#undef B3
#undef B4
#undef B5
#undef D
#undef I
#undef L
#undef B
#undef A
#undef V2
#undef R

}  // namespace

const BuiltinInfo* lookupBuiltin(const std::string& name) {
  static const std::unordered_map<std::string, const BuiltinInfo*> map = [] {
    std::unordered_map<std::string, const BuiltinInfo*> m;
    for (const auto& b : kBuiltins)
      m[b.name] = &b;
    return m;
  }();
  auto it = map.find(name);
  if (it == map.end())
    return nullptr;
  return it->second;
}

std::string builtinArgLlvm(FarTypeId id) {
  if (isAggregateType(id)) {
    const AggregateMeta* m = aggregateMeta(id);
    return std::string(m ? m->llvm_name : "i64") + "*";
  }
  if (id == FarTypeId::F64)
    return "double";
  if (id == FarTypeId::F32)
    return "float";
  return "i64";
}

std::string builtinRetLlvm(const BuiltinInfo* b) {
  if (isAggregateType(b->ret))
    return "void";
  if (b->ret == FarTypeId::F64)
    return "double";
  if (b->ret == FarTypeId::F32)
    return "float";
  return "i64";
}

void declareBuiltins(std::ostringstream& out) {
  for (const auto& b : kBuiltins) {
    std::ostringstream sig;
    sig << builtinRetLlvm(&b) << " @" << b.rt_name << "(";
    for (int i = 0; i < b.nargs; ++i) {
      if (i > 0)
        sig << ", ";
      sig << builtinArgLlvm(b.args[i]);
    }
    if (isAggregateType(b.ret)) {
      if (b.nargs > 0)
        sig << ", ";
      sig << builtinArgLlvm(b.ret);
    }
    sig << ")";
    out << "declare " << sig.str() << "\n";
  }
}

bool checkBuiltinArgs(const BuiltinInfo* builtin, const std::vector<CallArg>& args,
                      const std::function<FarTypeId(Expr&)>& typecheck_expr) {
  if (static_cast<int>(args.size()) != builtin->nargs) {
    throw FarError(std::string(builtin->name) + "() expects " + std::to_string(builtin->nargs) +
                   " argument(s)");
  }
  for (int i = 0; i < builtin->nargs; ++i) {
    FarTypeId at = typecheck_expr(*args[static_cast<size_t>(i)].value);
    FarTypeId expected = builtin->args[i];
    if (canAssign(at, expected))
      continue;
    if (expected == FarTypeId::F64 && (isIntegerType(at) || at == FarTypeId::F32))
      continue;
    if (expected == FarTypeId::F32 && isIntegerType(at))
      continue;
    throw FarError(std::string(builtin->name) + "() argument " + std::to_string(i + 1) +
                   " type mismatch");
  }
  return true;
}

}  // namespace far
