#include "typecheck.h"

#include <cmath>
#include <cstdint>

#include "aggregate.h"
#include "builtins.h"
#include "comptime.h"
#include "far_io.h"
#include "far_modern.h"
#include "far_perf.h"
#include "far_security.h"
#include "far_net.h"
#include "far_science.h"
#include "far_stdlib.h"
#include "collections.h"
#include "error.h"
#include "functions.h"
#include "memory.h"
#include "concurrency.h"
#include "errors.h"
#include "string_methods.h"
#include "generics.h"
#include "aggregate.h"
#include "geom_class.h"
#include "object_model.h"
#include "pattern.h"
#include "type_desc.h"
#include "types.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace far {

namespace {

static int g_pfor_id = 0;

static bool isGlobalFreeFunction(const std::string& name) {
  return name == "print" || name == "input" || name == "len";
}

static bool isCompilerIntrinsic(const std::string& name) {
  return name == "Range" || name == "range" || name == "reflect_kind" || name == "reflect_fields" ||
         name == "reflect_name" || name == "reflect_compile_value";
}

static bool allowLegacyRuntimeCall(const Function* fn) { return fn && fn->allow_public_builtins; }

static bool isLegacyGlobalOnly(const std::string& name) {
  return name == "thread_count" || name == "join" || name == "cores";
}

static std::string suggestClassMethod(const std::string& name) {
  static const std::unordered_map<std::string, std::string> kHints = {
      {"clamp_d", "math.clamp"},
      {"deg_to_rad", "math.rad"},
      {"rad_to_deg", "math.deg"},
      {"thread_count", "threads.count"},
      {"join", "threads.join"},
      {"cores", "threads.cores"},
  };
  auto it = kHints.find(name);
  if (it != kHints.end())
    return it->second;
  if (lookupBuiltin(name))
    return "math." + name;
  return {};
}

static void rejectDisallowedGlobalCall(const std::string& name, const Function* fn) {
  if (isGlobalFreeFunction(name) || isCompilerIntrinsic(name) || allowLegacyRuntimeCall(fn))
    return;
  const std::string hint = suggestClassMethod(name);
  if (!hint.empty())
    throw FarError("'" + name + "()' is not a global function; use " + hint + "()");
  throw FarError("'" + name + "()' is not a global function; import the stdlib facade class and use Class." +
                 name + "()");
}

static const UserMethod* lookupStaticMethod(const ObjectRegistry& reg, const UserTypeDef& td,
                                          const std::string& name) {
  for (const auto& m : td.methods) {
    if (m.is_static && m.name == name)
      return &m;
  }
  return nullptr;
}

static void materializeParallelForsInStmts(Program& program, std::vector<std::unique_ptr<Stmt>>& stmts,
                                         const std::string& ctx) {
  for (auto& s : stmts) {
    if (!s)
      continue;
    if (s->kind == Stmt::ForStmt && s->for_stmt.is_parallel) {
      Function lf;
      lf.name = "pfor$" + ctx + "$" + std::to_string(g_pfor_id++);
      Param p;
      p.name = s->for_stmt.parallel_var;
      p.type = TypeDesc::prim(FarTypeId::I64);
      lf.params.push_back(std::move(p));
      lf.return_type = TypeDesc::prim(FarTypeId::I64);
      lf.body = std::move(s->for_stmt.body);
      lf.llvm_name = mangleFunction(lf);
      s->for_stmt.parallel_fn = lf.llvm_name;
      program.synthetic_functions.push_back(std::move(lf));
      continue;
    }
    if (s->kind == Stmt::IfStmt) {
      for (auto& c : s->if_stmt.clauses)
        materializeParallelForsInStmts(program, c.body, ctx);
      materializeParallelForsInStmts(program, s->if_stmt.else_body, ctx);
    } else if (s->kind == Stmt::WhileStmt) {
      materializeParallelForsInStmts(program, s->while_stmt.body, ctx);
    } else if (s->kind == Stmt::ForStmt) {
      if (s->for_stmt.is_parallel || s->for_stmt.is_range) {
        materializeParallelForsInStmts(program, s->for_stmt.body, ctx);
        continue;
      }
      if (s->for_stmt.is_foreach) {
        materializeParallelForsInStmts(program, s->for_stmt.body, ctx);
        continue;
      }
      if (s->for_stmt.init) {
        std::vector<std::unique_ptr<Stmt>> tmp;
        tmp.push_back(std::move(s->for_stmt.init));
        materializeParallelForsInStmts(program, tmp, ctx);
        s->for_stmt.init = std::move(tmp[0]);
      }
      if (s->for_stmt.step) {
        std::vector<std::unique_ptr<Stmt>> tmp;
        tmp.push_back(std::move(s->for_stmt.step));
        materializeParallelForsInStmts(program, tmp, ctx);
        s->for_stmt.step = std::move(tmp[0]);
      }
      materializeParallelForsInStmts(program, s->for_stmt.body, ctx);
    } else if (s->kind == Stmt::UnsafeStmtK) {
      materializeParallelForsInStmts(program, s->unsafe.body, ctx);
    } else if (s->kind == Stmt::MatchStmtK) {
      for (auto& arm : s->match_stmt.arms)
        materializeParallelForsInStmts(program, arm.body, ctx);
    } else if (s->kind == Stmt::TryStmtK) {
      materializeParallelForsInStmts(program, s->try_stmt.try_body, ctx);
      materializeParallelForsInStmts(program, s->try_stmt.catch_body, ctx);
      materializeParallelForsInStmts(program, s->try_stmt.finally_body, ctx);
    }
  }
}

static void materializeParallelFors(Program& program) {
  g_pfor_id = 0;
  for (auto& fn : program.functions)
    materializeParallelForsInStmts(program, fn.body, fn.name);
}

static Function* findSyntheticByLlvm(Program& program, const std::string& llvm_name) {
  for (auto& f : program.synthetic_functions) {
    if (f.llvm_name == llvm_name)
      return &f;
  }
  return nullptr;
}

static bool isPrim(const TypeDesc& td, FarTypeId id) {
  return isPrimitiveDesc(td) && td.primitive == id;
}

static TypeDesc promoteNumeric(const TypeDesc& a, const TypeDesc& b) {
  if (!isPrimitiveDesc(a) || !isPrimitiveDesc(b))
    return defaultIntType();
  FarTypeId pa = a.primitive;
  FarTypeId pb = b.primitive;
  if (pa == FarTypeId::Bool || pb == FarTypeId::Bool) {
    if (pa == FarTypeId::Bool && pb == FarTypeId::Bool)
      return TypeDesc::prim(FarTypeId::Bool);
    return defaultIntType();
  }
  if (isFloatType(pa) || isFloatType(pb)) {
    if (pa == FarTypeId::F32 && pb == FarTypeId::F32)
      return TypeDesc::prim(FarTypeId::F32);
    return TypeDesc::prim(FarTypeId::F64);
  }
  if (pa == FarTypeId::I64 || pb == FarTypeId::I64 || pa == FarTypeId::U64 || pb == FarTypeId::U64 ||
      pa == FarTypeId::I128 || pb == FarTypeId::I128 || pa == FarTypeId::U128 || pb == FarTypeId::U128)
    return TypeDesc::prim(FarTypeId::I64);
  return defaultIntType();
}

static TypeDesc unifyTernaryBranches(const TypeDesc& a, const TypeDesc& b) {
  if (typeDescEquals(a, b))
    return a;
  if (!isPrimitiveDesc(a) || !isPrimitiveDesc(b))
    throw FarError("ternary branches must have compatible types");
  FarTypeId pa = a.primitive;
  FarTypeId pb = b.primitive;
  if (pa == FarTypeId::Bool || pb == FarTypeId::Bool) {
    if (pa == FarTypeId::Bool && pb == FarTypeId::Bool)
      return TypeDesc::prim(FarTypeId::Bool);
    throw FarError("ternary branches must have compatible types");
  }
  TypeDesc ty = promoteNumeric(a, b);
  if (!canAssignTypes(a, ty) || !canAssignTypes(b, ty))
    throw FarError("ternary branches must have compatible types");
  return ty;
}

enum class ConstBool { Unknown, False, True };

struct ConstLocalsState {
  std::unordered_map<std::string, int64_t> ints;
  std::unordered_map<std::string, double> floats;
};

static ConstBool constBoolFromExpr(const Expr& e, const ConstLocalsState& locals) {
  if (e.kind == Expr::Int)
    return e.int_lit.value != 0 ? ConstBool::True : ConstBool::False;
  if (e.kind == Expr::Float) {
    if (std::isnan(e.float_lit.value))
      return ConstBool::False;
    return e.float_lit.value != 0.0 ? ConstBool::True : ConstBool::False;
  }
  if (e.kind == Expr::String)
    return e.string_lit.value.empty() ? ConstBool::False : ConstBool::True;
  if (e.kind == Expr::Variable) {
    auto fit = locals.floats.find(e.var.name);
    if (fit != locals.floats.end()) {
      if (std::isnan(fit->second))
        return ConstBool::False;
      return fit->second != 0.0 ? ConstBool::True : ConstBool::False;
    }
    auto it = locals.ints.find(e.var.name);
    if (it != locals.ints.end())
      return it->second != 0 ? ConstBool::True : ConstBool::False;
    return ConstBool::Unknown;
  }
  if (e.kind == Expr::Binary && e.bin_op.op == "!" && e.bin_op.left->kind == Expr::Int &&
      e.bin_op.left->int_lit.value == 0) {
    ConstBool inner = constBoolFromExpr(*e.bin_op.right, locals);
    if (inner == ConstBool::Unknown)
      return ConstBool::Unknown;
    return inner == ConstBool::True ? ConstBool::False : ConstBool::True;
  }
  if (e.kind == Expr::Binary && (e.bin_op.op == "and" || e.bin_op.op == "&&")) {
    ConstBool l = constBoolFromExpr(*e.bin_op.left, locals);
    if (l == ConstBool::False)
      return ConstBool::False;
    if (l == ConstBool::True) {
      ConstBool r = constBoolFromExpr(*e.bin_op.right, locals);
      if (r == ConstBool::Unknown)
        return ConstBool::True;
      return r;
    }
    return ConstBool::Unknown;
  }
  if (e.kind == Expr::Binary && (e.bin_op.op == "or" || e.bin_op.op == "||")) {
    ConstBool l = constBoolFromExpr(*e.bin_op.left, locals);
    if (l == ConstBool::True)
      return ConstBool::True;
    if (l == ConstBool::False) {
      ConstBool r = constBoolFromExpr(*e.bin_op.right, locals);
      if (r == ConstBool::Unknown)
        return ConstBool::False;
      return r;
    }
    return ConstBool::Unknown;
  }
  return ConstBool::Unknown;
}

static bool exprConstNonNullish(const Expr& e, const ConstLocalsState& locals) {
  if (e.kind == Expr::Int)
    return e.int_lit.value != 0;
  if (e.kind == Expr::Float)
    return e.float_lit.value != 0.0 && !std::isnan(e.float_lit.value);
  if (e.kind == Expr::String)
    return !e.string_lit.value.empty();
  if (e.kind == Expr::Variable) {
    auto fit = locals.floats.find(e.var.name);
    if (fit != locals.floats.end())
      return fit->second != 0.0 && !std::isnan(fit->second);
    auto it = locals.ints.find(e.var.name);
    return it != locals.ints.end() && it->second != 0;
  }
  return false;
}

static std::optional<int64_t> constIntFromExprImpl(const Expr& e, const ConstLocalsState& locals,
                                                   int depth);

static std::optional<int64_t> constIntFromExpr(const Expr& e, const ConstLocalsState& locals) {
  return constIntFromExprImpl(e, locals, 0);
}

static std::optional<int64_t> constIntFromExprImpl(const Expr& e, const ConstLocalsState& locals,
                                                   int depth) {
  if (depth > 32)
    return std::nullopt;
  if (e.kind == Expr::Int)
    return e.int_lit.value;
  if (e.kind == Expr::Char)
    return static_cast<int64_t>(e.char_lit.value);
  if (e.kind == Expr::Variable) {
    auto it = locals.ints.find(e.var.name);
    if (it != locals.ints.end())
      return it->second;
  }
  if (e.kind == Expr::PrefixExprK) {
    if (e.prefix.op == "-") {
      if (auto v = constIntFromExprImpl(*e.prefix.operand, locals, depth + 1)) {
        if (*v == INT64_MIN)
          return std::nullopt;
        return -*v;
      }
    } else if (e.prefix.op == "~") {
      if (auto v = constIntFromExprImpl(*e.prefix.operand, locals, depth + 1))
        return ~*v;
    }
    return std::nullopt;
  }
  if (e.kind == Expr::Binary) {
    const std::string& op = e.bin_op.op;
    if (op == "!" && e.bin_op.left->kind == Expr::Int && e.bin_op.left->int_lit.value == 0) {
      if (auto v = constIntFromExprImpl(*e.bin_op.right, locals, depth + 1))
        return *v == 0 ? 1 : 0;
      return std::nullopt;
    }
    auto l = constIntFromExprImpl(*e.bin_op.left, locals, depth + 1);
    auto r = constIntFromExprImpl(*e.bin_op.right, locals, depth + 1);
    if (!l || !r)
      return std::nullopt;
    if (op == "+") {
#if defined(__GNUC__) || defined(__clang__)
      int64_t out;
      if (__builtin_add_overflow(*l, *r, &out))
        return std::nullopt;
      return out;
#else
      return *l + *r;
#endif
    }
    if (op == "-") {
#if defined(__GNUC__) || defined(__clang__)
      int64_t out;
      if (__builtin_sub_overflow(*l, *r, &out))
        return std::nullopt;
      return out;
#else
      return *l - *r;
#endif
    }
    if (op == "*") {
#if defined(__GNUC__) || defined(__clang__)
      int64_t out;
      if (__builtin_mul_overflow(*l, *r, &out))
        return std::nullopt;
      return out;
#else
      return *l * *r;
#endif
    }
    if (op == "/") {
      if (*r == 0)
        return std::nullopt;
      return *l / *r;
    }
    if (op == "%") {
      if (*r == 0)
        return std::nullopt;
      return *l % *r;
    }
    if (op == "&")
      return *l & *r;
    if (op == "|")
      return *l | *r;
    if (op == "^")
      return *l ^ *r;
    if (op == "<<") {
      if (*r < 0 || *r >= 64)
        return std::nullopt;
      return *l << *r;
    }
    if (op == ">>") {
      if (*r < 0 || *r >= 64)
        return std::nullopt;
      return *l >> *r;
    }
  }
  return std::nullopt;
}

static void checkLiteralShiftOverflow(const Expr& expr, const ConstLocalsState& locals) {
  if (expr.kind != Expr::Binary)
    return;
  const std::string& op = expr.bin_op.op;
  if (op != "<<" && op != ">>")
    return;
  auto l = constIntFromExprImpl(*expr.bin_op.left, locals, 0);
  auto r = constIntFromExprImpl(*expr.bin_op.right, locals, 0);
  if (!l || !r)
    return;
  if (*r < 0 || *r >= 64)
    throw FarError("shift out of range", expr.line, expr.col);
  if (op != "<<")
    return;
  uint64_t ul = static_cast<uint64_t>(*l);
  uint64_t ur = static_cast<uint64_t>(*r);
  if (ur > 0 && ul > (UINT64_MAX >> ur))
    throw FarError("integer overflow in shift expression", expr.line, expr.col);
  int64_t out = static_cast<int64_t>(ul << ur);
  TypeDesc lt = expr.bin_op.left->type;
  const bool unsigned_shift =
      isPrimitiveDesc(lt) && isIntegerType(lt.primitive) && !typeInfo(lt.primitive).is_signed;
  if (!unsigned_shift && *l >= 0 && out < 0)
    throw FarError("integer overflow in shift expression", expr.line, expr.col);
}

static std::optional<double> constFloatFromExprImpl(const Expr& e, const ConstLocalsState& locals,
                                                    int depth);

static std::optional<double> constFloatFromExpr(const Expr& e, const ConstLocalsState& locals) {
  return constFloatFromExprImpl(e, locals, 0);
}

static std::optional<double> constFloatFromExprImpl(const Expr& e, const ConstLocalsState& locals,
                                                    int depth) {
  if (depth > 32)
    return std::nullopt;
  if (e.kind == Expr::Float)
    return e.float_lit.value;
  if (e.kind == Expr::Variable) {
    auto it = locals.floats.find(e.var.name);
    if (it != locals.floats.end())
      return it->second;
    auto iit = locals.ints.find(e.var.name);
    if (iit != locals.ints.end())
      return static_cast<double>(iit->second);
  }
  if (auto i = constIntFromExprImpl(e, locals, depth))
    return static_cast<double>(*i);
  if (e.kind == Expr::PrefixExprK && e.prefix.op == "-") {
    if (auto v = constFloatFromExprImpl(*e.prefix.operand, locals, depth + 1))
      return -*v;
  }
  if (e.kind == Expr::Binary) {
    const std::string& op = e.bin_op.op;
    auto l = constFloatFromExprImpl(*e.bin_op.left, locals, depth + 1);
    auto r = constFloatFromExprImpl(*e.bin_op.right, locals, depth + 1);
    if (!l || !r)
      return std::nullopt;
    if (op == "+")
      return *l + *r;
    if (op == "-")
      return *l - *r;
    if (op == "*")
      return *l * *r;
    if (op == "/") {
      if (*r == 0.0)
        return std::nullopt;
      return *l / *r;
    }
  }
  return std::nullopt;
}

static bool literalPatternConstMatchesFloat(const Pattern& pat, double v) {
  if (pat.kind != PatKind::Literal)
    return false;
  if (pat.literal_is_float) {
    if (std::isnan(pat.float_literal))
      return std::isnan(v);
    return pat.float_literal == v;
  }
  if (std::isnan(v) || std::isinf(v))
    return false;
  return static_cast<double>(pat.literal) == v;
}

static bool literalPatternCertainlyFailsFloat(const Pattern& pat, double v) {
  if (pat.kind != PatKind::Literal)
    return false;
  if (pat.literal_is_float) {
    if (std::isnan(v))
      return !std::isnan(pat.float_literal);
    if (std::isnan(pat.float_literal))
      return true;
    return pat.float_literal != v;
  }
  if (std::isnan(v) || std::isinf(v))
    return true;
  return static_cast<double>(pat.literal) != v;
}

static bool patternCertainlyFailsConstFloat(const Pattern& pat, double v) {
  return literalPatternCertainlyFailsFloat(pat, v);
}

static bool patternMatchesConstFloatSpecific(const Pattern& pat, double v) {
  return literalPatternConstMatchesFloat(pat, v);
}

static bool isCatchAllPattern(const Pattern& pat) {
  return pat.kind == PatKind::Wildcard || pat.kind == PatKind::Bind;
}

static bool isMatchWildcardPattern(const Pattern& pat) {
  return pat.kind == PatKind::Wildcard;
}

static bool patternMatchesConstFloat(const Pattern& pat, double v) {
  if (patternMatchesConstFloatSpecific(pat, v))
    return true;
  if (isCatchAllPattern(pat))
    return true;
  return false;
}

static std::optional<int64_t> enumPatternConstValue(const Pattern& pat,
                                                   const ObjectRegistry& reg) {
  if (pat.kind != PatKind::EnumVariant)
    return std::nullopt;
  if (pat.variant_value >= 0)
    return pat.variant_value;
  int v = reg.enumVariantValue(pat.type_name, pat.variant);
  if (v < 0)
    return std::nullopt;
  return static_cast<int64_t>(v);
}

static bool patternCertainlyFailsConstInt(const Pattern& pat, int64_t v,
                                          const ObjectRegistry& reg) {
  if (pat.kind == PatKind::Literal) {
    if (pat.literal_is_float)
      return false;
    return pat.literal != v;
  }
  if (pat.kind == PatKind::EnumVariant) {
    auto pv = enumPatternConstValue(pat, reg);
    if (!pv)
      return false;
    return *pv != v;
  }
  return false;
}

static bool patternMatchesConstIntSpecific(const Pattern& pat, int64_t v,
                                             const ObjectRegistry& reg) {
  if (pat.kind == PatKind::Literal) {
    if (pat.literal_is_float)
      return false;
    return pat.literal == v;
  }
  if (pat.kind == PatKind::EnumVariant) {
    auto pv = enumPatternConstValue(pat, reg);
    return pv && *pv == v;
  }
  return false;
}

static bool patternMatchesConstInt(const Pattern& pat, int64_t v, const ObjectRegistry& reg) {
  if (patternMatchesConstIntSpecific(pat, v, reg))
    return true;
  if (isCatchAllPattern(pat))
    return true;
  return false;
}

static bool rangeForConstEmpty(int64_t start, int64_t end, bool exclusive) {
  return exclusive ? (start >= end) : (start > end);
}

static std::optional<int64_t> rangeForConstSampleValue(int64_t start, int64_t end, bool exclusive) {
  if (rangeForConstEmpty(start, end, exclusive))
    return std::nullopt;
  if (start <= end)
    return exclusive ? end - 1 : end;
  return exclusive ? end + 1 : end;
}

static std::optional<int64_t> constForeachSampleElem(const Expr& iter,
                                                       const ConstLocalsState& locals,
                                                       const Program* program) {
  if (iter.kind == Expr::ArrayLitExpr && !iter.array_lit.elements.empty())
    return constIntFromExpr(*iter.array_lit.elements.front(), locals);
  if (program && iter.kind == Expr::Variable) {
    for (const auto& stmt : program->comptime_stmts) {
      if (stmt->kind != Stmt::LetStmt || !stmt->let.is_constexpr || stmt->let.name != iter.var.name ||
          !stmt->let.value)
        continue;
      if (stmt->let.value->kind == Expr::ArrayLitExpr &&
          !stmt->let.value->array_lit.elements.empty())
        return constIntFromExpr(*stmt->let.value->array_lit.elements.front(), locals);
    }
  }
  return std::nullopt;
}

static std::optional<int64_t> tryBodyThrowLiteral(const std::vector<std::unique_ptr<Stmt>>& body,
                                                    const ConstLocalsState& locals) {
  for (const auto& s : body) {
    if (!s)
      continue;
    if (s->kind == Stmt::ThrowStmtK && s->throw_stmt.value)
      return constIntFromExpr(*s->throw_stmt.value, locals);
  }
  return std::nullopt;
}

static bool isEmptyConstexprLiteral(const Expr& e) {
  if (e.kind == Expr::String)
    return e.string_lit.value.empty();
  if (e.kind == Expr::ArrayLitExpr)
    return e.array_lit.elements.empty();
  return false;
}

static void rejectIntLiteralOutOfRangeForTarget(const Expr& expr, const TypeDesc& target) {
  if (!isPrimitiveDesc(target) || !isIntegerType(target.primitive))
    return;
  if (!intLiteralExprFitsType(expr, target.primitive))
    throw FarError("integer literal out of range for type " + typeDescName(target));
}

static void rejectNarrowingAssign(const TypeDesc& from, const TypeDesc& to) {
  if (!isPrimitiveDesc(from) || !isPrimitiveDesc(to))
    return;
  if (isNarrowingIntegerAssign(from.primitive, to.primitive))
    throw FarError("implicit narrowing from " + typeDescName(from) + " to " + typeDescName(to) +
                   "; use an explicit cast");
  if (isFloatType(from.primitive) && isIntegerType(to.primitive))
    throw FarError("implicit narrowing from " + typeDescName(from) + " to " + typeDescName(to) +
                   "; use an explicit cast");
}

static void rejectNarrowingStore(const TypeDesc& from, const TypeDesc& to) {
  rejectNarrowingAssign(from, to);
  if (!isPrimitiveDesc(from) || !isPrimitiveDesc(to))
    return;
  if (isFloatType(from.primitive) && isFloatType(to.primitive) &&
      typeInfo(to.primitive).bits < typeInfo(from.primitive).bits)
    throw FarError("implicit narrowing from " + typeDescName(from) + " to " + typeDescName(to) +
                   "; use an explicit cast");
}

static void rejectNarrowingCallArg(const Expr& arg, const TypeDesc& arg_ty, const TypeDesc& param_ty) {
  rejectIntLiteralOutOfRangeForTarget(arg, param_ty);
  rejectNarrowingAssign(arg_ty, param_ty);
}

static bool canWidenInferredIntegerLocal(const TypeDesc& from, const TypeDesc& to) {
  if (!isPrimitiveDesc(from) || !isPrimitiveDesc(to))
    return false;
  if (!isIntegerType(from.primitive) || !isIntegerType(to.primitive))
    return false;
  return typeInfo(from.primitive).bits > typeInfo(to.primitive).bits;
}

static void maybeWidenInferredLocal(std::unordered_map<std::string, TypeDesc>& locals,
                                    const std::unordered_set<std::string>& explicit_locals,
                                    const std::string& name, const TypeDesc& from,
                                    TypeDesc& target_ty) {
  if (explicit_locals.count(name) || !locals.count(name))
    return;
  if (!canWidenInferredIntegerLocal(from, target_ty))
    return;
  locals[name] = from;
  target_ty = from;
}

static std::optional<std::string> enclosingMethodSuffix(const Function* fn) {
  if (!fn)
    return std::nullopt;
  const size_t dollar = fn->name.find('$');
  if (dollar == std::string::npos || dollar + 1 >= fn->name.size())
    return std::nullopt;
  return fn->name.substr(dollar + 1);
}

static bool foreachConstEmpty(const Expr& iter, const Program* program = nullptr,
                              const std::unordered_set<std::string>* local_empty = nullptr) {
  if (iter.kind == Expr::ArrayLitExpr)
    return iter.array_lit.elements.empty();
  if (iter.kind == Expr::String)
    return iter.string_lit.value.empty();
  if (local_empty && iter.kind == Expr::Variable && local_empty->count(iter.var.name))
    return true;
  if (program && iter.kind == Expr::Variable) {
    for (const auto& stmt : program->comptime_stmts) {
      if (stmt->kind != Stmt::LetStmt || !stmt->let.is_constexpr || stmt->let.name != iter.var.name ||
          !stmt->let.value)
        continue;
      if (stmt->let.value->kind == Expr::String)
        return stmt->let.value->string_lit.value.empty();
      if (stmt->let.value->kind == Expr::ArrayLitExpr)
        return stmt->let.value->array_lit.elements.empty();
    }
  }
  return false;
}

static bool containsThrowStmt(const Stmt& stmt) {
  switch (stmt.kind) {
    case Stmt::ThrowStmtK:
      return true;
    case Stmt::IfStmt:
      for (const auto& c : stmt.if_stmt.clauses) {
        for (const auto& s : c.body)
          if (containsThrowStmt(*s))
            return true;
      }
      for (const auto& s : stmt.if_stmt.else_body)
        if (containsThrowStmt(*s))
          return true;
      return false;
    case Stmt::WhileStmt:
      for (const auto& s : stmt.while_stmt.body)
        if (containsThrowStmt(*s))
          return true;
      return false;
    case Stmt::ForStmt:
      for (const auto& s : stmt.for_stmt.body)
        if (containsThrowStmt(*s))
          return true;
      return false;
    case Stmt::TryStmtK:
      for (const auto& s : stmt.try_stmt.try_body)
        if (containsThrowStmt(*s))
          return true;
      for (const auto& s : stmt.try_stmt.catch_body)
        if (containsThrowStmt(*s))
          return true;
      for (const auto& s : stmt.try_stmt.finally_body)
        if (containsThrowStmt(*s))
          return true;
      return false;
    case Stmt::MatchStmtK:
      for (const auto& arm : stmt.match_stmt.arms)
        for (const auto& s : arm.body)
          if (containsThrowStmt(*s))
            return true;
      return false;
    case Stmt::UnsafeStmtK:
      for (const auto& s : stmt.unsafe.body)
        if (containsThrowStmt(*s))
          return true;
      return false;
    default:
      return false;
  }
}

static bool stmtBlockAlwaysReturns(const std::vector<std::unique_ptr<Stmt>>& stmts,
                                   const ConstLocalsState& locals, const ObjectRegistry& reg);

static bool stmtAlwaysReturns(const Stmt& stmt, const ConstLocalsState& locals,
                              const ObjectRegistry& reg) {
  switch (stmt.kind) {
    case Stmt::ReturnStmt:
      return true;
    case Stmt::IfStmt: {
      bool chain_matched = false;
      for (const auto& c : stmt.if_stmt.clauses) {
        if (chain_matched)
          break;
        ConstBool cb = constBoolFromExpr(*c.condition, locals);
        if (cb == ConstBool::False)
          continue;
        if (!stmtBlockAlwaysReturns(c.body, locals, reg))
          return false;
        if (cb == ConstBool::True) {
          chain_matched = true;
          return true;
        }
      }
      if (chain_matched)
        return true;
      if (stmt.if_stmt.else_body.empty())
        return false;
      return stmtBlockAlwaysReturns(stmt.if_stmt.else_body, locals, reg);
    }
    case Stmt::MatchStmtK: {
      std::optional<double> const_scrut_f =
          constFloatFromExpr(*stmt.match_stmt.scrutinee, locals);
      if (const_scrut_f) {
        for (const auto& arm : stmt.match_stmt.arms) {
          if (arm.pat && isMatchWildcardPattern(*arm.pat))
            continue;
          if (arm.pat && patternCertainlyFailsConstFloat(*arm.pat, *const_scrut_f))
            continue;
          if (arm.pat && patternMatchesConstFloat(*arm.pat, *const_scrut_f))
            return stmtBlockAlwaysReturns(arm.body, locals, reg);
        }
        for (const auto& arm : stmt.match_stmt.arms) {
          if (arm.pat && isMatchWildcardPattern(*arm.pat))
            return stmtBlockAlwaysReturns(arm.body, locals, reg);
        }
        return false;
      }
      std::optional<int64_t> const_scrut =
          constIntFromExpr(*stmt.match_stmt.scrutinee, locals);
      if (!const_scrut)
        return false;
      for (const auto& arm : stmt.match_stmt.arms) {
        if (arm.pat && isMatchWildcardPattern(*arm.pat))
          continue;
        if (arm.pat && patternCertainlyFailsConstInt(*arm.pat, *const_scrut, reg))
          continue;
        if (arm.pat && patternMatchesConstInt(*arm.pat, *const_scrut, reg))
          return stmtBlockAlwaysReturns(arm.body, locals, reg);
      }
      for (const auto& arm : stmt.match_stmt.arms) {
        if (arm.pat && isMatchWildcardPattern(*arm.pat))
          return stmtBlockAlwaysReturns(arm.body, locals, reg);
      }
      return false;
    }
    default:
      return false;
  }
}

static bool stmtBlockAlwaysReturns(const std::vector<std::unique_ptr<Stmt>>& stmts,
                                   const ConstLocalsState& locals, const ObjectRegistry& reg) {
  for (const auto& s : stmts) {
    if (stmtAlwaysReturns(*s, locals, reg))
      return true;
  }
  return false;
}

static bool tryCatchUnreachable(const std::vector<std::unique_ptr<Stmt>>& try_body,
                                const ConstLocalsState& locals, const ObjectRegistry& reg) {
  for (const auto& s : try_body) {
    if (containsThrowStmt(*s))
      return false;
  }
  return stmtBlockAlwaysReturns(try_body, locals, reg);
}

static bool tryBodyMayThrow(const std::vector<std::unique_ptr<Stmt>>& try_body) {
  for (const auto& s : try_body) {
    if (containsThrowStmt(*s))
      return true;
  }
  return false;
}

static bool methodArgMatches(TypeDesc arg, TypeDesc expected, TypeDesc recv) {
  if (canAssignTypes(arg, expected))
    return true;
  if (!isPrimitiveDesc(arg) || !isPrimitiveDesc(expected) || !isPrimitiveDesc(recv))
    return false;
  FarTypeId a = arg.primitive;
  FarTypeId e = expected.primitive;
  FarTypeId r = recv.primitive;
  if (isAggregateType(e) && isAggregateType(a) && a == e)
    return true;
  if (isVecFamily(e) && isVecFamily(a) && aggregateScalar(a) == aggregateScalar(r) &&
      aggregateDim(a) == aggregateDim(e))
    return true;
  if (isPointFamily(e) && isPointFamily(a) && aggregateScalar(a) == aggregateScalar(r))
    return true;
  if (isRectFamily(e) && isRectFamily(a) && aggregateScalar(a) == aggregateScalar(r))
    return true;
  if (isIVecFamily(e) && isIVecFamily(a) && aggregateDim(a) == aggregateDim(e))
    return true;
  if (isMatFamily(e) && isMatFamily(a) && a == e)
    return true;
  if (isQuatFamily(e) && isQuatFamily(a) && a == e)
    return true;
  if (isBoundsFamily(r) && isVecFamily(a) && a == FarTypeId::FVec3)
    return true;
  if (isRectFamily(r) && isVecFamily(a) && a == FarTypeId::FVec2)
    return true;
  if (isMatFamily(r) && isVecFamily(a) && aggregateMatDim(r) == aggregateDim(a) &&
      aggregateScalar(r) == aggregateScalar(a))
    return true;
  return false;
}

[[noreturn]] static void throwAt(const Expr& expr, const std::string& msg) {
  throw FarError(msg, expr.line, expr.col);
}

static void collectFreeVarsExpr(const Expr& expr, std::unordered_set<std::string>& out);
static void collectFreeVarsStmt(const Stmt& stmt, std::unordered_set<std::string>& out);

static void collectFreeVarsExpr(const Expr& expr, std::unordered_set<std::string>& out) {
  switch (expr.kind) {
    case Expr::Variable:
      out.insert(expr.var.name);
      break;
    case Expr::Binary:
      collectFreeVarsExpr(*expr.bin_op.left, out);
      collectFreeVarsExpr(*expr.bin_op.right, out);
      break;
    case Expr::FnCall:
      for (const auto& a : expr.call.args)
        collectFreeVarsExpr(*a.value, out);
      break;
    case Expr::CastExpr:
      collectFreeVarsExpr(*expr.cast.value, out);
      break;
    case Expr::IndexExpr:
      collectFreeVarsExpr(*expr.index.array, out);
      collectFreeVarsExpr(*expr.index.index, out);
      break;
    case Expr::SliceExpr:
      collectFreeVarsExpr(*expr.slice.array, out);
      if (expr.slice.start)
        collectFreeVarsExpr(*expr.slice.start, out);
      if (expr.slice.end)
        collectFreeVarsExpr(*expr.slice.end, out);
      break;
    case Expr::ArrayLitExpr:
      for (const auto& el : expr.array_lit.elements)
        collectFreeVarsExpr(*el, out);
      break;
    case Expr::DictLitExpr:
      for (const auto& entry : expr.dict_lit.entries) {
        collectFreeVarsExpr(*entry.key, out);
        collectFreeVarsExpr(*entry.value, out);
      }
      break;
    case Expr::MemberExpr:
      collectFreeVarsExpr(*expr.member.object, out);
      break;
    case Expr::MethodExpr:
      collectFreeVarsExpr(*expr.method_call.object, out);
      for (const auto& a : expr.method_call.args)
        collectFreeVarsExpr(*a, out);
      break;
    case Expr::FnLitExpr:
      break;
    case Expr::AwaitExprK:
      collectFreeVarsExpr(*expr.await.value, out);
      break;
    default:
      break;
  }
}

static void collectFreeVarsStmt(const Stmt& stmt, std::unordered_set<std::string>& out) {
  switch (stmt.kind) {
    case Stmt::LetStmt:
      collectFreeVarsExpr(*stmt.let.value, out);
      break;
    case Stmt::ReturnStmt:
      if (stmt.ret.has_value)
        collectFreeVarsExpr(*stmt.ret.value, out);
      break;
    case Stmt::YieldStmtK:
      if (stmt.yield.has_value)
        collectFreeVarsExpr(*stmt.yield.value, out);
      break;
    case Stmt::ExprStmtK:
      collectFreeVarsExpr(*stmt.expr_stmt.expr, out);
      break;
    case Stmt::PrintStmt:
      collectFreeVarsExpr(*stmt.print.value, out);
      break;
    case Stmt::IfStmt:
      for (const auto& c : stmt.if_stmt.clauses) {
        collectFreeVarsExpr(*c.condition, out);
        for (const auto& s : c.body)
          collectFreeVarsStmt(*s, out);
      }
      for (const auto& s : stmt.if_stmt.else_body)
        collectFreeVarsStmt(*s, out);
      break;
    case Stmt::WhileStmt:
      collectFreeVarsExpr(*stmt.while_stmt.condition, out);
      for (const auto& s : stmt.while_stmt.body)
        collectFreeVarsStmt(*s, out);
      break;
    case Stmt::ForStmt:
      if (stmt.for_stmt.is_parallel) {
        collectFreeVarsExpr(*stmt.for_stmt.range_start, out);
        collectFreeVarsExpr(*stmt.for_stmt.range_end, out);
      } else if (stmt.for_stmt.is_range) {
        collectFreeVarsExpr(*stmt.for_stmt.range_start, out);
        collectFreeVarsExpr(*stmt.for_stmt.range_end, out);
      } else if (stmt.for_stmt.is_foreach) {
        collectFreeVarsExpr(*stmt.for_stmt.foreach_iter, out);
      }
      if (stmt.for_stmt.init)
        collectFreeVarsStmt(*stmt.for_stmt.init, out);
      if (stmt.for_stmt.cond)
        collectFreeVarsExpr(*stmt.for_stmt.cond, out);
      if (stmt.for_stmt.step)
        collectFreeVarsStmt(*stmt.for_stmt.step, out);
      for (const auto& s : stmt.for_stmt.body)
        collectFreeVarsStmt(*s, out);
      break;
    case Stmt::TryStmtK:
      for (const auto& s : stmt.try_stmt.try_body)
        collectFreeVarsStmt(*s, out);
      for (const auto& s : stmt.try_stmt.catch_body)
        collectFreeVarsStmt(*s, out);
      for (const auto& s : stmt.try_stmt.finally_body)
        collectFreeVarsStmt(*s, out);
      break;
    case Stmt::MatchStmtK:
      collectFreeVarsExpr(*stmt.match_stmt.scrutinee, out);
      for (const auto& arm : stmt.match_stmt.arms) {
        for (const auto& s : arm.body)
          collectFreeVarsStmt(*s, out);
      }
      break;
    case Stmt::ThrowStmtK:
      collectFreeVarsExpr(*stmt.throw_stmt.value, out);
      break;
    case Stmt::DeferStmtK:
      collectFreeVarsExpr(*stmt.defer.expr, out);
      break;
    case Stmt::UnsafeStmtK:
      for (const auto& s : stmt.unsafe.body)
        collectFreeVarsStmt(*s, out);
      break;
    default:
      break;
  }
}

static bool isIndexable(const TypeDesc& td) {
  return td.form == TypeForm::Array || td.form == TypeForm::List || td.form == TypeForm::FixedArray ||
         td.form == TypeForm::Span || td.form == TypeForm::Slice || isPrim(td, FarTypeId::String) ||
         isPrim(td, FarTypeId::RawString);
}

static TypeDesc inferArrayElemType(const std::vector<std::unique_ptr<Expr>>& elements) {
  if (elements.empty())
    return defaultIntType();
  TypeDesc common = elements[0]->type;
  for (size_t i = 1; i < elements.size(); ++i) {
    TypeDesc et = elements[i]->type;
    if (typeDescEquals(common, et))
      continue;
    if (isPrimitiveDesc(common) && isPrimitiveDesc(et))
      common = promoteNumeric(common, et);
  }
  return common;
}

static TypeDesc inferDictEntryType(const std::vector<DictEntry>& entries, bool key) {
  if (entries.empty())
    return defaultIntType();
  TypeDesc common = key ? entries[0].key->type : entries[0].value->type;
  for (size_t i = 1; i < entries.size(); ++i) {
    TypeDesc et = key ? entries[i].key->type : entries[i].value->type;
    if (typeDescEquals(common, et))
      continue;
    if (isPrimitiveDesc(common) && isPrimitiveDesc(et))
      common = promoteNumeric(common, et);
  }
  return common;
}

static bool exprHasStringConst(const Expr& e) {
  if (e.kind == Expr::String || e.kind == Expr::Char)
    return true;
  if (e.kind == Expr::Binary && e.bin_op.op == "+")
    return exprHasStringConst(*e.bin_op.left) || exprHasStringConst(*e.bin_op.right);
  return false;
}

static bool exprIsParamRef(const Expr& e, const std::string& name) {
  return e.kind == Expr::Variable && e.var.name == name;
}

static bool paramTypeIsString(const TypeDesc& td) {
  return isPrim(td, FarTypeId::String) || isPrim(td, FarTypeId::Char);
}

static bool inferParamStringFromExpr(const Expr& e, const std::string& param,
                                     const std::unordered_map<std::string, TypeDesc>& param_types) {
  if (e.kind == Expr::Binary && (e.bin_op.op == "==" || e.bin_op.op == "!=")) {
    if (exprIsParamRef(*e.bin_op.left, param) && exprHasStringConst(*e.bin_op.right))
      return true;
    if (exprIsParamRef(*e.bin_op.right, param) && exprHasStringConst(*e.bin_op.left))
      return true;
  }
  if (e.kind == Expr::Binary && e.bin_op.op == "+") {
    const Expr& l = *e.bin_op.left;
    const Expr& r = *e.bin_op.right;
    if (exprIsParamRef(l, param)) {
      if (exprHasStringConst(r))
        return true;
      if (r.kind == Expr::Variable && param_types.count(r.var.name) &&
          paramTypeIsString(param_types.at(r.var.name)))
        return true;
    }
    if (exprIsParamRef(r, param)) {
      if (exprHasStringConst(l))
        return true;
      if (l.kind == Expr::Variable && param_types.count(l.var.name) &&
          paramTypeIsString(param_types.at(l.var.name)))
        return true;
    }
    if (inferParamStringFromExpr(l, param, param_types) || inferParamStringFromExpr(r, param, param_types))
      return true;
  }
  if (e.kind == Expr::FnCall) {
    if (e.call.name == "len" && !e.call.args.empty() &&
        exprIsParamRef(*e.call.args[0].value, param))
      return true;
    for (const auto& a : e.call.args) {
      if (inferParamStringFromExpr(*a.value, param, param_types))
        return true;
    }
  }
  if (e.kind == Expr::TernaryExprK) {
    if (inferParamStringFromExpr(*e.ternary.then_br, param, param_types) ||
        inferParamStringFromExpr(*e.ternary.else_br, param, param_types))
      return true;
  }
  if (e.kind == Expr::AssignExprK) {
    if (inferParamStringFromExpr(*e.assign.value, param, param_types))
      return true;
  }
  return false;
}

static bool inferParamStringFromStmt(const Stmt& s, const std::string& param,
                                     const std::unordered_map<std::string, TypeDesc>& param_types) {
  switch (s.kind) {
    case Stmt::LetStmt:
      return inferParamStringFromExpr(*s.let.value, param, param_types);
    case Stmt::ReturnStmt:
      return s.ret.has_value && inferParamStringFromExpr(*s.ret.value, param, param_types);
    case Stmt::ExprStmtK:
      return inferParamStringFromExpr(*s.expr_stmt.expr, param, param_types);
    case Stmt::PrintStmt:
      return inferParamStringFromExpr(*s.print.value, param, param_types);
    case Stmt::IfStmt: {
      bool hit = false;
      for (const auto& c : s.if_stmt.clauses) {
        hit = inferParamStringFromExpr(*c.condition, param, param_types) || hit;
        for (const auto& st : c.body)
          hit = inferParamStringFromStmt(*st, param, param_types) || hit;
      }
      for (const auto& st : s.if_stmt.else_body)
        hit = inferParamStringFromStmt(*st, param, param_types) || hit;
      return hit;
    }
    case Stmt::WhileStmt:
      return inferParamStringFromExpr(*s.while_stmt.condition, param, param_types) ||
             std::any_of(s.while_stmt.body.begin(), s.while_stmt.body.end(),
                         [&](const auto& st) { return inferParamStringFromStmt(*st, param, param_types); });
    case Stmt::ForStmt: {
      bool hit = false;
      if (s.for_stmt.init)
        hit = inferParamStringFromStmt(*s.for_stmt.init, param, param_types);
      if (s.for_stmt.step)
        hit = inferParamStringFromStmt(*s.for_stmt.step, param, param_types) || hit;
      for (const auto& st : s.for_stmt.body)
        hit = inferParamStringFromStmt(*st, param, param_types) || hit;
      return hit;
    }
    default:
      return false;
  }
}

static const std::vector<std::unique_ptr<Stmt>>* functionBody(const Function& fn) {
  if (fn.body_source)
    return &fn.body_source->body;
  if (fn.shared_body)
    return fn.shared_body;
  return &fn.body;
}

static bool exprLooksString(const Expr& e, const std::unordered_map<std::string, TypeDesc>& param_types) {
  if (exprHasStringConst(e))
    return true;
  if (e.kind == Expr::Variable && param_types.count(e.var.name) &&
      paramTypeIsString(param_types.at(e.var.name)))
    return true;
  if (e.kind == Expr::Binary && e.bin_op.op == "+")
    return exprLooksString(*e.bin_op.left, param_types) || exprLooksString(*e.bin_op.right, param_types);
  return false;
}

static bool exprLooksFloat(const Expr& e) {
  return e.kind == Expr::Float;
}

static void inferReturnType(Function& fn) {
  if (fn.return_type_explicit)
    return;
  std::unordered_map<std::string, TypeDesc> param_types;
  for (const auto& p : fn.params)
    param_types[p.name] = p.type;
  for (const auto& stmt : *functionBody(fn)) {
    if (stmt->kind == Stmt::ReturnStmt && stmt->ret.has_value) {
      if (exprLooksString(*stmt->ret.value, param_types)) {
        fn.return_type = TypeDesc::prim(FarTypeId::String);
        return;
      }
      if (exprLooksFloat(*stmt->ret.value)) {
        fn.return_type = TypeDesc::prim(stmt->ret.value->float_lit.is_float ? FarTypeId::F32
                                                                           : FarTypeId::F64);
        return;
      }
    }
  }
}

static void inferUntypedParamTypes(Function& fn) {
  const std::vector<std::unique_ptr<Stmt>>* body = functionBody(fn);
  bool changed = true;
  for (int guard = 0; changed && guard < 12; ++guard) {
    changed = false;
    std::unordered_map<std::string, TypeDesc> param_types;
    for (const auto& p : fn.params)
      param_types[p.name] = p.type;

    for (auto& p : fn.params) {
      if (p.type_explicit)
        continue;
      if (p.default_value &&
          (p.default_value->kind == Expr::String || p.default_value->kind == Expr::Char)) {
        if (!isPrim(p.type, FarTypeId::String)) {
          p.type = TypeDesc::prim(FarTypeId::String);
          changed = true;
        }
        continue;
      }
      bool is_string = false;
      for (const auto& stmt : *body) {
        if (inferParamStringFromStmt(*stmt, p.name, param_types))
          is_string = true;
      }
      if (is_string && !isPrim(p.type, FarTypeId::String)) {
        p.type = TypeDesc::prim(FarTypeId::String);
        changed = true;
      }
    }
  }
}

class TypeChecker {
 public:
  explicit TypeChecker(Program& program) : program_(program) {
    registerFunctions(program_, fn_overloads_);
  }

