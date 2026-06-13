#include "far_science.h"

#include "collections.h"
#include "error.h"
#include "type_desc.h"

#include <unordered_map>

namespace far {

namespace {

#define A ScienceTy::Arr
#define I ScienceTy::I64
#define D ScienceTy::F64
#define V ScienceTy::Void

#define S0(n, rt, r) \
  { n, rt, r, 0, {} }
#define S1(n, rt, r, a0) \
  { n, rt, r, 1, { a0 } }
#define S2(n, rt, r, a0, a1) \
  { n, rt, r, 2, { a0, a1 } }
#define S3(n, rt, r, a0, a1, a2) \
  { n, rt, r, 3, { a0, a1, a2 } }
#define S4(n, rt, r, a0, a1, a2, a3) \
  { n, rt, r, 4, { a0, a1, a2, a3 } }
#define S6(n, rt, r, a0, a1, a2, a3, a4, a5) \
  { n, rt, r, 6, { a0, a1, a2, a3, a4, a5 } }

static const ScienceFn kScience[] = {
    // statistics
    S1("sci_mean", "far_sci_mean", D, A),
    S1("sci_variance", "far_sci_variance", D, A),
    S1("sci_stddev", "far_sci_stddev", D, A),
    S1("sci_median", "far_sci_median", D, A),
    S2("sci_correlation", "far_sci_correlation", D, A, A),
    S1("sci_min", "far_sci_min", D, A),
    S1("sci_max", "far_sci_max", D, A),

    // fft
    S1("sci_fft", "far_sci_fft", A, A),
    S1("sci_ifft", "far_sci_ifft", A, A),

    // optimization
    S3("sci_gradient_descent", "far_sci_gradient_descent", D, D, D, D),
    S3("sci_parabola_min", "far_sci_parabola_min", D, D, D, D),

    // machine learning
    S1("sci_sigmoid", "far_sci_sigmoid", D, D),
    S1("sci_relu", "far_sci_relu", D, D),
    S1("sci_tanh", "far_sci_tanh", D, D),
    S1("sci_softmax", "far_sci_softmax", A, A),
    S2("sci_dot", "far_sci_dot", D, A, A),

    // numerical methods
    S2("sci_trapz", "far_sci_trapz", D, A, D),
    S2("sci_simpson", "far_sci_simpson", D, A, D),
    S1("sci_finite_diff", "far_sci_finite_diff", A, A),
    S2("sci_lerp_arr", "far_sci_lerp_arr", A, A, D),

    // physics
    S2("sci_kinetic_energy", "far_sci_kinetic_energy", D, D, D),
    S3("sci_potential_energy", "far_sci_potential_energy", D, D, D, D),
    S3("sci_gravitational_force", "far_sci_gravitational_force", D, D, D, D),
    S3("sci_projectile_range", "far_sci_projectile_range", D, D, D, D),  /* v0, angle, g */
    S2("sci_hooke_force", "far_sci_hooke_force", D, D, D),

    // 3d vector utilities (component form)
    S6("sci_v3_dot", "far_sci_v3_dot", D, D, D, D, D, D, D),
    S3("sci_v3_norm", "far_sci_v3_norm", D, D, D, D),

    // matrix utilities (2x2 row-major)
    S4("sci_mat2_det", "far_sci_mat2_det", D, D, D, D, D),
    S4("sci_mat2_trace", "far_sci_mat2_trace", D, D, D, D, D),
};

#undef S0
#undef S1
#undef S2
#undef S3
#undef S4
#undef S6
#undef A
#undef I
#undef D
#undef V

static FarTypeId scienceToFar(ScienceTy ty) {
  switch (ty) {
    case ScienceTy::F64:
      return FarTypeId::F64;
    case ScienceTy::Arr:
      return FarTypeId::Arr;
    case ScienceTy::Void:
    case ScienceTy::I64:
    default:
      return FarTypeId::I64;
  }
}

}  // namespace

const ScienceFn* lookupScience(const std::string& name) {
  static const std::unordered_map<std::string, const ScienceFn*> map = [] {
    std::unordered_map<std::string, const ScienceFn*> m;
    for (const auto& f : kScience)
      m[f.name] = &f;
    return m;
  }();
  auto it = map.find(name);
  return it == map.end() ? nullptr : it->second;
}

std::string scienceArgLlvm(ScienceTy ty) {
  if (ty == ScienceTy::F64)
    return "double";
  return "i64";
}

std::string scienceRetLlvm(ScienceTy ty) {
  if (ty == ScienceTy::F64)
    return "double";
  return "i64";
}

void declareScienceRuntime(std::ostringstream& out) {
  for (const auto& f : kScience) {
    std::ostringstream sig;
    sig << (f.ret == ScienceTy::Void ? "void" : scienceRetLlvm(f.ret)) << " @" << f.rt_name << "(";
    for (int i = 0; i < f.nargs; ++i) {
      if (i > 0)
        sig << ", ";
      sig << scienceArgLlvm(f.args[i]);
    }
    sig << ")";
    out << "declare " << sig.str() << "\n";
  }
}

TypeDesc scienceRetType(const ScienceFn* fn) {
  if (fn->ret == ScienceTy::Arr)
    return TypeDesc::prim(FarTypeId::Arr);
  if (fn->ret == ScienceTy::F64)
    return TypeDesc::prim(FarTypeId::F64);
  return TypeDesc::prim(FarTypeId::I64);
}

bool checkScienceArgs(const ScienceFn* fn, const std::vector<CallArg>& args,
                      const std::function<TypeDesc(Expr&)>& typecheck_expr) {
  if (static_cast<int>(args.size()) != fn->nargs) {
    throw FarError(std::string(fn->name) + "() expects " + std::to_string(fn->nargs) + " argument(s)");
  }
  for (int i = 0; i < fn->nargs; ++i) {
    TypeDesc td = typecheck_expr(*args[static_cast<size_t>(i)].value);
    ScienceTy expected = fn->args[i];
    if (expected == ScienceTy::Arr) {
      if ((isPrimitiveDesc(td) && td.primitive == FarTypeId::Arr) || td.form == TypeForm::Array ||
          isCollectionHandle(td))
        continue;
      throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
    }
    if (!isPrimitiveDesc(td))
      throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
    FarTypeId at = td.primitive;
    FarTypeId want = scienceToFar(expected);
    if (canAssign(at, want))
      continue;
    if (want == FarTypeId::F64 && (isIntegerType(at) || at == FarTypeId::F32))
      continue;
    throw FarError(std::string(fn->name) + "() argument " + std::to_string(i + 1) + " type mismatch");
  }
  return true;
}

}  // namespace far