  void run() {
    obj_reg_.build(program_, true);
    materializeParallelFors(program_);
    for (auto& fn : program_.synthetic_functions) {
      fn.llvm_name = mangleFunction(fn);
      fn_overloads_[fn.name].push_back(&fn);
    }
    std::unordered_map<std::string, int> overload_count;
    for (const auto& fn : program_.functions)
      overload_count[fn.name]++;
    for (auto& fn : program_.functions)
      fn.llvm_name = (overload_count[fn.name] > 1 || fn.is_lambda) ? mangleFunction(fn) : fn.name;
    for (auto& fn : program_.functions) {
      if (!fn.type_params.empty())
        continue;
      inferUntypedParamTypes(fn);
      inferReturnType(fn);
    }
    for (auto& fn : program_.synthetic_functions) {
      if (!fn.type_params.empty())
        continue;
      inferUntypedParamTypes(fn);
      inferReturnType(fn);
    }
    for (const auto& stmt : program_.comptime_stmts) {
      auto saved = locals_;
      auto saved_const = const_locals_;
      if (stmt->kind == Stmt::ComptimeBlockK)
        checkStmtBlock(stmt->comptime_block);
      else
        checkStmt(*stmt);
      locals_ = saved;
      const_locals_ = saved_const;
    }
    for (const auto& stmt : program_.codegen_stmts) {
      auto saved = locals_;
      auto saved_const = const_locals_;
      if (stmt->kind == Stmt::CodegenBlockK)
        checkStmtBlock(stmt->codegen_block);
      else
        checkStmt(*stmt);
      locals_ = saved;
      const_locals_ = saved_const;
    }
    for (auto& fn : program_.functions) {
      if (!fn.type_params.empty())
        continue;
      checkFunction(fn);
    }
    for (size_t i = 0; i < program_.synthetic_functions.size(); ++i) {
      Function& fn = program_.synthetic_functions[i];
      if (fn.name.rfind("pfor$", 0) == 0)
        continue;
      if (fn.type_params.empty()) {
        inferUntypedParamTypes(fn);
        inferReturnType(fn);
      }
      checkFunction(fn);
    }
  }

 private:
  Program& program_;
  ObjectRegistry obj_reg_;
  std::unordered_map<std::string, std::vector<const Function*>> fn_overloads_;
  std::unordered_map<std::string, TypeDesc> locals_;
  ConstLocalsState const_locals_;
  std::unordered_set<std::string> explicit_locals_;
  std::unordered_set<std::string> constexpr_empty_locals_;
  const Function* current_fn_ = nullptr;
  const UserTypeDef* current_user_type_ = nullptr;
  int lambda_counter_ = 0;
  int unsafe_depth_ = 0;
  int comptime_depth_ = 0;
  int check_depth_ = 0;
  int loop_depth_ = 0;
  int switch_depth_ = 0;
  int parallel_for_depth_ = 0;

  void trackConstLocal(const std::string& name, const Expr& value) {
    if (auto i = constIntFromExpr(value, const_locals_)) {
      const_locals_.ints[name] = *i;
      const_locals_.floats.erase(name);
      return;
    }
    if (auto f = constFloatFromExpr(value, const_locals_)) {
      const_locals_.floats[name] = *f;
      const_locals_.ints.erase(name);
      return;
    }
    if (value.kind == Expr::Int) {
      const_locals_.ints[name] = value.int_lit.value;
      const_locals_.floats.erase(name);
    } else if (value.kind == Expr::Float && !std::isnan(value.float_lit.value)) {
      const_locals_.floats[name] = value.float_lit.value;
      const_locals_.ints.erase(name);
    } else if (value.kind == Expr::EnumVariantExprK) {
      const_locals_.ints[name] = value.enum_variant.value;
      const_locals_.floats.erase(name);
    } else {
      const_locals_.ints.erase(name);
      const_locals_.floats.erase(name);
    }
  }

  void clearConstLocal(const std::string& name) {
    const_locals_.ints.erase(name);
    const_locals_.floats.erase(name);
  }

  bool assignTargetCanBeNullish(const Expr& target) const {
    if (target.kind == Expr::Int)
      return target.int_lit.value == 0;
    if (target.kind == Expr::Float)
      return target.float_lit.value == 0.0 || std::isnan(target.float_lit.value);
    if (target.kind == Expr::Variable) {
      auto fit = const_locals_.floats.find(target.var.name);
      if (fit != const_locals_.floats.end())
        return fit->second == 0.0 || std::isnan(fit->second);
      auto it = const_locals_.ints.find(target.var.name);
      if (it != const_locals_.ints.end())
        return it->second == 0;
    }
    return true;
  }

  void pushCheckDepth() {
    if (++check_depth_ > 512)
      throw FarError("typecheck depth exceeded");
  }

  void popCheckDepth() {
    if (check_depth_ > 0)
      --check_depth_;
  }

  struct CheckDepthGuard {
    TypeChecker& tc;
    explicit CheckDepthGuard(TypeChecker& t) : tc(t) { tc.pushCheckDepth(); }
    ~CheckDepthGuard() { tc.popCheckDepth(); }
  };

  static bool isIntegerScrutinee(const TypeDesc& ty) {
    return isPrimitiveDesc(ty) && isIntegerType(ty.primitive);
  }

  static bool isFloatScrutinee(const TypeDesc& ty) {
    return isPrimitiveDesc(ty) && isFloatType(ty.primitive);
  }

  void injectConstexprFromStmts(const std::vector<std::unique_ptr<Stmt>>& stmts) {
    for (const auto& stmt : stmts) {
      if (stmt->kind == Stmt::LetStmt && stmt->let.is_constexpr) {
        if (!locals_.count(stmt->let.name)) {
          TypeDesc ty = stmt->let.explicit_type ? stmt->let.type
                                                : (stmt->let.value ? stmt->let.value->type
                                                                   : TypeDesc::prim(FarTypeId::I64));
          locals_[stmt->let.name] = ty;
        }
        if (stmt->let.value)
          trackConstLocal(stmt->let.name, *stmt->let.value);
      } else if (stmt->kind == Stmt::ComptimeBlockK) {
        injectConstexprFromStmts(stmt->comptime_block);
      }
    }
  }

  void injectConstexprGlobals() {
    injectConstexprFromStmts(program_.comptime_stmts);
  }

  void checkPattern(Pattern& pat, const TypeDesc& scrut_ty) {
    CheckDepthGuard depth(*this);
    switch (pat.kind) {
      case PatKind::Wildcard:
        return;
      case PatKind::Bind:
        locals_[pat.bind_name] = scrut_ty;
        return;
      case PatKind::Literal:
        if (pat.literal_is_float) {
          if (!isFloatScrutinee(scrut_ty))
            throw FarError("literal pattern requires float scrutinee");
        } else if (!isIntegerScrutinee(scrut_ty) && !isFloatScrutinee(scrut_ty)) {
          throw FarError("literal pattern requires integer or float scrutinee");
        }
        return;
      case PatKind::EnumVariant: {
        int v = obj_reg_.enumVariantValue(pat.type_name, pat.variant);
        if (v < 0)
          throw FarError("unknown enum variant " + pat.type_name + "." + pat.variant);
        pat.variant_value = v;
        if (isUserDesc(scrut_ty)) {
          if (scrut_ty.user_name != pat.type_name)
            throw FarError("enum pattern type mismatch");
        } else if (!isIntegerScrutinee(scrut_ty)) {
          throw FarError("enum pattern requires integer or enum scrutinee");
        }
        return;
      }
      case PatKind::UnionVariant: {
        const UserTypeDef* td = obj_reg_.lookup(pat.type_name);
        if (!td || td->kind != UserTypeKind::Union)
          throw FarError("unknown union type '" + pat.type_name + "'");
        const EnumVariant* uv = lookupVariant(*td, pat.variant);
        if (!uv)
          throw FarError("unknown union variant " + pat.type_name + "." + pat.variant);
        pat.variant_value = uv->value;
        if (!isUserDesc(scrut_ty) || scrut_ty.user_name != pat.type_name)
          throw FarError("union pattern type mismatch");
        if (pat.fields.size() != uv->fields.size())
          throw FarError("union pattern field count mismatch");
        for (size_t i = 0; i < pat.fields.size(); ++i)
          checkPattern(*pat.fields[i], uv->fields[i].type);
        return;
      }
      case PatKind::TypeTest:
        if (!isUserDesc(scrut_ty) || !typeDescEquals(scrut_ty, pat.type_test))
          throw FarError("type pattern mismatch");
        return;
      case PatKind::StructDestructure: {
        if (!isUserDesc(scrut_ty))
          throw FarError("struct pattern type mismatch");
        const UserTypeDef* td = resolveUserType(scrut_ty, obj_reg_, program_);
        if (!td)
          throw FarError("unknown type '" + pat.type_name + "'");
        if (scrut_ty.user_name != pat.type_name)
          throw FarError("struct pattern type mismatch");
        for (size_t i = 0; i < pat.fields.size(); ++i) {
          int fidx = static_cast<int>(i);
          if (!pat.field_names.empty()) {
            fidx = obj_reg_.lookupFieldIndex(scrut_ty, pat.field_names[i]);
            if (fidx < 0)
              throw FarError("unknown field '" + pat.field_names[i] + "'");
          } else if (fidx >= static_cast<int>(td->fields.size())) {
            throw FarError("struct pattern field count mismatch");
          }
          checkPattern(*pat.fields[i], td->fields[static_cast<size_t>(fidx)].type);
        }
        return;
      }
      case PatKind::TupleDestructure:
        if (scrut_ty.form != TypeForm::Tuple)
          throw FarError("tuple pattern requires tuple scrutinee");
        if (pat.fields.size() != scrut_ty.args.size())
          throw FarError("tuple pattern arity mismatch");
        for (size_t i = 0; i < pat.fields.size(); ++i)
          checkPattern(*pat.fields[i], scrut_ty.args[i]);
        return;
    }
  }

  void checkFunction(Function& fn) {
    current_fn_ = &fn;
    current_user_type_ = nullptr;
    const size_t dollar = fn.name.find('$');
    if (dollar != std::string::npos)
      current_user_type_ = obj_reg_.lookupMut(fn.name.substr(0, dollar));
    locals_.clear();
    const_locals_ = {};
    explicit_locals_.clear();
    constexpr_empty_locals_.clear();
    for (auto& p : fn.params) {
      if (p.default_value) {
        TypeDesc dv = checkExpr(*p.default_value);
        rejectIntLiteralOutOfRangeForTarget(*p.default_value, p.type);
        if (!p.type_explicit && !typeDescEquals(p.type, dv))
          p.type = dv;
      }
      locals_[p.name] = p.type;
      explicit_locals_.insert(p.name);
    }
    injectConstexprGlobals();
    const std::vector<std::unique_ptr<Stmt>>* body = &fn.body;
    if (fn.body_source)
      body = &fn.body_source->body;
    else if (fn.shared_body)
      body = fn.shared_body;
    checkStmtBlock(*body);
    current_fn_ = nullptr;
    current_user_type_ = nullptr;
  }

  bool stmtAbortsRestOfBlock(const Stmt& stmt) const {
    if (stmt.kind == Stmt::ReturnStmt || stmt.kind == Stmt::ThrowStmtK)
      return true;
    if (stmt.kind == Stmt::IfStmt) {
      bool chain_matched = false;
      for (const auto& c : stmt.if_stmt.clauses) {
        if (chain_matched)
          break;
        ConstBool cb = constBoolFromExpr(*c.condition, const_locals_);
        if (cb == ConstBool::False)
          continue;
        if (cb == ConstBool::True) {
          chain_matched = true;
          return stmtBlockAlwaysReturns(c.body, const_locals_, obj_reg_);
        }
      }
      if (chain_matched)
        return true;
      if (stmt.if_stmt.else_body.empty())
        return false;
      return stmtBlockAlwaysReturns(stmt.if_stmt.else_body, const_locals_, obj_reg_);
    }
    if (stmt.kind == Stmt::TryStmtK)
      return tryCatchUnreachable(stmt.try_stmt.try_body, const_locals_, obj_reg_);
    if (stmt.kind == Stmt::MatchStmtK) {
      std::optional<double> const_scrut_f =
          constFloatFromExpr(*stmt.match_stmt.scrutinee, const_locals_);
      if (const_scrut_f) {
        for (const auto& arm : stmt.match_stmt.arms) {
          if (arm.pat && isMatchWildcardPattern(*arm.pat))
            continue;
          if (arm.pat && patternCertainlyFailsConstFloat(*arm.pat, *const_scrut_f))
            continue;
          if (arm.pat && patternMatchesConstFloat(*arm.pat, *const_scrut_f))
            return stmtBlockAlwaysReturns(arm.body, const_locals_, obj_reg_);
        }
        for (const auto& arm : stmt.match_stmt.arms) {
          if (arm.pat && isMatchWildcardPattern(*arm.pat))
            return stmtBlockAlwaysReturns(arm.body, const_locals_, obj_reg_);
        }
        return false;
      }
      std::optional<int64_t> const_scrut =
          constIntFromExpr(*stmt.match_stmt.scrutinee, const_locals_);
      if (!const_scrut)
        return false;
      for (const auto& arm : stmt.match_stmt.arms) {
        if (arm.pat && isMatchWildcardPattern(*arm.pat))
          continue;
        if (arm.pat && patternCertainlyFailsConstInt(*arm.pat, *const_scrut, obj_reg_))
          continue;
        if (arm.pat && patternMatchesConstInt(*arm.pat, *const_scrut, obj_reg_))
          return stmtBlockAlwaysReturns(arm.body, const_locals_, obj_reg_);
      }
      for (const auto& arm : stmt.match_stmt.arms) {
        if (arm.pat && isMatchWildcardPattern(*arm.pat))
          return stmtBlockAlwaysReturns(arm.body, const_locals_, obj_reg_);
      }
    }
    return false;
  }

  void checkStmtBlock(const std::vector<std::unique_ptr<Stmt>>& stmts) {
    for (const auto& stmt : stmts) {
      checkStmt(*stmt);
      if (stmtAbortsRestOfBlock(*stmt))
        break;
    }
  }

  void checkStmt(Stmt& stmt) {
    CheckDepthGuard depth(*this);
    switch (stmt.kind) {
      case Stmt::LetStmt: {
        TypeDesc ty = checkExpr(*stmt.let.value);
        if (stmt.let.explicit_type)
          rejectIntLiteralOutOfRangeForTarget(*stmt.let.value, stmt.let.type);
        if (stmt.let.explicit_type) {
          if (stmt.let.value->kind == Expr::DictLitExpr && stmt.let.value->dict_lit.entries.empty() &&
              stmt.let.type.form == TypeForm::Dict) {
            ty = stmt.let.type;
            stmt.let.value->type = ty;
          } else if (stmt.let.value->kind == Expr::ArrayLitExpr &&
                     stmt.let.value->array_lit.elements.empty() &&
                     stmt.let.type.form == TypeForm::Array) {
            ty = stmt.let.type;
            stmt.let.value->type = ty;
          } else if (stmt.let.value->kind == Expr::EnumVariantExprK && isUserDesc(stmt.let.type) &&
                     stmt.let.value->enum_variant.type_name == stmt.let.type.user_name) {
            ty = stmt.let.type;
            stmt.let.value->type = ty;
          }
          if (!canAssignTypes(ty, stmt.let.type))
            throw FarError("type mismatch in let: expected " + typeDescName(stmt.let.type) + ", got " +
                           typeDescName(ty));
          rejectNarrowingStore(ty, stmt.let.type);
          locals_[stmt.let.name] = stmt.let.type;
          explicit_locals_.insert(stmt.let.name);
        } else if (locals_.count(stmt.let.name)) {
          TypeDesc existing = locals_.at(stmt.let.name);
          maybeWidenInferredLocal(locals_, explicit_locals_, stmt.let.name, ty, existing);
          rejectIntLiteralOutOfRangeForTarget(*stmt.let.value, existing);
          if (explicit_locals_.count(stmt.let.name) ||
              (isPrimitiveDesc(existing) && isFloatType(existing.primitive)))
            rejectNarrowingStore(ty, existing);
          if (!canAssignTypes(ty, existing))
            throw FarError("type mismatch in assignment to '" + stmt.let.name + "': expected " +
                           typeDescName(existing) + ", got " + typeDescName(ty));
          locals_[stmt.let.name] = existing;
        } else {
          if (stmt.let.value->kind == Expr::EnumVariantExprK)
            ty = TypeDesc::user(stmt.let.value->enum_variant.type_name);
          locals_[stmt.let.name] = ty;
        }
        trackConstLocal(stmt.let.name, *stmt.let.value);
        if (stmt.let.is_constexpr && stmt.let.value && isEmptyConstexprLiteral(*stmt.let.value))
          constexpr_empty_locals_.insert(stmt.let.name);
        break;
      }
      case Stmt::ReturnStmt:
        if (stmt.ret.has_value) {
          TypeDesc ty = checkExpr(*stmt.ret.value);
          rejectIntLiteralOutOfRangeForTarget(*stmt.ret.value, current_fn_->return_type);
          rejectNarrowingStore(ty, current_fn_->return_type);
          if (!canAssignTypes(ty, current_fn_->return_type))
            throw FarError(std::string("return type mismatch: expected ") +
                           typeDescName(current_fn_->return_type) + ", got " + typeDescName(ty));
        } else if (!isPrim(current_fn_->return_type, FarTypeId::I64)) {
          throw FarError("function must return a value");
        }
        break;
      case Stmt::ExprStmtK:
        checkExpr(*stmt.expr_stmt.expr);
        break;
      case Stmt::PrintStmt:
        checkExpr(*stmt.print.value);
        break;
      case Stmt::IfStmt: {
        bool chain_matched = false;
        for (const auto& c : stmt.if_stmt.clauses) {
          if (chain_matched)
            break;
          auto branch_saved = locals_;
          auto branch_const = const_locals_;
          checkExpr(*c.condition);
          ConstBool cb = constBoolFromExpr(*c.condition, const_locals_);
          if (cb == ConstBool::False) {
            locals_ = branch_saved;
            const_locals_ = branch_const;
            continue;
          }
          checkStmtBlock(c.body);
          locals_ = branch_saved;
          if (cb != ConstBool::True)
            const_locals_ = branch_const;
          if (cb == ConstBool::True)
            chain_matched = true;
        }
        if (!chain_matched) {
          auto else_saved = locals_;
          checkStmtBlock(stmt.if_stmt.else_body);
          locals_ = else_saved;
        }
        break;
      }
      case Stmt::WhileStmt: {
        checkExpr(*stmt.while_stmt.condition);
        ConstBool cb = constBoolFromExpr(*stmt.while_stmt.condition, const_locals_);
        if (cb != ConstBool::False) {
          ++loop_depth_;
          auto saved = locals_;
          auto saved_const = const_locals_;
          checkStmtBlock(stmt.while_stmt.body);
          locals_ = saved;
          if (cb != ConstBool::True)
            const_locals_ = saved_const;
          --loop_depth_;
        }
        break;
      }
      case Stmt::ForStmt:
        if (stmt.for_stmt.is_parallel) {
          checkExpr(*stmt.for_stmt.range_start);
          checkExpr(*stmt.for_stmt.range_end);
          auto range_start = constIntFromExpr(*stmt.for_stmt.range_start, const_locals_);
          auto range_end = constIntFromExpr(*stmt.for_stmt.range_end, const_locals_);
          bool skip_body = range_start && range_end &&
                           rangeForConstEmpty(*range_start, *range_end, stmt.for_stmt.range_exclusive);
          Function* pfor = findSyntheticByLlvm(program_, stmt.for_stmt.parallel_fn);
          if (!pfor)
            throw FarError("internal: missing parallel-for worker");
          std::unordered_set<std::string> free;
          for (const auto& s : pfor->body)
            collectFreeVarsStmt(*s, free);
          free.erase(stmt.for_stmt.parallel_var);
          std::vector<std::string> caps;
          for (const auto& name : free) {
            if (locals_.count(name))
              caps.push_back(name);
            else
              throw FarError("parallel for references undefined variable '" + name + "'");
          }
          if (caps.size() > 4)
            throw FarError("parallel for captures at most 4 variables");
          stmt.for_stmt.parallel_captures = caps;
          pfor->captures = caps;
          std::vector<Param> params;
          for (const auto& cap : caps) {
            Param cp;
            cp.name = cap;
            cp.type = locals_.at(cap);
            params.push_back(std::move(cp));
          }
          Param ip;
          ip.name = stmt.for_stmt.parallel_var;
          ip.type = TypeDesc::prim(FarTypeId::I64);
          params.push_back(std::move(ip));
          pfor->params = std::move(params);
          pfor->llvm_name = mangleFunction(*pfor);
          stmt.for_stmt.parallel_fn = pfor->llvm_name;
          auto saved = locals_;
          auto saved_const = const_locals_;
          locals_[stmt.for_stmt.parallel_var] = TypeDesc::prim(FarTypeId::I64);
          ++parallel_for_depth_;
          for (const auto& s : pfor->body)
            checkStmt(*s);
          --parallel_for_depth_;
          locals_ = saved;
          const_locals_ = saved_const;
          if (skip_body) {
            pfor->body.clear();
            pfor->captures.clear();
            stmt.for_stmt.parallel_captures.clear();
          }
          break;
        }
        if (stmt.for_stmt.is_range) {
          checkExpr(*stmt.for_stmt.range_start);
          checkExpr(*stmt.for_stmt.range_end);
          auto range_start = constIntFromExpr(*stmt.for_stmt.range_start, const_locals_);
          auto range_end = constIntFromExpr(*stmt.for_stmt.range_end, const_locals_);
          bool skip_body = range_start && range_end &&
                           rangeForConstEmpty(*range_start, *range_end, stmt.for_stmt.range_exclusive);
          bool range_known = range_start.has_value() && range_end.has_value();
          locals_[stmt.for_stmt.range_var] = defaultIntType();
          if (!skip_body) {
            ++loop_depth_;
            {
              auto saved = locals_;
              auto saved_const = const_locals_;
              if (range_known) {
                if (auto sample = rangeForConstSampleValue(*range_start, *range_end,
                                                           stmt.for_stmt.range_exclusive))
                  const_locals_.ints[stmt.for_stmt.range_var] = *sample;
              }
              checkStmtBlock(stmt.for_stmt.body);
              locals_ = saved;
              if (!range_known)
                const_locals_ = saved_const;
            }
            --loop_depth_;
          }
          locals_.erase(stmt.for_stmt.range_var);
          const_locals_.ints.erase(stmt.for_stmt.range_var);
          const_locals_.floats.erase(stmt.for_stmt.range_var);
          break;
        }
        if (stmt.for_stmt.is_foreach) {
          TypeDesc coll_ty = checkExpr(*stmt.for_stmt.foreach_iter);
          bool skip_body =
              foreachConstEmpty(*stmt.for_stmt.foreach_iter, &program_, &constexpr_empty_locals_);
          if (isPrim(coll_ty, FarTypeId::String)) {
            if (!skip_body)
              throw FarError("for-in over non-empty string is not supported yet");
            break;
          }
          if (!isIndexable(coll_ty))
            throw FarError("for-in requires an indexable collection (array, list, slice, ...), not " +
                           typeDescName(coll_ty));
          TypeDesc elem = elemTypeOf(coll_ty);
          locals_[stmt.for_stmt.foreach_var] = elem;
          if (!skip_body) {
            ++loop_depth_;
            {
              auto saved = locals_;
              if (auto sample = constForeachSampleElem(*stmt.for_stmt.foreach_iter, const_locals_,
                                                       &program_))
                const_locals_.ints[stmt.for_stmt.foreach_var] = *sample;
              for (const auto& s : stmt.for_stmt.body)
                checkStmt(*s);
              locals_ = saved;
            }
            --loop_depth_;
          }
          locals_.erase(stmt.for_stmt.foreach_var);
          const_locals_.ints.erase(stmt.for_stmt.foreach_var);
          const_locals_.floats.erase(stmt.for_stmt.foreach_var);
          break;
        }
        {
          if (stmt.for_stmt.init)
            checkStmt(*stmt.for_stmt.init);
          ConstBool for_cb = ConstBool::Unknown;
          if (stmt.for_stmt.cond) {
            checkExpr(*stmt.for_stmt.cond);
            for_cb = constBoolFromExpr(*stmt.for_stmt.cond, const_locals_);
          }
          if (for_cb != ConstBool::False) {
            ++loop_depth_;
            {
              auto saved = locals_;
              auto saved_const = const_locals_;
              for (const auto& s : stmt.for_stmt.body)
                checkStmt(*s);
              if (stmt.for_stmt.step)
                checkStmt(*stmt.for_stmt.step);
              locals_ = saved;
              if (for_cb != ConstBool::True)
                const_locals_ = saved_const;
            }
            --loop_depth_;
          }
        }
        break;
      case Stmt::YieldStmtK:
        if (!current_fn_->is_generator && !current_fn_->is_coroutine)
          throw FarError("yield only allowed in generator/coroutine functions");
        if (stmt.yield.has_value) {
          TypeDesc ty = checkExpr(*stmt.yield.value);
          rejectIntLiteralOutOfRangeForTarget(*stmt.yield.value, current_fn_->return_type);
          rejectNarrowingStore(ty, current_fn_->return_type);
          if (!canAssignTypes(ty, current_fn_->return_type))
            throw FarError(std::string("yield type mismatch: expected ") +
                           typeDescName(current_fn_->return_type) + ", got " + typeDescName(ty));
        }
        break;
      case Stmt::BreakStmt:
        if (parallel_for_depth_ > 0)
          throw FarError("break not allowed in parallel for");
        if (loop_depth_ <= 0 && switch_depth_ <= 0)
          throw FarError("break outside loop");
        break;
      case Stmt::ContinueStmt:
        if (parallel_for_depth_ > 0)
          throw FarError("continue not allowed in parallel for");
        if (loop_depth_ <= 0)
          throw FarError("continue outside loop");
        break;
      case Stmt::DeferStmtK:
        checkExpr(*stmt.defer.expr);
        break;
      case Stmt::UnsafeStmtK: {
        ++unsafe_depth_;
        auto saved = locals_;
        auto saved_const = const_locals_;
        checkStmtBlock(stmt.unsafe.body);
        locals_ = saved;
        const_locals_ = saved_const;
        --unsafe_depth_;
        break;
      }
      case Stmt::TryStmtK: {
        auto pre = locals_;
        auto pre_const = const_locals_;
        bool try_always_throws =
            tryCatchUnreachable(stmt.try_stmt.try_body, const_locals_, obj_reg_);
        bool catch_path_reachable = tryBodyMayThrow(stmt.try_stmt.try_body);
        checkStmtBlock(stmt.try_stmt.try_body);
        if (stmt.try_stmt.has_catch && !try_always_throws) {
          TypeDesc catch_ty = TypeDesc::prim(FarTypeId::I64);
          if (stmt.try_stmt.catch_type_explicit) {
            catch_ty = stmt.try_stmt.catch_type;
            if (isUserDesc(catch_ty)) {
              const UserTypeDef* ut = obj_reg_.lookup(catch_ty.user_name);
              if (ut && ut->kind == UserTypeKind::Exception)
                stmt.try_stmt.catch_type_tag = ut->type_tag;
            }
          }
          stmt.try_stmt.catch_type = catch_ty;
          auto catch_saved = locals_;
          auto catch_const = catch_path_reachable ? pre_const : const_locals_;
          const_locals_ = catch_const;
          if (catch_path_reachable) {
            if (auto thrown = tryBodyThrowLiteral(stmt.try_stmt.try_body, pre_const))
              const_locals_.ints[stmt.try_stmt.catch_var] = *thrown;
          }
          locals_[stmt.try_stmt.catch_var] = catch_ty;
          checkStmtBlock(stmt.try_stmt.catch_body);
          locals_ = catch_saved;
          if (!catch_path_reachable)
            const_locals_ = catch_const;
        }
        if (stmt.try_stmt.has_finally) {
          auto fin_saved = locals_;
          checkStmtBlock(stmt.try_stmt.finally_body);
          locals_ = fin_saved;
        }
        locals_ = pre;
        if (try_always_throws)
          const_locals_ = pre_const;
        break;
      }
      case Stmt::ThrowStmtK:
        checkExpr(*stmt.throw_stmt.value);
        break;
      case Stmt::MatchStmtK: {
        TypeDesc st = checkExpr(*stmt.match_stmt.scrutinee);
        if (stmt.match_stmt.is_switch) {
          bool has_default = false;
          for (const auto& arm : stmt.match_stmt.arms) {
            if (arm.pat && arm.pat->kind == PatKind::Wildcard)
              has_default = true;
          }
          if (!has_default)
            throw FarError("switch statement requires a default case");
        } else if (isIntegerScrutinee(st)) {
          bool has_wild = false;
          for (const auto& arm : stmt.match_stmt.arms) {
            if (arm.pat && (arm.pat->kind == PatKind::Wildcard || arm.pat->kind == PatKind::Bind)) {
              has_wild = true;
              break;
            }
          }
          if (!has_wild)
            throw FarError("non-exhaustive match on integer scrutinee: missing wildcard arm");
        } else if (isUserDesc(st)) {
          const UserTypeDef* td = obj_reg_.lookup(st.user_name);
          if (td && (td->kind == UserTypeKind::Enum || td->kind == UserTypeKind::FlagsEnum)) {
            bool has_wild = false;
            std::unordered_set<int64_t> covered;
            for (const auto& arm : stmt.match_stmt.arms) {
              if (!arm.pat)
                continue;
              if (arm.pat->kind == PatKind::Wildcard || arm.pat->kind == PatKind::Bind) {
                has_wild = true;
                break;
              }
              if (arm.pat->kind == PatKind::EnumVariant && arm.pat->type_name == st.user_name) {
                int v = obj_reg_.enumVariantValue(arm.pat->type_name, arm.pat->variant);
                if (v >= 0)
                  covered.insert(v);
              }
            }
            if (!has_wild) {
              for (const auto& v : td->variants) {
                if (!covered.count(v.value))
                  throw FarError("non-exhaustive match on " + st.user_name + ": missing variant " +
                                 v.name);
              }
            }
          }
        }
        std::optional<int64_t> const_scrut =
            constIntFromExpr(*stmt.match_stmt.scrutinee, const_locals_);
        std::optional<double> const_scrut_f =
            const_scrut ? std::nullopt
                        : constFloatFromExpr(*stmt.match_stmt.scrutinee, const_locals_);
        struct SwitchDepthGuard {
          TypeChecker* tc;
          explicit SwitchDepthGuard(TypeChecker* t) : tc(t) { ++tc->switch_depth_; }
          ~SwitchDepthGuard() { --tc->switch_depth_; }
        };
        std::optional<SwitchDepthGuard> switch_depth_guard;
        if (stmt.match_stmt.is_switch)
          switch_depth_guard.emplace(this);
        bool chain_matched = false;
        for (auto& arm : stmt.match_stmt.arms) {
          if (chain_matched)
            break;
          if (const_scrut_f && arm.pat && isMatchWildcardPattern(*arm.pat))
            continue;
          if (const_scrut_f && arm.pat &&
              patternCertainlyFailsConstFloat(*arm.pat, *const_scrut_f))
            continue;
          if (const_scrut && arm.pat && isMatchWildcardPattern(*arm.pat))
            continue;
          if (const_scrut && arm.pat &&
              patternCertainlyFailsConstInt(*arm.pat, *const_scrut, obj_reg_))
            continue;
          auto saved = locals_;
          auto saved_const = const_locals_;
          checkPattern(*arm.pat, st);
          if (const_scrut && arm.pat && arm.pat->kind == PatKind::Bind)
            const_locals_.ints[arm.pat->bind_name] = *const_scrut;
          if (const_scrut_f && arm.pat && arm.pat->kind == PatKind::Bind)
            const_locals_.floats[arm.pat->bind_name] = *const_scrut_f;
          checkStmtBlock(arm.body);
          locals_ = saved;
          bool arm_static = false;
          if (const_scrut_f && arm.pat &&
              patternMatchesConstFloat(*arm.pat, *const_scrut_f))
            arm_static = true;
          if (const_scrut && arm.pat &&
              patternMatchesConstInt(*arm.pat, *const_scrut, obj_reg_))
            arm_static = true;
          if (!arm_static)
            const_locals_ = saved_const;
          if (arm_static)
            chain_matched = true;
        }
        if ((const_scrut || const_scrut_f) && !chain_matched) {
          for (auto& arm : stmt.match_stmt.arms) {
            if (!arm.pat || !isMatchWildcardPattern(*arm.pat))
              continue;
            auto saved = locals_;
            checkPattern(*arm.pat, st);
            checkStmtBlock(arm.body);
            locals_ = saved;
            break;
          }
        }
        break;
      }
      case Stmt::ComptimeBlockK:
        checkStmtBlock(stmt.comptime_block);
        break;
      case Stmt::CodegenBlockK:
        checkStmtBlock(stmt.codegen_block);
        break;
    }
  }

  TypeDesc checkExpr(Expr& expr) {
    CheckDepthGuard depth(*this);
    TypeDesc ty = TypeDesc::prim(FarTypeId::I64);
    switch (expr.kind) {
      case Expr::Int:
        if (isPrim(expr.type, FarTypeId::Bool))
          ty = TypeDesc::prim(FarTypeId::Bool);
        else
          ty = inferIntLiteralType(expr.int_lit.value, expr.int_lit.unsigned_decimal);
        break;
      case Expr::Float:
        ty = TypeDesc::prim(expr.float_lit.is_float ? FarTypeId::F32 : FarTypeId::F64);
        break;
      case Expr::String:
        ty = TypeDesc::prim(FarTypeId::String);
        break;
      case Expr::Char:
        ty = TypeDesc::prim(FarTypeId::Char);
        break;
      case Expr::Variable: {
        auto it = locals_.find(expr.var.name);
        if (it != locals_.end()) {
          ty = it->second;
          break;
        }
        if (comptime_depth_ > 0) {
          if (const_locals_.ints.count(expr.var.name)) {
            ty = TypeDesc::prim(FarTypeId::I64);
            break;
          }
          if (const_locals_.floats.count(expr.var.name)) {
            ty = TypeDesc::prim(FarTypeId::F64);
            break;
          }
        }
        throwAt(expr, "undefined variable '" + expr.var.name + "'");
        break;
      }
      case Expr::Binary: {
        const std::string& op = expr.bin_op.op;
        TypeDesc lt = checkExpr(*expr.bin_op.left);
        TypeDesc rt;
        if (op == "and" || op == "&&") {
          ConstBool lb = constBoolFromExpr(*expr.bin_op.left, const_locals_);
          if (lb == ConstBool::False)
            rt = TypeDesc::prim(FarTypeId::Bool);
          else
            rt = checkExpr(*expr.bin_op.right);
        } else if (op == "or" || op == "||") {
          ConstBool lb = constBoolFromExpr(*expr.bin_op.left, const_locals_);
          if (lb == ConstBool::True)
            rt = TypeDesc::prim(FarTypeId::Bool);
          else
            rt = checkExpr(*expr.bin_op.right);
        } else if (op == "??") {
          if (exprConstNonNullish(*expr.bin_op.left, const_locals_))
            rt = lt;
          else
            rt = checkExpr(*expr.bin_op.right);
        } else {
          rt = checkExpr(*expr.bin_op.right);
        }
        if ((op == "-" || op == "~") && expr.bin_op.left->kind == Expr::Int &&
            expr.bin_op.left->int_lit.value == 0 && isAggregateDesc(rt))
          ty = TypeDesc::prim(checkUnaryAggregateOp(op, aggregateDescId(rt)));
        else if (op == "+" && (isPrim(lt, FarTypeId::String) || isPrim(rt, FarTypeId::String) ||
                               isPrim(lt, FarTypeId::Char) || isPrim(rt, FarTypeId::Char)))
          ty = TypeDesc::prim(FarTypeId::String);
        else if (isAggregateDesc(lt) || isAggregateDesc(rt))
          ty = TypeDesc::prim(checkAggregateBinOp(op, aggregateDescId(lt), aggregateDescId(rt)));
        else if (op == "in" || op == "not in") {
          if (isPrim(rt, FarTypeId::String)) {
            if (!isPrim(lt, FarTypeId::String) && !isPrim(lt, FarTypeId::Char))
              throwAt(expr, "left operand of 'in' must be a string when searching inside a string");
          } else if (isIndexable(rt) || rt.form == TypeForm::Set || rt.form == TypeForm::Dict) {
            if (rt.form == TypeForm::Dict) {
              if (!canAssignTypes(lt, rt.args[0]))
                throwAt(expr, "left operand type mismatch for dict key in 'in'");
            } else if (rt.form == TypeForm::Set) {
              if (!rt.args.empty() && !canAssignTypes(lt, rt.args[0]))
                throwAt(expr, "left operand type mismatch for set element in 'in'");
            } else if (!canAssignTypes(lt, elemTypeOf(rt)))
              throwAt(expr, "left operand type mismatch for collection element in 'in'");
          } else {
            throwAt(expr, "right operand of 'in' must be a string or indexable collection");
          }
          ty = TypeDesc::prim(FarTypeId::Bool);
        } else if (op == "and" || op == "or" || op == "&&" || op == "||") {
          ty = TypeDesc::prim(FarTypeId::Bool);
        } else if (op == "!" && expr.bin_op.left->kind == Expr::Int &&
                   expr.bin_op.left->int_lit.value == 0) {
          ty = TypeDesc::prim(FarTypeId::Bool);
        } else if (isUserDesc(lt) && (op == "==" || op == "!=" || op == "===" || op == "!==" || op == "<" ||
                                        op == ">" || op == "<=" || op == ">=")) {
          const UserMethod* om = obj_reg_.lookupMethod(lt, userOpMethodName(op));
          if (om)
            ty = om->return_type;
          else
            ty = TypeDesc::prim(FarTypeId::Bool);
        } else if (op == "==" || op == "!=" || op == "===" || op == "!==" || op == "<" || op == ">" ||
                   op == "<=" || op == ">=")
          ty = TypeDesc::prim(FarTypeId::Bool);
        else if (op == "??") {
          try {
            ty = unifyTernaryBranches(lt, rt);
          } catch (const FarError& e) {
            throwAt(expr, e.what());
          }
        }
        else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>")
          ty = TypeDesc::prim(FarTypeId::I64);
        else if (op == "?.") {
          if (!isUserDesc(lt) && !isOptionDesc(lt) && !isPointerDesc(lt))
            throwAt(expr, "optional chaining requires object, optional, or pointer type");
          TypeDesc base_ty = lt;
          if (isOptionDesc(lt) && !lt.args.empty())
            base_ty = lt.args[0];
          else if (isPointerDesc(lt))
            base_ty = lt.args[0];
          if (expr.bin_op.right->kind != Expr::String)
            throwAt(expr, "optional chaining requires a member name");
          if (isUserDesc(base_ty)) {
            const UserTypeDef* td = resolveUserType(base_ty, obj_reg_, program_);
            if (!td)
              throwAt(expr, "unknown user type");
            const std::string& member = expr.bin_op.right->string_lit.value;
            const UserField* uf = lookupUserField(*td, member);
            if (!uf) {
              int idx = obj_reg_.lookupFieldIndex(base_ty, member);
              if (idx < 0)
                throwAt(expr, "unknown member '" + member + "'");
              ty = td->fields[static_cast<size_t>(idx)].type;
            } else {
              ty = uf->type;
            }
          } else {
            ty = TypeDesc::prim(FarTypeId::I64);
          }
        }
        else if (op == "//" &&
                 ((isPrimitiveDesc(lt) && isFloatType(lt.primitive)) ||
                  (isPrimitiveDesc(rt) && isFloatType(rt.primitive))))
          throw FarError("operator '" + op + "' requires integer operands");
        else if (op == "%" &&
                 ((isPrimitiveDesc(lt) && isFloatType(lt.primitive)) ||
                  (isPrimitiveDesc(rt) && isFloatType(rt.primitive))))
          throw FarError("operator '%' requires integer operands");
        else if (isUserDesc(lt)) {
          const UserMethod* om = obj_reg_.lookupMethod(lt, userOpMethodName(op));
          if (om)
            ty = om->return_type;
          else
            ty = promoteNumeric(lt, rt);
        } else
          ty = promoteNumeric(lt, rt);
        if (op == "<<" || op == ">>")
          checkLiteralShiftOverflow(expr, const_locals_);
        break;
      }
      case Expr::AssignExprK: {
        if (expr.assign.target->kind != Expr::Variable && expr.assign.target->kind != Expr::IndexExpr &&
            expr.assign.target->kind != Expr::MemberExpr &&
            !(expr.assign.target->kind == Expr::PrefixExprK &&
              expr.assign.target->prefix.op == "*"))
          throw FarError("assignment requires variable, member, index, or *pointer target");
        if (expr.assign.op.size() == 3 && expr.assign.op[0] == '?' && expr.assign.op[1] == '?' &&
            expr.assign.op[2] == '=') {
          checkExpr(*expr.assign.target);
          ty = expr.assign.target->type;
          TypeDesc val_ty = checkExpr(*expr.assign.value);
          TypeDesc target_ty = expr.assign.target->type;
          if (expr.assign.target->kind == Expr::Variable) {
            auto it = locals_.find(expr.assign.target->var.name);
            if (it != locals_.end())
              target_ty = it->second;
          }
          if (!canAssignTypes(val_ty, target_ty))
            throwAt(expr, "assignment type mismatch");
          if (assignTargetCanBeNullish(*expr.assign.target)) {
            rejectIntLiteralOutOfRangeForTarget(*expr.assign.value, target_ty);
            if (expr.assign.target->kind == Expr::MemberExpr ||
                expr.assign.target->kind == Expr::IndexExpr ||
                (expr.assign.target->kind == Expr::PrefixExprK &&
                 expr.assign.target->prefix.op == "*"))
              rejectNarrowingStore(val_ty, target_ty);
            else if (expr.assign.target->kind == Expr::Variable)
              rejectNarrowingStore(val_ty, target_ty);
            else
              rejectNarrowingAssign(val_ty, target_ty);
            if (expr.assign.target->kind == Expr::Variable)
              trackConstLocal(expr.assign.target->var.name, *expr.assign.value);
          }
          break;
        }
        ty = checkExpr(*expr.assign.value);
        if (expr.assign.target->kind == Expr::Variable &&
            locals_.find(expr.assign.target->var.name) == locals_.end()) {
          if (expr.assign.op != "=")
            throwAt(expr, "undefined variable '" + expr.assign.target->var.name + "'");
          locals_[expr.assign.target->var.name] = ty;
          expr.assign.target->type = ty;
          trackConstLocal(expr.assign.target->var.name, *expr.assign.value);
          break;
        }
        checkExpr(*expr.assign.target);
        TypeDesc target_ty = expr.assign.target->type;
        if (expr.assign.target->kind == Expr::Variable) {
          auto it = locals_.find(expr.assign.target->var.name);
          if (it != locals_.end())
            target_ty = it->second;
        }
        if (expr.assign.op == "=" && expr.assign.target->kind == Expr::Variable)
          maybeWidenInferredLocal(locals_, explicit_locals_, expr.assign.target->var.name, ty,
                                  target_ty);
        rejectIntLiteralOutOfRangeForTarget(*expr.assign.value, target_ty);
        if (expr.assign.op != "=")
          rejectNarrowingStore(ty, target_ty);
        else if (expr.assign.target->kind == Expr::MemberExpr ||
                 expr.assign.target->kind == Expr::IndexExpr ||
                 (expr.assign.target->kind == Expr::PrefixExprK &&
                  expr.assign.target->prefix.op == "*"))
          rejectNarrowingStore(ty, target_ty);
        else if (expr.assign.target->kind == Expr::Variable &&
                 (explicit_locals_.count(expr.assign.target->var.name) ||
                  (isPrimitiveDesc(target_ty) && isFloatType(target_ty.primitive)) ||
                  locals_.count(expr.assign.target->var.name)))
          rejectNarrowingStore(ty, target_ty);
        if (expr.assign.target->kind == Expr::IndexExpr) {
          TypeDesc arr_ty = expr.assign.target->index.array->type;
          if (arr_ty.form == TypeForm::Dict) {
            TypeDesc key_ty = arr_ty.args[0];
            TypeDesc val_ty = arr_ty.args[1];
            TypeDesc idx_ty = expr.assign.target->index.index->type;
            if (!canAssignTypes(idx_ty, key_ty))
              throwAt(expr, "dict key type mismatch");
            if (!canAssignTypes(ty, val_ty))
              throwAt(expr, "dict value type mismatch");
          } else if (isUserDesc(arr_ty)) {
            const UserMethod* im = obj_reg_.lookupMethod(arr_ty, "__index_set");
            if (!im)
              throwAt(expr, "type " + arr_ty.user_name + " has no index setter");
            if (im->params.size() >= 3) {
              TypeDesc idx_ty = expr.assign.target->index.index->type;
              if (!canAssignTypes(idx_ty, im->params[1].type))
                throwAt(expr, "index assignment key type mismatch");
              if (!canAssignTypes(ty, im->params[2].type))
                throwAt(expr, "index assignment value type mismatch");
            }
          } else if (isPrim(arr_ty, FarTypeId::String) || isPrim(arr_ty, FarTypeId::RawString)) {
            throwAt(expr, "cannot assign to string index: strings are immutable");
          } else if (isIndexable(arr_ty) && !canAssignTypes(ty, elemTypeOf(arr_ty)))
            throwAt(expr, "index assignment type mismatch");
        } else if (!canAssignTypes(ty, target_ty))
          throwAt(expr, "assignment type mismatch");
        if (expr.assign.target->kind == Expr::Variable) {
          if (expr.assign.op == "=")
            trackConstLocal(expr.assign.target->var.name, *expr.assign.value);
          else
            clearConstLocal(expr.assign.target->var.name);
        }
        break;
      }
      case Expr::TernaryExprK: {
        checkExpr(*expr.ternary.cond);
        auto saved = locals_;
        auto branch_const = const_locals_;
        ConstBool cb = constBoolFromExpr(*expr.ternary.cond, const_locals_);
        if (cb == ConstBool::True) {
          ty = checkExpr(*expr.ternary.then_br);
        } else if (cb == ConstBool::False) {
          ty = checkExpr(*expr.ternary.else_br);
        } else {
          TypeDesc a = checkExpr(*expr.ternary.then_br);
          locals_ = saved;
          const_locals_ = branch_const;
          TypeDesc b = checkExpr(*expr.ternary.else_br);
          locals_ = saved;
          const_locals_ = branch_const;
          try {
            ty = unifyTernaryBranches(a, b);
          } catch (const FarError& e) {
            throwAt(expr, e.what());
          }
        }
        locals_ = saved;
        const_locals_ = branch_const;
        break;
      }
      case Expr::PrefixExprK: {
        TypeDesc inner = checkExpr(*expr.prefix.operand);
        if (expr.prefix.op == "*") {
          if (!isPtrLikeDesc(inner))
            throw FarError("dereference requires pointer or reference type");
          ty = pointeeOf(inner);
        } else if (expr.prefix.op == "&") {
          ty = TypeDesc::pointer(inner);
        } else if (expr.prefix.op == "++" || expr.prefix.op == "--") {
          if (expr.prefix.operand->kind != Expr::Variable)
            throwAt(expr, "increment/decrement requires a variable");
          clearConstLocal(expr.prefix.operand->var.name);
          ty = inner;
        } else {
          ty = inner;
        }
        break;
      }
      case Expr::PostfixExprK: {
        TypeDesc inner = checkExpr(*expr.postfix.operand);
        if ((expr.postfix.op == "++" || expr.postfix.op == "--") &&
            expr.postfix.operand->kind != Expr::Variable)
          throwAt(expr, "increment/decrement requires a variable");
        if ((expr.postfix.op == "++" || expr.postfix.op == "--") &&
            expr.postfix.operand->kind == Expr::Variable)
          clearConstLocal(expr.postfix.operand->var.name);
        if (expr.postfix.op == "!?") {
          if (isOptionDesc(inner) && !inner.args.empty())
            ty = inner.args[0];
          else if (isPointerDesc(inner))
            ty = inner.args[0];
          else
            throwAt(expr, "force unwrap requires optional or pointer type");
        } else {
          ty = inner;
        }
        break;
      }
      case Expr::TypeUnaryExprK:
        if (expr.type_unary.op == "stackalloc") {
          TypeDesc elem = expr.type_unary.has_type ? expr.type_unary.type_arg
                                                   : checkExpr(*expr.type_unary.value);
          if (expr.type_unary.has_type && expr.type_unary.value)
            checkExpr(*expr.type_unary.value);
          if (isUserDesc(elem) || elem.form == TypeForm::Function)
            throwAt(expr, "stackalloc does not support type " + typeDescName(elem));
          if (elemSizeBytes(elem) <= 0)
            throwAt(expr, "stackalloc element type has unknown size");
          ty = TypeDesc::pointer(elem);
        } else if (expr.type_unary.has_type) {
          if (expr.type_unary.op == "typeof")
            ty = TypeDesc::prim(FarTypeId::String);
          else
            ty = TypeDesc::prim(FarTypeId::I64);
        } else {
          checkExpr(*expr.type_unary.value);
          if (expr.type_unary.op == "typeof")
            ty = TypeDesc::prim(FarTypeId::String);
          else
            ty = TypeDesc::prim(FarTypeId::I64);
        }
        break;
      case Expr::IsExprK: {
        TypeDesc scrut_ty = checkExpr(*expr.is_expr.value);
        TypeDesc target = expr.is_expr.type;
        if (isUserDesc(target)) {
          const UserTypeDef* ut = obj_reg_.lookup(target.user_name);
          if (!ut)
            throwAt(expr, "unknown type in 'is' expression");
        }
        (void)scrut_ty;
        ty = TypeDesc::prim(FarTypeId::Bool);
        break;
      }
      case Expr::AsExprK: {
        TypeDesc value_ty = checkExpr(*expr.as_expr.value);
        if (!canCastTypes(value_ty, expr.as_expr.type) &&
            !typeDescEquals(value_ty, expr.as_expr.type))
          throwAt(expr, std::string("cannot cast from ") + typeDescName(value_ty) + " to " +
                             typeDescName(expr.as_expr.type));
        ty = expr.as_expr.type;
        break;
      }
      case Expr::FnCall:
        ty = checkCall(expr);
        break;
      case Expr::CastExpr:
        checkExpr(*expr.cast.value);
        if (expr.cast.target.form == TypeForm::Array)
          throw FarError(std::string("cannot cast to ") + typeDescName(expr.cast.target));
        if (isPrim(expr.cast.target, FarTypeId::String)) {
          if (isPrim(expr.cast.value->type, FarTypeId::String) ||
              isPrim(expr.cast.value->type, FarTypeId::RawString)) {
            ty = expr.cast.target;
            break;
          }
          if (isPrimitiveDesc(expr.cast.value->type)) {
            FarTypeId src = primitiveOf(expr.cast.value->type);
            if (isIntegerType(src) || isFloatType(src) || src == FarTypeId::Bool ||
                src == FarTypeId::Char) {
              ty = expr.cast.target;
              break;
            }
          }
          throw FarError(std::string("cannot cast to ") + typeDescName(expr.cast.target));
        }
        if (isPrim(expr.cast.value->type, FarTypeId::String) ||
            isPrim(expr.cast.value->type, FarTypeId::RawString)) {
          FarTypeId target = primitiveOf(expr.cast.target);
          if (isIntegerType(target) || isFloatType(target) || target == FarTypeId::Bool ||
              target == FarTypeId::Char) {
            ty = expr.cast.target;
            break;
          }
          throw FarError(std::string("cannot cast to ") + typeDescName(expr.cast.target));
        }
        if (isPrim(expr.cast.value->type, FarTypeId::String) ||
            expr.cast.value->type.form == TypeForm::Array)
          throw FarError(std::string("cannot cast from ") + typeDescName(expr.cast.value->type));
        if (isAggregateDesc(expr.cast.target) || isAggregateDesc(expr.cast.value->type))
          throw FarError("cannot cast aggregate types");
        if (isUserDesc(expr.cast.value->type) && isPrimitiveDesc(expr.cast.target)) {
          const UserTypeDef* ut = obj_reg_.lookup(expr.cast.value->type.user_name);
          if (ut && (ut->kind == UserTypeKind::Enum || ut->kind == UserTypeKind::FlagsEnum) &&
              isIntegerType(primitiveOf(expr.cast.target))) {
            ty = expr.cast.target;
            break;
          }
        }
        if (!canCastTypes(expr.cast.value->type, expr.cast.target))
          throw FarError(std::string("cannot cast from ") + typeDescName(expr.cast.value->type) +
                         " to " + typeDescName(expr.cast.target));
        ty = expr.cast.target;
        break;
      case Expr::TypeConstExpr:
        if (isFloatType(expr.type_const.type))
          ty = TypeDesc::prim(expr.type_const.type);
        else if (expr.type_const.type == FarTypeId::Char)
          ty = TypeDesc::prim(FarTypeId::Char);
        else
          ty = TypeDesc::prim(FarTypeId::I64);
        break;
      case Expr::SpawnExpr:
        if (expr.spawn.call->kind == Expr::FnCall)
          checkCall(*expr.spawn.call);
        ty = expr.spawn.as_task ? TypeDesc::task() : TypeDesc::prim(FarTypeId::I64);
        break;
      case Expr::ParallelExpr:
        ty = TypeDesc::prim(FarTypeId::I64);
        break;
      case Expr::IndexExpr: {
        TypeDesc arr_ty = checkExpr(*expr.index.array);
        checkExpr(*expr.index.index);
        if (isUserDesc(arr_ty)) {
          const UserMethod* im = obj_reg_.lookupMethod(arr_ty, "__index_get");
          if (!im)
            throw FarError("type " + arr_ty.user_name + " has no indexer");
          ty = im->return_type;
        } else if (isPrim(arr_ty, FarTypeId::String) || isPrim(arr_ty, FarTypeId::RawString))
          ty = TypeDesc::prim(FarTypeId::Char);
        else if (isIndexable(arr_ty))
          ty = elemTypeOf(arr_ty);
        else if (arr_ty.form == TypeForm::Dict) {
          TypeDesc idx_ty = expr.index.index->type;
          if (!canAssignTypes(idx_ty, arr_ty.args[0]))
            throwAt(expr, "dict key type mismatch");
          ty = arr_ty.args[1];
        } else
          throw FarError("cannot index type " + typeDescName(arr_ty));
        break;
      }
      case Expr::SliceExpr: {
        TypeDesc arr_ty = checkExpr(*expr.slice.array);
        if (expr.slice.start)
          checkExpr(*expr.slice.start);
        if (expr.slice.end)
          checkExpr(*expr.slice.end);
        if (!isIndexable(arr_ty))
          throw FarError("cannot slice type " + typeDescName(arr_ty));
        if (isPrim(arr_ty, FarTypeId::String) || isPrim(arr_ty, FarTypeId::RawString))
          ty = TypeDesc::prim(FarTypeId::String);
        else
          ty = TypeDesc::slice(elemTypeOf(arr_ty));
        break;
      }
      case Expr::ArrayLitExpr:
        for (const auto& el : expr.array_lit.elements)
          checkExpr(*el);
        ty = TypeDesc::array(inferArrayElemType(expr.array_lit.elements));
        break;
      case Expr::DictLitExpr:
        for (const auto& entry : expr.dict_lit.entries) {
          checkExpr(*entry.key);
          checkExpr(*entry.value);
        }
        ty = TypeDesc::dict(inferDictEntryType(expr.dict_lit.entries, true),
                            inferDictEntryType(expr.dict_lit.entries, false));
        break;
      case Expr::TupleLitExpr:
        for (const auto& el : expr.tuple_lit.elements)
          checkExpr(*el);
        {
          std::vector<TypeDesc> fields;
          for (const auto& el : expr.tuple_lit.elements)
            fields.push_back(el->type);
          ty = TypeDesc::tuple(std::move(fields));
        }
        break;
      case Expr::MemberExpr: {
        TypeDesc obj_ty = checkExpr(*expr.member.object);
        if (obj_ty.form == TypeForm::Tuple) {
          if (expr.member.member.size() >= 2 && expr.member.member[0] == '.' &&
              expr.member.member.substr(1).find_first_not_of("0123456789") == std::string::npos) {
            size_t idx = static_cast<size_t>(
                parseIntLiteral(expr.member.member.substr(1), expr.line, expr.col));
            if (idx >= obj_ty.args.size())
              throw FarError("tuple field index out of range");
            ty = obj_ty.args[idx];
          } else {
            throw FarError("tuple field access uses .0, .1, ...");
          }
        } else if (isAggregateDesc(obj_ty)) {
          FarTypeId obj_id = aggregateDescId(obj_ty);
          int idx = lookupFieldIndex(obj_id, expr.member.member);
          if (idx < 0)
            throw FarError("unknown field '" + expr.member.member + "' on " + typeInfo(obj_id).name);
          ty = TypeDesc::prim(aggregateScalar(obj_id));
        } else if (isUserDesc(obj_ty)) {
          std::string getter = "__prop_get_" + expr.member.member;
          if (const UserMethod* gm = obj_reg_.lookupMethod(obj_ty, getter))
            ty = gm->return_type;
          else {
            const UserTypeDef* td = resolveUserType(obj_ty, obj_reg_, program_);
            if (!td)
              throw FarError("unknown user type");
            const UserField* uf = lookupUserField(*td, expr.member.member);
            if (!uf) {
              int idx = obj_reg_.lookupFieldIndex(obj_ty, expr.member.member);
              if (idx < 0)
                throw FarError("unknown member '" + expr.member.member + "' on " + obj_ty.user_name);
              uf = &td->fields[static_cast<size_t>(idx)];
            }
            if (uf->visibility == Visibility::Private &&
                (!current_user_type_ || current_user_type_->name != td->name))
              throw FarError("cannot access private field '" + expr.member.member + "' on " + td->name);
            ty = uf->type;
          }
        } else {
          throw FarError("field access requires aggregate, user, or tuple type");
        }
        break;
      }
      case Expr::MethodExpr:
        ty = checkMethod(expr);
        break;
      case Expr::FnLitExpr:
        ty = checkFnLit(expr);
        break;
      case Expr::AwaitExprK: {
        TypeDesc inner = checkExpr(*expr.await.value);
        (void)inner;
        ty = TypeDesc::prim(FarTypeId::I64);
        break;
      }
      case Expr::EnumVariantExprK: {
        int v = obj_reg_.enumVariantValue(expr.enum_variant.type_name, expr.enum_variant.variant);
        if (v < 0)
          throw FarError("unknown enum variant " + expr.enum_variant.type_name + "." + expr.enum_variant.variant);
        expr.enum_variant.value = v;
        ty = TypeDesc::prim(FarTypeId::I64);
        break;
      }
      case Expr::UnionVariantExprK: {
        const UserTypeDef* td = obj_reg_.lookup(expr.union_variant.type_name);
        if (!td || td->kind != UserTypeKind::Union)
          throw FarError("unknown union type '" + expr.union_variant.type_name + "'");
        const EnumVariant* uv = lookupVariant(*td, expr.union_variant.variant);
        if (!uv)
          throw FarError("unknown union variant " + expr.union_variant.type_name + "." +
                         expr.union_variant.variant);
        if (expr.union_variant.args.size() != uv->fields.size())
          throw FarError("union constructor argument count mismatch");
        if (uv->fields.size() > 8)
          throw FarError("union variants with more than 8 fields are not supported");
        for (size_t i = 0; i < expr.union_variant.args.size(); ++i) {
          TypeDesc at = checkExpr(*expr.union_variant.args[i]);
          rejectNarrowingCallArg(*expr.union_variant.args[i], at, uv->fields[i].type);
          if (!canAssignTypes(at, uv->fields[i].type))
            throwAt(expr, "union constructor argument type mismatch for field '" + uv->fields[i].name + "'");
        }
        expr.union_variant.value = uv->value;
        ty = TypeDesc::user(td->name);
        break;
      }
      case Expr::MacroSubstExprK:
      case Expr::MacroInvokeExprK:
        throw FarError("unexpanded macro in typecheck");
      case Expr::ComptimeExprK: {
        ++comptime_depth_;
        if (expr.comptime_expr.is_block) {
          for (size_t i = 0; i + 1 < expr.comptime_expr.block.size(); ++i)
            checkStmt(*expr.comptime_expr.block[i]);
          const Stmt& last = *expr.comptime_expr.block.back();
          if (last.kind == Stmt::ReturnStmt && last.ret.has_value)
            ty = checkExpr(*last.ret.value);
          else if (last.kind == Stmt::ExprStmtK && last.expr_stmt.expr)
            ty = checkExpr(*last.expr_stmt.expr);
          else
            throwAt(expr, "comptime block must end with an expression or return");
        } else {
          ty = checkExpr(*expr.comptime_expr.value);
        }
        --comptime_depth_;
        break;
      }
    }
    expr.type = ty;
    return ty;
  }

  TypeDesc checkFnLit(Expr& expr) {
    FnLit& lit = expr.fn_lit;
    Function lf;
    lf.is_lambda = true;
    lf.lambda_id = lambda_counter_++;
    lf.name = "__lambda_" + std::to_string(lf.lambda_id);
    lf.return_type = lit.return_type;
    std::unordered_set<std::string> param_names;
    for (const auto& p : lit.params)
      param_names.insert(p.name);
    std::unordered_set<std::string> free;
    if (lit.expr_body)
      collectFreeVarsExpr(*lit.expr_body, free);
    else
      for (const auto& st : lit.body)
        collectFreeVarsStmt(*st, free);
    std::vector<std::string> captures;
    for (const auto& name : free) {
      if (param_names.count(name))
        continue;
      if (locals_.count(name))
        captures.push_back(name);
    }
    if (captures.size() > 4)
      throw FarError("closure captures at most 4 variables");
    std::vector<Param> prefixed;
    for (const auto& cap : captures) {
      Param cp;
      cp.name = cap;
      cp.type = locals_.at(cap);
      prefixed.push_back(std::move(cp));
    }
    for (auto& p : lit.params)
      prefixed.push_back(std::move(p));
    lf.params = std::move(prefixed);
    lf.captures = captures;
    lit.captures = captures;
    if (lit.expr_body) {
      auto ret = std::make_unique<Stmt>();
      ret->kind = Stmt::ReturnStmt;
      ret->ret.has_value = true;
      ret->ret.value = std::move(lit.expr_body);
      lf.body.push_back(std::move(ret));
    } else {
      lf.body = std::move(lit.body);
    }
    lf.llvm_name = mangleFunction(lf);
    program_.synthetic_functions.push_back(std::move(lf));
    Function& stored = program_.synthetic_functions.back();
    if (stored.type_params.empty()) {
      inferUntypedParamTypes(stored);
      inferReturnType(stored);
    }
    expr.fn_lit.id = stored.lambda_id;
    std::vector<TypeDesc> ptypes;
    for (const auto& p : stored.params) {
      if (p.is_variadic)
        continue;
      if (std::find(stored.captures.begin(), stored.captures.end(), p.name) != stored.captures.end())
        continue;
      ptypes.push_back(p.type);
    }
    return TypeDesc::function(std::move(ptypes), stored.return_type);
  }

  TypeDesc checkModuleFunctionCall(Expr& expr, const std::string& fn_name) {
    auto it = fn_overloads_.find(fn_name);
    if (it == fn_overloads_.end() || it->second.empty())
      throw FarError("undefined function '" + fn_name + "'");
    const Function* fn = it->second.front();
    if (expr.method_call.args.size() != fn->params.size())
      throw FarError(fn_name + "() argument count mismatch");
    for (size_t i = 0; i < expr.method_call.args.size(); ++i) {
      TypeDesc at = checkExpr(*expr.method_call.args[i]);
      rejectNarrowingCallArg(*expr.method_call.args[i], at, fn->params[i].type);
      if (!canAssignTypes(at, fn->params[i].type))
        throw FarError(fn_name + "() argument type mismatch");
    }
    expr.method_call.is_module_call = true;
    expr.method_call.resolved_fn = fn_name;
    expr.method_call.resolved = fn;
    expr.method_call.resolved_llvm_name = mangleFunction(*fn);
    return fn->return_type;
  }

  TypeDesc checkModuleTypeConstruct(Expr& expr, const UserTypeDef& ut) {
    if (userTypeHasConstructor(ut)) {
      const UserMethod* ctor = lookupUserConstructor(ut, expr.method_call.args.size());
      if (!ctor)
        throw FarError(ut.name + "() has no constructor matching " + std::to_string(expr.method_call.args.size()) +
                       " argument(s)");
      const size_t nargs = userMethodCallArgCount(*ctor);
      if (expr.method_call.args.size() != nargs)
        throw FarError(ut.name + "() constructor expects " + std::to_string(nargs) + " argument(s)");
      for (size_t i = 0; i < expr.method_call.args.size(); ++i) {
        TypeDesc at = checkExpr(*expr.method_call.args[i]);
        const Param& p = ctor->params[i + 1];
        rejectNarrowingCallArg(*expr.method_call.args[i], at, p.type);
        if (!canAssignTypes(at, p.type))
          throw FarError(ut.name + "() constructor argument type mismatch for '" + p.name + "'");
      }
      expr.method_call.is_type_construct = true;
      expr.method_call.resolved_ctor = ctor;
      return TypeDesc::user(ut.name);
    }
    if (expr.method_call.args.size() != ut.fields.size())
      throw FarError(ut.name + "() expects " + std::to_string(ut.fields.size()) + " argument(s)");
    for (size_t i = 0; i < expr.method_call.args.size(); ++i) {
      TypeDesc at = checkExpr(*expr.method_call.args[i]);
      rejectNarrowingCallArg(*expr.method_call.args[i], at, ut.fields[i].type);
      if (!canAssignTypes(at, ut.fields[i].type))
        throw FarError(ut.name + "() argument type mismatch for field '" + ut.fields[i].name + "'");
    }
    expr.method_call.is_type_construct = true;
    return TypeDesc::user(ut.name);
  }

  TypeDesc checkGeomStaticMethod(Expr& expr, FarTypeId agg_type, const std::string& method_name) {
    const MethodInfo* mi = lookupGeomMethod(agg_type, method_name);
    if (!mi)
      throw FarError("unknown static method '" + method_name + "' on " + geomClassName(agg_type));
    const size_t expected = static_cast<size_t>(mi->nargs) + 1;
    if (expr.method_call.args.size() != expected)
      throw FarError(method_name + "() expects " + std::to_string(expected) + " argument(s)");
    TypeDesc obj_ty = TypeDesc::prim(agg_type);
    for (size_t i = 0; i < expr.method_call.args.size(); ++i) {
      TypeDesc arg_ty = checkExpr(*expr.method_call.args[i]);
      TypeDesc expected_ty = TypeDesc::prim(FarTypeId::F64);
      if (i == 0) {
        expected_ty = obj_ty;
      } else if (mi->id == AggMethodId::Dot || mi->id == AggMethodId::Distance ||
                 mi->id == AggMethodId::Distance2 || mi->id == AggMethodId::Min || mi->id == AggMethodId::Max ||
                 mi->id == AggMethodId::Cross || mi->id == AggMethodId::DistanceTo ||
                 mi->id == AggMethodId::Translate || mi->id == AggMethodId::Intersects) {
        expected_ty = mi->ret == FarTypeId::Bool ? TypeDesc::prim(FarTypeId::F64) : obj_ty;
      } else if (mi->id == AggMethodId::Clamp && i > 0) {
        expected_ty = obj_ty;
      } else if (mi->id == AggMethodId::Contains && i == 1) {
        if (isBoundsFamily(agg_type))
          expected_ty = TypeDesc::prim(FarTypeId::FVec3);
        else if (isRectFamily(agg_type) && aggregateScalar(agg_type) == FarTypeId::F32)
          expected_ty = TypeDesc::prim(FarTypeId::FVec2);
        else
          expected_ty = TypeDesc::prim(pointTypeForScalar(aggregateScalar(agg_type)));
      } else if (mi->id == AggMethodId::Expand && i == 1) {
        expected_ty = TypeDesc::prim(aggregateScalar(agg_type));
      } else if (mi->id == AggMethodId::QuatMul && i == 1) {
        expected_ty = obj_ty;
      } else if ((mi->id == AggMethodId::Dot || mi->id == AggMethodId::Cross || mi->id == AggMethodId::Min ||
                  mi->id == AggMethodId::Max) &&
                 i == 1) {
        expected_ty = obj_ty;
      } else if (mi->id == AggMethodId::ApproxEq && i == 1) {
        expected_ty = obj_ty;
      } else if (mi->id == AggMethodId::ApproxEq && i == 2) {
        expected_ty = TypeDesc::prim(FarTypeId::F64);
      }
      bool arg_ok = false;
      if (mi->id == AggMethodId::MatMul && i == 1) {
        arg_ok = typeDescEquals(arg_ty, obj_ty) ||
                 (isAggregateDesc(arg_ty) && isVecFamily(aggregateDescId(arg_ty)) &&
                  aggregateMatDim(agg_type) == aggregateDim(aggregateDescId(arg_ty)) &&
                  aggregateScalar(agg_type) == aggregateScalar(aggregateDescId(arg_ty)));
      } else {
        arg_ok = methodArgMatches(arg_ty, expected_ty, obj_ty);
      }
      if (!arg_ok)
        throw FarError("argument type mismatch in " + method_name + "()");
    }
    TypeDesc ty = TypeDesc::prim(mi->ret);
    if (mi->id == AggMethodId::Normalize || mi->id == AggMethodId::Min || mi->id == AggMethodId::Max ||
        mi->id == AggMethodId::Clamp)
      ty = obj_ty;
    if (mi->id == AggMethodId::Translate || mi->id == AggMethodId::Center)
      ty = TypeDesc::prim(pointTypeForScalar(aggregateScalar(agg_type)));
    if (mi->id == AggMethodId::Expand) {
      if (isBoundsFamily(agg_type))
        ty = TypeDesc::prim(FarTypeId::Bounds);
      else
        ty = TypeDesc::prim(rectTypeForScalar(aggregateScalar(agg_type)));
    }
    if (mi->id == AggMethodId::BoundsSize)
      ty = TypeDesc::prim(FarTypeId::FVec3);
    if (mi->id == AggMethodId::ToColor)
      ty = TypeDesc::prim(FarTypeId::Color);
    if (mi->id == AggMethodId::MatMul && isVecFamily(aggregateDescId(checkExpr(*expr.method_call.args[1]))))
      ty = TypeDesc::prim(vecTypeForDim(aggregateScalar(agg_type), aggregateMatDim(agg_type)));
    expr.method_call.is_geom_call = true;
    expr.method_call.geom_agg_type = agg_type;
    return ty;
  }

  TypeDesc checkNamespaceGeomStaticMethod(Expr& expr, const UserTypeDef& facade,
                                          const std::string& method_name) {
    std::vector<TypeDesc> arg_types;
    arg_types.reserve(expr.method_call.args.size());
    for (const auto& a : expr.method_call.args)
      arg_types.push_back(checkExpr(*a));

    const UserMethod* match = nullptr;
    for (const auto& m : facade.methods) {
      if (!m.is_static || m.name != method_name)
        continue;
      if (m.params.size() != arg_types.size())
        continue;
      bool ok = true;
      for (size_t i = 0; i < arg_types.size(); ++i) {
        if (!canAssignTypes(arg_types[i], m.params[i].type)) {
          ok = false;
          break;
        }
      }
      if (!ok)
        continue;
      match = &m;
      break;
    }
    if (!match)
      throw FarError("no matching overload for " + facade.name + "." + method_name + "()");
    if (arg_types.empty())
      throw FarError(facade.name + "." + method_name + "() requires at least one argument");
    FarTypeId agg = FarTypeId::Void;
    if (isUserDesc(arg_types[0]))
      agg = lookupGeomAggType(arg_types[0].user_name);
    if (!isAggregateType(agg) && isAggregateDesc(arg_types[0]))
      agg = aggregateDescId(arg_types[0]);
    if (!isAggregateType(agg) || !lookupGeomMethod(agg, method_name))
      return checkModuleFunctionCall(expr, userMangleMethod(facade.name, method_name));
    return checkGeomStaticMethod(expr, agg, method_name);
  }

  TypeDesc checkStaticTypeMethod(Expr& expr, const UserTypeDef& ut, const UserMethod& m) {
    if (FarTypeId agg = lookupGeomAggType(ut.name); isAggregateType(agg)) {
      if (lookupGeomMethod(agg, m.name))
        throw FarError(geomNamespaceMethodHint(agg, m.name));
    }
    if (m.visibility == Visibility::Private) {
      if (!current_user_type_ || current_user_type_->name != ut.name)
        throw FarError("cannot call private static method '" + m.name + "' on " + ut.name);
    }
    return checkModuleFunctionCall(expr, userMangleMethod(ut.name, m.name));
  }

  const UserTypeDef* lookupModuleFacadeType(const ModuleAlias& mod, const std::string& class_name) const {
    for (const auto& td : program_.user_types) {
      if (td.module_name == mod.module_name && td.name == class_name)
        return &td;
    }
    return nullptr;
  }

  TypeDesc checkMethod(Expr& expr) {
    if (expr.method_call.object->kind == Expr::Variable) {
      const std::string& base = expr.method_call.object->var.name;
      if (!locals_.count(base) && program_.module_aliases.count(base)) {
        const ModuleAlias& mod = program_.module_aliases.at(base);
        const std::string flat = stdlibModuleFlatName(mod.module_name);
        if (isStdlibFunctionModule(flat) || isStdlibTypeModule(flat)) {
          auto fit = mod.flat_methods.find(expr.method_call.method);
          if (fit != mod.flat_methods.end()) {
            if (const UserTypeDef* ut = lookupModuleFacadeType(mod, fit->second)) {
              if (isStdlibTypeModule(flat))
                return checkNamespaceGeomStaticMethod(expr, *ut, expr.method_call.method);
              if (const UserMethod* sm = lookupStaticMethod(obj_reg_, *ut, expr.method_call.method))
                return checkStaticTypeMethod(expr, *ut, *sm);
            }
          }
          auto sit = mod.symbols.find(expr.method_call.method);
          if (sit != mod.symbols.end()) {
            if (const UserTypeDef* ut = lookupModuleFacadeType(mod, sit->second))
              return checkModuleTypeConstruct(expr, *ut);
            throw FarError("module '" + base + "' does not export '" + expr.method_call.method + "'");
          }
          throw FarError("module alias '" + base + "' has no symbol '" + expr.method_call.method + "'");
        }
      }
      if (!locals_.count(base)) {
        if (const UserTypeDef* ut = obj_reg_.lookup(base)) {
          if (FarTypeId agg = lookupGeomAggType(ut->name); isAggregateType(agg)) {
            if (lookupGeomMethod(agg, expr.method_call.method))
              throw FarError(geomNamespaceMethodHint(agg, expr.method_call.method));
          }
          if (isStdlibTypeModule(ut->name))
            return checkNamespaceGeomStaticMethod(expr, *ut, expr.method_call.method);
          if (const UserMethod* sm = lookupStaticMethod(obj_reg_, *ut, expr.method_call.method))
            return checkStaticTypeMethod(expr, *ut, *sm);
        }
      }
    }
    TypeDesc obj_ty = checkExpr(*expr.method_call.object);
    if (expr.method_call.method == "len")
      throw FarError("use len(value) instead of value.len() — len is a global function");
    if (isConcurrencyHandleDesc(obj_ty)) {
      const ConcMethodInfo* mi = lookupConcMethod(obj_ty.form, expr.method_call.method);
      if (!mi)
        throw FarError("unknown method '" + expr.method_call.method + "' on " + typeDescName(obj_ty));
      if (static_cast<int>(expr.method_call.args.size()) != mi->nargs)
        throw FarError(expr.method_call.method + "() argument count mismatch");
      TypeDesc arg_ty = TypeDesc::prim(FarTypeId::I64);
      for (const auto& a : expr.method_call.args)
        arg_ty = checkExpr(*a);
      return concMethodRetType(obj_ty.form, mi->id, obj_ty, arg_ty);
    }
    if (isOptionDesc(obj_ty) || isResultDesc(obj_ty)) {
      const ErrMethodInfo* mi = isOptionDesc(obj_ty) ? lookupOptionMethod(expr.method_call.method)
                                                     : lookupResultMethod(expr.method_call.method);
      if (!mi)
        throw FarError("unknown method '" + expr.method_call.method + "' on " + typeDescName(obj_ty));
      if (static_cast<int>(expr.method_call.args.size()) != mi->nargs)
        throw FarError(expr.method_call.method + "() argument count mismatch");
      TypeDesc arg_ty = TypeDesc::prim(FarTypeId::I64);
      for (const auto& a : expr.method_call.args)
        arg_ty = checkExpr(*a);
      return errMethodRetType(mi->id, obj_ty, arg_ty);
    }
    if (isMemoryHandleDesc(obj_ty)) {
      const MemMethodInfo* mi = lookupMemMethod(obj_ty.form, expr.method_call.method);
      if (!mi)
        throw FarError("unknown method '" + expr.method_call.method + "' on " + typeDescName(obj_ty));
      if (static_cast<int>(expr.method_call.args.size()) != mi->nargs)
        throw FarError(expr.method_call.method + "() argument count mismatch");
      TypeDesc arg_ty = TypeDesc::prim(FarTypeId::I64);
      for (const auto& a : expr.method_call.args)
        arg_ty = checkExpr(*a);
      return memMethodRetType(obj_ty.form, mi->id, obj_ty, arg_ty);
    }
    if (isPrim(obj_ty, FarTypeId::String) || isPrim(obj_ty, FarTypeId::RawString)) {
      const StrMethodInfo* mi = lookupStrMethod(expr.method_call.method);
      if (!mi)
        throw FarError("unknown method '" + expr.method_call.method + "' on string");
      if (static_cast<int>(expr.method_call.args.size()) != mi->nargs)
        throw FarError(expr.method_call.method + "() expects " + std::to_string(mi->nargs) + " argument(s)");
      for (const auto& a : expr.method_call.args) {
        TypeDesc at = checkExpr(*a);
        if (mi->nargs > 0 && !isPrim(at, FarTypeId::String) && !isPrim(at, FarTypeId::Char))
          throw FarError(expr.method_call.method + "() argument 1 must be string");
      }
      return strMethodRetType(mi->id);
    }
    if (isUserDesc(obj_ty)) {
      const UserTypeDef* ut = obj_reg_.lookup(obj_ty.user_name);
      if (ut && ut->kind == UserTypeKind::Actor) {
        const ConcMethodInfo* am = lookupActorMethod(expr.method_call.method);
        if (!am)
          throw FarError("unknown actor method '" + expr.method_call.method + "'");
        if (static_cast<int>(expr.method_call.args.size()) != am->nargs)
          throw FarError(expr.method_call.method + "() argument count mismatch");
        for (const auto& a : expr.method_call.args)
          checkExpr(*a);
        return TypeDesc::prim(FarTypeId::I64);
      }
      const UserMethod* m = obj_reg_.lookupMethod(obj_ty, expr.method_call.method);
      if (!m)
        m = obj_reg_.lookupExtension(obj_ty.user_name, expr.method_call.method);
      if (!m)
        throw FarError("unknown method '" + expr.method_call.method + "' on " + obj_ty.user_name);
      if (m->is_static) {
        const UserTypeDef* ut = obj_reg_.lookup(userTypeKey(obj_ty));
        if (!ut)
          throw FarError("unknown type for static method '" + expr.method_call.method + "'");
        return checkStaticTypeMethod(expr, *ut, *m);
      }
      if (m->visibility == Visibility::Private) {
        const UserTypeDef* ut = resolveUserType(obj_ty, obj_reg_, program_);
        if (!ut || !current_user_type_ || current_user_type_->name != ut->name)
          throw FarError("cannot call private method '" + expr.method_call.method + "' on " + obj_ty.user_name);
      }
      const size_t nargs = userMethodCallArgCount(*m);
      if (expr.method_call.args.size() != nargs)
        throw FarError(expr.method_call.method + "() argument count mismatch");
      size_t param_base = 0;
      if (!m->params.empty() && (m->params[0].name == "this" || m->params[0].name == "self"))
        param_base = 1;
      for (size_t i = 0; i < expr.method_call.args.size(); ++i) {
        TypeDesc at = checkExpr(*expr.method_call.args[i]);
        size_t pidx = param_base + i;
        if (pidx < m->params.size()) {
          rejectNarrowingCallArg(*expr.method_call.args[i], at, m->params[pidx].type);
          if (!canAssignTypes(at, m->params[pidx].type))
            throw FarError(expr.method_call.method + "() argument type mismatch for '" +
                           m->params[pidx].name + "'");
        }
      }
      return m->return_type;
    }
    if (isCollectionDesc(obj_ty)) {
      const CollMethodInfo* mi = lookupCollMethod(obj_ty.form, expr.method_call.method);
      if (!mi)
        throw FarError("unknown method '" + expr.method_call.method + "' on " + typeDescName(obj_ty));
      if (static_cast<int>(expr.method_call.args.size()) != mi->nargs)
        throw FarError(expr.method_call.method + "() expects " + std::to_string(mi->nargs) + " argument(s)");
      TypeDesc arg_ty = TypeDesc::prim(FarTypeId::I64);
      for (const auto& a : expr.method_call.args)
        arg_ty = checkExpr(*a);
      (void)arg_ty;
      return collMethodRetType(obj_ty.form, mi->id, obj_ty, arg_ty);
    }
    if (!isAggregateDesc(obj_ty))
      throw FarError("method call requires aggregate or collection type");
    FarTypeId obj_id = aggregateDescId(obj_ty);
    const MethodInfo* mi = lookupMethod(obj_id, expr.method_call.method);
    if (mi) {
      throw FarError(geomInstanceMethodHint(obj_id, expr.method_call.method, mi->nargs));
    }
    throw FarError("unknown method '" + expr.method_call.method + "' on " + typeInfo(obj_id).name);
  }

  TypeDesc checkCall(Expr& expr) {
    Call& call = expr.call;
    if (const ConstructorInfo* ctor = lookupConstructor(call.name)) {
      if (static_cast<int>(call.args.size()) != ctor->nargs)
        throw FarError(std::string(ctor->name) + "() expects " + std::to_string(ctor->nargs) + " argument(s)");
      FarTypeId sc = aggregateScalar(ctor->ret);
      for (const auto& a : call.args) {
        TypeDesc at = checkExpr(*a.value);
        TypeDesc expect = TypeDesc::prim(sc);
        rejectNarrowingCallArg(*a.value, at, expect);
        if (!canAssignTypes(at, expect) &&
            !(sc == FarTypeId::F32 && isPrim(at, FarTypeId::F64)))
          throw FarError(std::string("constructor ") + ctor->name + "() argument type mismatch");
      }
      return TypeDesc::prim(ctor->ret);
    }
    if (const CollConstructorInfo* cc = lookupCollConstructor(call.name)) {
      return checkCollConstructor(call, cc, expr.type);
    }
    if (const MemConstructorInfo* mc = lookupMemConstructor(call.name)) {
      if (mc->has_elem_type) {
        if (call.type_args.empty())
          throw FarError(call.name + " requires type argument");
        if (call.args.size() != static_cast<size_t>(mc->nargs))
          throw FarError(call.name + "() argument count mismatch");
        TypeDesc elem_ty = call.type_args[0];
        for (const auto& a : call.args) {
          TypeDesc at = checkExpr(*a.value);
          if (!canAssignTypes(at, elem_ty))
            throw FarError(call.name + "() argument type mismatch");
          rejectNarrowingCallArg(*a.value, at, elem_ty);
        }
        if (mc->form == TypeForm::Box)
          return TypeDesc::box(call.type_args[0]);
        if (mc->form == TypeForm::Rc)
          return TypeDesc::rc(call.type_args[0]);
        return TypeDesc::memPool(call.type_args[0]);
      }
      if (call.args.size() != static_cast<size_t>(mc->nargs))
        throw FarError(call.name + "() argument count mismatch");
      for (const auto& a : call.args)
        checkExpr(*a.value);
      return TypeDesc::arena();
    }
    if (const ConcConstructorInfo* cc = lookupConcConstructor(call.name)) {
      if (cc->has_elem_type) {
        if (call.type_args.empty())
          throw FarError(call.name + " requires type argument");
        if (call.args.size() != static_cast<size_t>(cc->nargs))
          throw FarError(call.name + "() argument count mismatch");
        TypeDesc elem_ty = call.type_args[0];
        for (const auto& a : call.args) {
          TypeDesc at = checkExpr(*a.value);
          if (!canAssignTypes(at, elem_ty))
            throw FarError(call.name + "() argument type mismatch");
          rejectNarrowingCallArg(*a.value, at, elem_ty);
        }
        if (cc->form == TypeForm::Channel)
          return TypeDesc::channel(call.type_args[0]);
        if (cc->form == TypeForm::Atomic)
          return TypeDesc::atomic(call.type_args[0]);
        return TypeDesc::lockFreeQueue(call.type_args[0]);
      }
      if (call.args.size() != static_cast<size_t>(cc->nargs))
        throw FarError(call.name + "() argument count mismatch");
      for (const auto& a : call.args)
        checkExpr(*a.value);
      if (cc->form == TypeForm::Mutex)
        return TypeDesc::mutex();
      if (cc->form == TypeForm::Semaphore)
        return TypeDesc::semaphore();
      if (cc->form == TypeForm::ThreadPool)
        return TypeDesc::threadPool();
      return TypeDesc::task();
    }
    if (const ErrConstructorInfo* ec = lookupErrConstructor(call.name)) {
      if (call.args.size() != static_cast<size_t>(ec->nargs))
        throw FarError(call.name + "() argument count mismatch");
      TypeDesc arg_ty = TypeDesc::prim(FarTypeId::I64);
      for (const auto& a : call.args)
        arg_ty = checkExpr(*a.value);
      if (isPrimitiveDesc(arg_ty) && isIntegerType(arg_ty.primitive))
        arg_ty = TypeDesc::prim(FarTypeId::I64);
      if (ec->is_some)
        return TypeDesc::optional(arg_ty);
      if (ec->nargs == 0)
        return TypeDesc::optional(TypeDesc::prim(FarTypeId::I64));
      if (ec->is_ok)
        return TypeDesc::result(arg_ty, TypeDesc::prim(FarTypeId::I64));
      return TypeDesc::result(TypeDesc::prim(FarTypeId::I64), arg_ty);
    }
    if (call.name == "panic") {
      if (call.args.size() != 1)
        throw FarError("panic() expects 1 argument");
      checkExpr(*call.args[0].value);
      return TypeDesc::prim(FarTypeId::I64);
    }
    if (call.name == "assert") {
      if (call.args.size() < 1 || call.args.size() > 2)
        throw FarError("assert() expects 1 or 2 arguments");
      checkExpr(*call.args[0].value);
      if (call.args.size() == 2)
        checkExpr(*call.args[1].value);
      return TypeDesc::prim(FarTypeId::I64);
    }
    if (call.name == "stack_trace") {
      if (!call.args.empty())
        throw FarError("stack_trace() expects no arguments");
      return TypeDesc::prim(FarTypeId::I64);
    }
    if (call.name == "alloc") {
      if (call.args.size() != 1)
        throw FarError("alloc() expects 1 argument");
      checkExpr(*call.args[0].value);
      return TypeDesc::pointer(TypeDesc::prim(FarTypeId::I8));
    }
    if (call.name == "free") {
      if (unsafe_depth_ <= 0)
        throw FarError("free() only allowed in unsafe blocks");
      if (call.args.size() != 1)
        throw FarError("free() expects 1 argument");
      checkExpr(*call.args[0].value);
      return TypeDesc::prim(FarTypeId::I64);
    }
    if (call.name == "realloc") {
      if (unsafe_depth_ <= 0)
        throw FarError("realloc() only allowed in unsafe blocks");
      if (call.args.size() != 2)
        throw FarError("realloc() expects 2 arguments");
      checkExpr(*call.args[0].value);
      checkExpr(*call.args[1].value);
      return TypeDesc::pointer(TypeDesc::prim(FarTypeId::I8));
    }
    if (call.name == "borrow") {
      if (call.args.size() != 1)
        throw FarError("borrow() expects 1 argument");
      TypeDesc inner = checkExpr(*call.args[0].value);
      return TypeDesc::borrowRef(inner);
    }
    if (call.name == "move") {
      if (call.args.size() != 1)
        throw FarError("move() expects 1 argument");
      TypeDesc inner = checkExpr(*call.args[0].value);
      if (isBoxDesc(inner) && !inner.args.empty())
        return TypeDesc::pointer(inner.args[0]);
      return inner;
    }
    if (call.name == "drop") {
      if (call.args.size() != 1)
        throw FarError("drop() expects 1 argument");
      checkExpr(*call.args[0].value);
      return TypeDesc::prim(FarTypeId::I64);
    }
    if (auto suffix = enclosingMethodSuffix(current_fn_); suffix && call.name == *suffix) {
      if (const BuiltinInfo* builtin = lookupBuiltin(call.name)) {
        rejectDisallowedGlobalCall(call.name, current_fn_);
        checkBuiltinArgs(builtin, call.args, [&](Expr& e) {
          TypeDesc td = checkExpr(e);
          if (td.form == TypeForm::Array || isCollectionHandle(td))
            return FarTypeId::Arr;
          if (isPrimitiveDesc(td))
            return td.primitive;
          return FarTypeId::I64;
        });
        return TypeDesc::prim(builtin->ret);
      }
    }
    if (fn_overloads_.count(call.name)) {
      bool callable = false;
      for (const Function* f : fn_overloads_[call.name]) {
        if (f->link_public || current_fn_) {
          callable = true;
          break;
        }
      }
      if (!callable)
        throwAt(expr, "undefined function '" + call.name + "'");
      BoundCall bc = resolveCall(call.name, call, fn_overloads_, [&](Expr& e) { return checkExpr(e); },
                                 program_, &obj_reg_);
      if (bc.fn->is_consteval && comptime_depth_ == 0)
        throw FarError("consteval function '" + call.name + "' must be called via comptime");
      if (bc.fn->is_async)
        return TypeDesc::prim(FarTypeId::I64);
      if (bc.fn->is_generator || bc.fn->is_coroutine)
        return TypeDesc::prim(FarTypeId::I64);
      return bc.fn->return_type;
    }
    if (const BuiltinInfo* builtin = lookupBuiltin(call.name)) {
      rejectDisallowedGlobalCall(call.name, current_fn_);
      checkBuiltinArgs(builtin, call.args, [&](Expr& e) {
        TypeDesc td = checkExpr(e);
        if (td.form == TypeForm::Array || isCollectionHandle(td))
          return FarTypeId::Arr;
        if (isPrimitiveDesc(td))
          return td.primitive;
        return FarTypeId::I64;
      });
      return TypeDesc::prim(builtin->ret);
    }
    if (const UserTypeDef* ut = obj_reg_.lookup(call.name)) {
      if (ut->kind == UserTypeKind::Enum || ut->kind == UserTypeKind::FlagsEnum)
        throw FarError("use Type.Variant for enum values");
      if (ut->kind == UserTypeKind::Union)
        throw FarError("use Type.Variant(...) for union values");
      if (ut->kind == UserTypeKind::Interface || ut->kind == UserTypeKind::Trait)
        throw FarError("cannot construct interface/trait " + call.name);
      if (ut->kind == UserTypeKind::Actor) {
        for (const auto& a : call.args)
          checkExpr(*a.value);
        return TypeDesc::user(ut->name);
      }
      if (!ut->type_params.empty()) {
        std::vector<TypeDesc> gargs;
        if (!call.type_args.empty()) {
          if (call.type_args.size() != ut->type_params.size())
            throw FarError("generic type argument count mismatch for " + call.name);
          gargs = call.type_args;
        } else if (!inferUserTypeArgs(*ut, call, [&](Expr& e) { return checkExpr(e); }, gargs)) {
          throw FarError("cannot infer generic type arguments for " + call.name);
        }
        const UserTypeDef* mono = findOrCreateUserMono(program_, obj_reg_, *ut, gargs);
        if (userTypeHasConstructor(*mono)) {
          const UserMethod* ctor = lookupUserConstructor(*mono, call.args.size());
          if (!ctor)
            throw FarError(call.name + "() has no constructor matching " +
                           std::to_string(call.args.size()) + " argument(s)");
          const size_t nargs = userMethodCallArgCount(*ctor);
          if (call.args.size() != nargs)
            throw FarError(call.name + "() constructor expects " + std::to_string(nargs) +
                           " argument(s)");
          for (size_t i = 0; i < call.args.size(); ++i) {
            TypeDesc at = checkExpr(*call.args[i].value);
            const Param& p = ctor->params[i + 1];
            rejectNarrowingCallArg(*call.args[i].value, at, p.type);
            if (!canAssignTypes(at, p.type))
              throw FarError(call.name + "() constructor argument type mismatch for '" + p.name +
                             "'");
          }
          call.resolved_ctor = ctor;
          return userTypeDesc(ut->name, gargs);
        }
        if (call.args.size() != mono->fields.size())
          throw FarError(call.name + "() expects " + std::to_string(mono->fields.size()) + " argument(s)");
        for (size_t i = 0; i < call.args.size(); ++i) {
          TypeDesc at = checkExpr(*call.args[i].value);
          rejectNarrowingCallArg(*call.args[i].value, at, mono->fields[i].type);
          if (!canAssignTypes(at, mono->fields[i].type))
            throw FarError(call.name + "() argument type mismatch");
        }
        return userTypeDesc(ut->name, gargs);
      }
      if (ut->kind == UserTypeKind::Exception) {
        if (call.args.size() != ut->fields.size())
          throw FarError(call.name + "() expects " + std::to_string(ut->fields.size()) + " argument(s)");
        for (size_t i = 0; i < call.args.size(); ++i) {
          TypeDesc at = checkExpr(*call.args[i].value);
          rejectNarrowingCallArg(*call.args[i].value, at, ut->fields[i].type);
          if (!canAssignTypes(at, ut->fields[i].type))
            throw FarError(call.name + "() argument type mismatch for field '" + ut->fields[i].name +
                           "'");
        }
        return TypeDesc::user(ut->name);
      }
      if (userTypeHasConstructor(*ut)) {
        const UserMethod* ctor = lookupUserConstructor(*ut, call.args.size());
        if (!ctor)
          throw FarError(call.name + "() has no constructor matching " + std::to_string(call.args.size()) +
                         " argument(s)");
        const size_t nargs = userMethodCallArgCount(*ctor);
        if (call.args.size() != nargs)
          throw FarError(call.name + "() constructor expects " + std::to_string(nargs) + " argument(s)");
        for (size_t i = 0; i < call.args.size(); ++i) {
          TypeDesc at = checkExpr(*call.args[i].value);
          const Param& p = ctor->params[i + 1];
          rejectNarrowingCallArg(*call.args[i].value, at, p.type);
          if (!canAssignTypes(at, p.type))
            throw FarError(call.name + "() constructor argument type mismatch for '" + p.name + "'");
        }
        call.resolved_ctor = ctor;
        return TypeDesc::user(ut->name);
      }
      if (call.args.size() != ut->fields.size())
        throw FarError(call.name + "() expects " + std::to_string(ut->fields.size()) + " argument(s)");
      for (size_t i = 0; i < call.args.size(); ++i) {
        TypeDesc at = checkExpr(*call.args[i].value);
        rejectNarrowingCallArg(*call.args[i].value, at, ut->fields[i].type);
        if (!canAssignTypes(at, ut->fields[i].type))
          throw FarError(call.name + "() argument type mismatch for field '" + ut->fields[i].name + "'");
      }
      return TypeDesc::user(ut->name);
    }
    if (call.name == "reflect_kind" || call.name == "reflect_fields" || call.name == "reflect_name" ||
        call.name == "reflect_compile_value") {
      if (call.args.size() != 1)
        throw FarError(call.name + "() expects 1 argument");
      checkExpr(*call.args[0].value);
      return TypeDesc::prim(FarTypeId::I64);
    }
    if (call.name == "print") {
      for (const auto& a : call.args)
        checkExpr(*a.value);
      return TypeDesc::prim(FarTypeId::I64);
    }
    if (isLegacyGlobalOnly(call.name)) {
      if (!allowLegacyRuntimeCall(current_fn_))
        rejectDisallowedGlobalCall(call.name, current_fn_);
      for (const auto& a : call.args)
        checkExpr(*a.value);
      return TypeDesc::prim(FarTypeId::I64);
    }
    if (call.name == "input") {
      if (call.args.size() > 1)
        throw FarError("input() expects 0 or 1 argument");
      if (call.args.size() == 1) {
        TypeDesc arg = checkExpr(*call.args[0].value);
        if (!isPrim(arg, FarTypeId::String))
          throw FarError("input() prompt must be a string");
      }
      return TypeDesc::prim(FarTypeId::String);
    }
    if (call.name == "len") {
      if (call.args.size() != 1)
        throw FarError("len() expects 1 argument");
      TypeDesc arg = checkExpr(*call.args[0].value);
      if (isPrim(arg, FarTypeId::String) || isCollectionDesc(arg) || isPrim(arg, FarTypeId::Arr))
        return TypeDesc::prim(FarTypeId::I64);
      throw FarError("len() requires string or collection");
    }
    if (call.name == "Range" || call.name == "range") {
      if (call.args.size() < 2 || call.args.size() > 3)
        throw FarError("Range() expects 2 or 3 arguments");
      for (const auto& a : call.args)
        checkExpr(*a.value);
      return TypeDesc::range();
    }
    if (auto loc = locals_.find(call.name); loc != locals_.end() && isFunctionDesc(loc->second)) {
      const TypeDesc& ft = loc->second;
      size_t nparams = ft.args.empty() ? 0 : ft.args.size() - 1;
      if (call.args.size() != nparams)
        throw FarError("function value argument count mismatch");
      call.is_hof_call = true;
      call.bound_exprs.clear();
      for (size_t i = 0; i < call.args.size(); ++i) {
        TypeDesc at = checkExpr(*call.args[i].value);
        if (!canAssignTypes(at, ft.args[i]))
          throw FarError("higher-order call argument type mismatch");
        call.bound_exprs.push_back(call.args[i].value.get());
      }
      return ft.args.back();
    }
    if (const IoFn* iofn = resolveIoCall(call.name, static_cast<int>(call.args.size()))) {
      if (call.name != "input")
        rejectDisallowedGlobalCall(call.name, current_fn_);
      checkIoArgs(iofn, call.name, call.args, [&](Expr& e) { return checkExpr(e); });
      return ioRetType(iofn);
    }
    if (const PerfFn* perffn = lookupPerf(call.name)) {
      rejectDisallowedGlobalCall(call.name, current_fn_);
      checkPerfArgs(perffn, call.args, [&](Expr& e) { return checkExpr(e); });
      return perfRetType(perffn);
    }
    if (const SecFn* secfn = lookupSec(call.name)) {
      rejectDisallowedGlobalCall(call.name, current_fn_);
      checkSecArgs(secfn, call.args, [&](Expr& e) { return checkExpr(e); });
      return secRetType(secfn);
    }
    if (const ModernFn* modfn = lookupModern(call.name)) {
      rejectDisallowedGlobalCall(call.name, current_fn_);
      checkModernArgs(modfn, call.args, [&](Expr& e) { return checkExpr(e); });
      return modernRetType(modfn);
    }
    if (const NetFn* netfn = lookupNet(call.name)) {
      rejectDisallowedGlobalCall(call.name, current_fn_);
      checkNetArgs(netfn, call.args, [&](Expr& e) { return checkExpr(e); });
      return netRetType(netfn);
    }
    if (const ScienceFn* scifn = lookupScience(call.name)) {
      rejectDisallowedGlobalCall(call.name, current_fn_);
      checkScienceArgs(scifn, call.args, [&](Expr& e) { return checkExpr(e); });
      return scienceRetType(scifn);
    }
    if (const StdlibFn* stdfn = lookupStdlib(call.name)) {
      rejectDisallowedGlobalCall(call.name, current_fn_);
      checkStdlibArgs(stdfn, call.args, [&](Expr& e) {
        TypeDesc td = checkExpr(e);
        if (isPrim(td, FarTypeId::String))
          return FarTypeId::String;
        if (isPrimitiveDesc(td))
          return td.primitive;
        return FarTypeId::I64;
      });
      return stdlibRetType(stdfn);
    }
    throwAt(expr, "undefined function '" + call.name + "'");
  }

  TypeDesc checkCollConstructor(const Call& call, const CollConstructorInfo* cc, const TypeDesc& annotated) {
    (void)cc;
    if (call.name == "Range") {
      if (call.args.size() < 2 || call.args.size() > 3)
        throw FarError("Range() expects 2 or 3 arguments");
      for (const auto& a : call.args)
        checkExpr(*a.value);
      return TypeDesc::range();
    }
    std::vector<TypeDesc> arg_types;
    for (const auto& a : call.args)
      arg_types.push_back(checkExpr(*a.value));
    auto check_homogeneous = [&](const TypeDesc& elem) {
      for (size_t i = 0; i < call.args.size(); ++i) {
        rejectNarrowingCallArg(*call.args[i].value, arg_types[i], elem);
        if (!canAssignTypes(arg_types[i], elem) && !typeDescEquals(arg_types[i], elem))
          throw FarError(call.name + "() argument type mismatch");
      }
      return elem;
    };
    if (annotated.form == TypeForm::Dict && annotated.args.size() == 2) {
      if (call.args.size() % 2 != 0)
        throw FarError("Dict() expects key/value pairs");
      for (size_t i = 0; i + 1 < call.args.size(); i += 2) {
        rejectNarrowingCallArg(*call.args[i].value, arg_types[i], annotated.args[0]);
        rejectNarrowingCallArg(*call.args[i + 1].value, arg_types[i + 1], annotated.args[1]);
        if (!canAssignTypes(arg_types[i], annotated.args[0]))
          throw FarError("Dict() key type mismatch");
        if (!canAssignTypes(arg_types[i + 1], annotated.args[1]))
          throw FarError("Dict() value type mismatch");
      }
      return annotated;
    }
    if (annotated.form == TypeForm::List && annotated.args.size() == 1) {
      check_homogeneous(annotated.args[0]);
      return annotated;
    }
    if (annotated.form == TypeForm::Set && annotated.args.size() == 1) {
      check_homogeneous(annotated.args[0]);
      return annotated;
    }
    if (annotated.form == TypeForm::Queue && annotated.args.size() == 1) {
      check_homogeneous(annotated.args[0]);
      return annotated;
    }
    if (annotated.form == TypeForm::Stack && annotated.args.size() == 1) {
      check_homogeneous(annotated.args[0]);
      return annotated;
    }
    if (annotated.form == TypeForm::LinkedList && annotated.args.size() == 1) {
      check_homogeneous(annotated.args[0]);
      return annotated;
    }
    TypeDesc elem = arg_types.empty() ? defaultIntType() : arg_types[0];
    for (size_t i = 1; i < arg_types.size(); ++i) {
      if (typeDescEquals(elem, arg_types[i]))
        continue;
      if (isPrimitiveDesc(elem) && isPrimitiveDesc(arg_types[i]))
        elem = promoteNumeric(elem, arg_types[i]);
      else if (!canAssignTypes(arg_types[i], elem))
        throw FarError(call.name + "() argument type mismatch");
    }
    if (call.name == "List")
      return TypeDesc::list(elem);
    if (call.name == "Dict")
      return TypeDesc::dict(defaultIntType(), defaultIntType());
    if (call.name == "Set")
      return TypeDesc::set(elem);
    if (call.name == "Queue")
      return TypeDesc::queue(elem);
    if (call.name == "Stack")
      return TypeDesc::stack(elem);
    if (call.name == "LinkedList")
      return TypeDesc::linkedList(elem);
    return TypeDesc::prim(FarTypeId::I64);
  }
};

}  // namespace

void typecheckProgram(Program& program) {
  prepareProgram(program);
  TypeChecker(program).run();
  foldProgramExpressions(program);
}

}  // namespace far
