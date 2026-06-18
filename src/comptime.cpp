#include "comptime.h"

#include "error.h"
#include "generics.h"
#include "macros.h"
#include "type_desc.h"
#include "types.h"

#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>

#include <climits>
#include <cmath>

namespace far {

int64_t reflectNameHash(const std::string& name) {
  std::hash<std::string> h;
  return static_cast<int64_t>(h(name));
}

static int64_t reflectFieldsFromTag(int64_t tag) {
  if (tag < 0x8000)
    return 0;
  return (tag - 0x8000) % 256;
}

static int64_t reflectKindFromTag(int64_t tag) {
  if (tag < 0x8000)
    return 0;
  return (tag - 0x8000) / 256;
}

static TypeDesc typeDescFromUnary(const Expr& expr) {
  if (expr.kind != Expr::TypeUnaryExprK)
    return TypeDesc::prim(FarTypeId::I64);
  const TypeUnaryExpr& t = expr.type_unary;
  if (t.has_type)
    return t.type_arg;
  if (t.value)
    return t.value->type;
  return TypeDesc::prim(FarTypeId::I64);
}

static int64_t typeTagFromUnary(const ComptimeContext& ctx, const Expr& expr) {
  TypeDesc td = typeDescFromUnary(expr);
  if (isUserDesc(td) && ctx.obj_reg && ctx.program) {
    const UserTypeDef* ut = resolveUserType(td, *ctx.obj_reg, *ctx.program);
    return ut ? ut->type_tag : 0;
  }
  return typeTag(td);
}

static std::string typeNameFromTypeof(const ComptimeContext& ctx, const Expr& expr) {
  if (expr.kind != Expr::TypeUnaryExprK || expr.type_unary.op != "typeof")
    return {};
  if (expr.type_unary.has_type && isUserDesc(expr.type_unary.type_arg))
    return expr.type_unary.type_arg.user_name;
  if (expr.type_unary.value && expr.type_unary.value->kind == Expr::Variable)
    return expr.type_unary.value->var.name;
  return {};
}

static int64_t typeTagFromTypeof(const ComptimeContext& ctx, const Expr& expr) {
  return typeTagFromUnary(ctx, expr);
}

static ComptimeValue makeIntVal(int64_t v) {
  ComptimeValue cv;
  cv.kind = ComptimeValue::Kind::Int;
  cv.i64 = v;
  return cv;
}

static bool checkedAddI64(int64_t a, int64_t b, int64_t& out) {
#if defined(__GNUC__) || defined(__clang__)
  return !__builtin_add_overflow(a, b, &out);
#else
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
    return false;
  out = a + b;
  return true;
#endif
}

static bool checkedAddU64(uint64_t a, uint64_t b, uint64_t& out) {
  out = a + b;
  return out >= a;
}

static bool checkedSubU64(uint64_t a, uint64_t b, uint64_t& out) {
  if (b > a)
    return false;
  out = a - b;
  return true;
}

static bool checkedMulU64(uint64_t a, uint64_t b, uint64_t& out) {
#if defined(__GNUC__) || defined(__clang__)
  return !__builtin_mul_overflow(a, b, &out);
#else
  if (a != 0 && b > UINT64_MAX / a)
    return false;
  out = a * b;
  return true;
#endif
}

static bool checkedSubI64(int64_t a, int64_t b, int64_t& out) {
#if defined(__GNUC__) || defined(__clang__)
  return !__builtin_sub_overflow(a, b, &out);
#else
  if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
    return false;
  out = a - b;
  return true;
#endif
}

static bool checkedMulI64(int64_t a, int64_t b, int64_t& out) {
#if defined(__GNUC__) || defined(__clang__)
  return !__builtin_mul_overflow(a, b, &out);
#else
  if (a != 0 && b != 0) {
    if (a == -1 && b == INT64_MIN)
      return false;
    if (b == -1 && a == INT64_MIN)
      return false;
    if (a > INT64_MAX / b || a < INT64_MIN / b)
      return false;
  }
  out = a * b;
  return true;
#endif
}

static std::optional<ComptimeValue> evalComptimeStmt(ComptimeContext& ctx, const Stmt& stmt);

static constexpr int64_t kMaxComptimeLoopIters = 1'000'000;
static constexpr size_t kMaxComptimeStringLen = 64 * 1024 * 1024;

static void bumpComptimeLoopIter(ComptimeContext& ctx) {
  if (++ctx.loop_iters > kMaxComptimeLoopIters)
    throw FarError("comptime loop iterations exceeded");
}

static void pushComptimeDepth(ComptimeContext& ctx) {
  if (++ctx.depth > 32)
    throw FarError("comptime evaluation depth exceeded");
}

static void popComptimeDepth(ComptimeContext& ctx) {
  if (ctx.depth > 0)
    --ctx.depth;
}

struct ComptimeDepthGuard {
  ComptimeContext& ctx;
  explicit ComptimeDepthGuard(ComptimeContext& c) : ctx(c) { pushComptimeDepth(ctx); }
  ~ComptimeDepthGuard() { popComptimeDepth(ctx); }
};

static ComptimeValue makeFloatVal(double v) {
  ComptimeValue cv;
  cv.kind = ComptimeValue::Kind::Float;
  cv.f64 = v;
  return cv;
}

static bool comptimeNullish(const ComptimeValue& v) {
  if (v.kind == ComptimeValue::Kind::String)
    return v.str.empty();
  if (v.kind == ComptimeValue::Kind::Float)
    return std::isnan(v.f64) || v.f64 == 0.0;
  if (v.kind == ComptimeValue::Kind::Bool)
    return !v.b;
  return v.i64 == 0;
}

static bool comptimeTruthy(const ComptimeValue& v) {
  switch (v.kind) {
    case ComptimeValue::Kind::String:
      return !v.str.empty();
    case ComptimeValue::Kind::Float:
      return !std::isnan(v.f64) && v.f64 != 0.0;
    case ComptimeValue::Kind::Bool:
      return v.b;
    default:
      return v.i64 != 0;
  }
}

static int64_t comptimeEq(const ComptimeValue& l, const ComptimeValue& r) {
  if (l.kind != r.kind)
    throw FarError("comptime comparison between incompatible types");
  if (l.kind == ComptimeValue::Kind::String)
    return l.str == r.str ? 1 : 0;
  if (l.kind == ComptimeValue::Kind::Float)
    return l.f64 == r.f64 ? 1 : 0;
  if (l.kind == ComptimeValue::Kind::Bool)
    return l.b == r.b ? 1 : 0;
  return l.i64 == r.i64 ? 1 : 0;
}

static void requireComptimeInt(const ComptimeValue& v, const char* ctx) {
  if (v.kind != ComptimeValue::Kind::Int)
    throw FarError(std::string(ctx) + " requires integer operand");
}

static ComptimeValue makeStringVal(const std::string& s) {
  ComptimeValue cv;
  cv.kind = ComptimeValue::Kind::String;
  cv.str = s;
  return cv;
}

static ComptimeValue makeDictVal(std::vector<ComptimeValue> keys, std::vector<ComptimeValue> values) {
  ComptimeValue cv;
  cv.kind = ComptimeValue::Kind::Dict;
  cv.dict_keys = std::move(keys);
  cv.dict_values = std::move(values);
  return cv;
}

static bool comptimeDictContainsKey(const ComptimeValue& dict, const ComptimeValue& key) {
  if (dict.kind != ComptimeValue::Kind::Dict)
    throw FarError("comptime 'in' requires dict container");
  for (const auto& entry_key : dict.dict_keys) {
    if (comptimeEq(key, entry_key))
      return true;
  }
  return false;
}

static bool comptimeArrayContains(const ComptimeValue& arr, const ComptimeValue& key) {
  if (arr.kind != ComptimeValue::Kind::Array)
    throw FarError("comptime 'in' requires array container");
  for (const auto& el : arr.array_elems) {
    if (comptimeEq(key, el))
      return true;
  }
  return false;
}

static ComptimeValue makeArrayVal(std::vector<ComptimeValue> elems) {
  ComptimeValue cv;
  cv.kind = ComptimeValue::Kind::Array;
  cv.array_elems = std::move(elems);
  return cv;
}

static std::optional<std::vector<ComptimeValue>> resolveComptimeArrayElems(ComptimeContext& ctx,
                                                                           const Expr& iter) {
  if (iter.kind == Expr::ArrayLitExpr) {
    std::vector<ComptimeValue> elems;
    elems.reserve(iter.array_lit.elements.size());
    for (const auto& el : iter.array_lit.elements)
      elems.push_back(evalExpr(ctx, *el));
    return elems;
  }
  if (iter.kind == Expr::Variable) {
    auto it = ctx.vars.find(iter.var.name);
    if (it != ctx.vars.end() && it->second.kind == ComptimeValue::Kind::Array)
      return it->second.array_elems;
    if (ctx.program) {
      for (const auto& stmt : ctx.program->comptime_stmts) {
        if (stmt->kind != Stmt::LetStmt || !stmt->let.is_constexpr || stmt->let.name != iter.var.name ||
            !stmt->let.value)
          continue;
        if (stmt->let.value->kind == Expr::ArrayLitExpr)
          return resolveComptimeArrayElems(ctx, *stmt->let.value);
      }
    }
  }
  ComptimeValue cv;
  if (tryEvalExpr(ctx, iter, cv) && cv.kind == ComptimeValue::Kind::Array)
    return cv.array_elems;
  return std::nullopt;
}

static ComptimeValue evalFunctionBody(ComptimeContext& ctx, const Function& fn,
                                      const std::vector<ComptimeValue>& args) {
  ComptimeDepthGuard depth(ctx);
  ComptimeContext local = ctx;
  if (args.size() != fn.params.size())
    throw FarError("comptime call to '" + fn.name + "' expects " + std::to_string(fn.params.size()) +
                   " argument(s), got " + std::to_string(args.size()));
  for (size_t i = 0; i < fn.params.size(); ++i)
    local.vars[fn.params[i].name] = args[i];
  for (const auto& stmt : fn.body) {
    if (auto result = evalComptimeStmt(local, *stmt))
      return *result;
  }
  throw FarError("comptime function fell through without return");
}

static std::unique_ptr<Expr> cloneComptimeRhs(const Expr& src) {
  switch (src.kind) {
    case Expr::Int: {
      auto e = Expr::makeInt(src.int_lit.value);
      e->type = src.type;
      return e;
    }
    case Expr::Float: {
      auto e = Expr::makeFloat(src.float_lit.value, src.float_lit.is_float);
      e->type = src.type;
      return e;
    }
    case Expr::Variable: {
      auto e = Expr::makeVar(src.var.name);
      e->type = src.type;
      return e;
    }
    case Expr::Binary: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Binary;
      e->type = src.type;
      e->bin_op.op = src.bin_op.op;
      e->bin_op.left = cloneComptimeRhs(*src.bin_op.left);
      e->bin_op.right = cloneComptimeRhs(*src.bin_op.right);
      return e;
    }
    case Expr::PrefixExprK: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::PrefixExprK;
      e->type = src.type;
      e->prefix.op = src.prefix.op;
      e->prefix.operand = cloneComptimeRhs(*src.prefix.operand);
      return e;
    }
    default:
      throw FarError("comptime assignment rhs is too complex");
  }
}

static void evalComptimeAssign(ComptimeContext& ctx, const AssignExpr& assign) {
  if (assign.target->kind != Expr::Variable)
    throw FarError("comptime assignment requires a simple variable target");
  const std::string& name = assign.target->var.name;
  if (!ctx.vars.count(name))
    throw FarError("unknown comptime variable '" + name + "'");
  if (assign.op == "=") {
    ctx.vars[name] = evalExpr(ctx, *assign.value);
    return;
  }
  if (assign.op.size() == 3 && assign.op[0] == '?' && assign.op[1] == '?' && assign.op[2] == '=') {
    if (comptimeNullish(ctx.vars[name]))
      ctx.vars[name] = evalExpr(ctx, *assign.value);
    return;
  }
  static const std::unordered_map<std::string, std::string> compound = {
      {"+=", "+"},  {"-=", "-"},  {"*=", "*"},    {"/=", "/"},     {"%=", "%"},
      {"**=", "**"}, {"//=", "//"}, {"&=", "&"}, {"|=", "|"},     {"^=", "^"},
      {"<<=", "<<"}, {">>=", ">>"},
  };
  auto mapped = compound.find(assign.op);
  if (mapped == compound.end())
    throw FarError("unsupported comptime assignment operator '" + assign.op + "'");
  auto lhs = Expr::makeVar(name);
  lhs->type = assign.target->type;
  auto bin = Expr::makeBinOp(mapped->second, std::move(lhs), cloneComptimeRhs(*assign.value));
  bin->type = assign.target->type;
  ctx.vars[name] = evalExpr(ctx, *bin);
}

static std::optional<ComptimeValue> evalComptimeStmt(ComptimeContext& ctx, const Stmt& stmt);

static bool foreachConstEmpty(const Expr& iter, const Program* program = nullptr);

static bool comptimeContainsThrowStmt(const Stmt& stmt) {
  switch (stmt.kind) {
    case Stmt::ThrowStmtK:
      return true;
    case Stmt::IfStmt:
      for (const auto& c : stmt.if_stmt.clauses) {
        for (const auto& s : c.body)
          if (comptimeContainsThrowStmt(*s))
            return true;
      }
      for (const auto& s : stmt.if_stmt.else_body)
        if (comptimeContainsThrowStmt(*s))
          return true;
      return false;
    case Stmt::WhileStmt:
      for (const auto& s : stmt.while_stmt.body)
        if (comptimeContainsThrowStmt(*s))
          return true;
      return false;
    case Stmt::ForStmt:
      for (const auto& s : stmt.for_stmt.body)
        if (comptimeContainsThrowStmt(*s))
          return true;
      return false;
    case Stmt::TryStmtK:
      for (const auto& s : stmt.try_stmt.try_body)
        if (comptimeContainsThrowStmt(*s))
          return true;
      for (const auto& s : stmt.try_stmt.catch_body)
        if (comptimeContainsThrowStmt(*s))
          return true;
      for (const auto& s : stmt.try_stmt.finally_body)
        if (comptimeContainsThrowStmt(*s))
          return true;
      return false;
    case Stmt::MatchStmtK:
      for (const auto& arm : stmt.match_stmt.arms)
        for (const auto& s : arm.body)
          if (comptimeContainsThrowStmt(*s))
            return true;
      return false;
    case Stmt::UnsafeStmtK:
      for (const auto& s : stmt.unsafe.body)
        if (comptimeContainsThrowStmt(*s))
          return true;
      return false;
    default:
      return false;
  }
}

static bool comptimeTryBodyMayThrow(const std::vector<std::unique_ptr<Stmt>>& try_body) {
  for (const auto& s : try_body) {
    if (comptimeContainsThrowStmt(*s))
      return true;
  }
  return false;
}

static bool comptimePatternMatches(const Pattern& pat, const ComptimeValue& scrut) {
  switch (pat.kind) {
    case PatKind::Wildcard:
    case PatKind::Bind:
      return true;
    case PatKind::Literal:
      if (pat.literal_is_float) {
        if (std::isnan(pat.float_literal))
          return scrut.kind == ComptimeValue::Kind::Float && std::isnan(scrut.f64);
        return scrut.kind == ComptimeValue::Kind::Float && scrut.f64 == pat.float_literal;
      }
      if (scrut.kind == ComptimeValue::Kind::Int)
        return scrut.i64 == pat.literal;
      if (scrut.kind == ComptimeValue::Kind::Float)
        return static_cast<double>(pat.literal) == scrut.f64;
      return false;
    default:
      return false;
  }
}

static void evalComptimeForeachBody(ComptimeContext& ctx,
                                    const std::vector<std::unique_ptr<Stmt>>& body,
                                    std::optional<ComptimeValue>& early_out) {
  for (const auto& s : body) {
    if (auto result = evalComptimeStmt(ctx, *s)) {
      early_out = result;
      return;
    }
  }
}

static std::optional<ComptimeValue> evalComptimeStmt(ComptimeContext& ctx, const Stmt& stmt) {
  ComptimeDepthGuard depth(ctx);
  switch (stmt.kind) {
    case Stmt::ReturnStmt:
      if (!stmt.ret.has_value)
        throw FarError("comptime function must return a value");
      return evalExpr(ctx, *stmt.ret.value);
    case Stmt::LetStmt:
      ctx.vars[stmt.let.name] = evalExpr(ctx, *stmt.let.value);
      return std::nullopt;
    case Stmt::IfStmt: {
      for (const auto& c : stmt.if_stmt.clauses) {
        ComptimeValue cond = evalExpr(ctx, *c.condition);
        if (comptimeTruthy(cond)) {
          for (const auto& s : c.body) {
            if (auto result = evalComptimeStmt(ctx, *s))
              return result;
          }
          return std::nullopt;
        }
      }
      for (const auto& s : stmt.if_stmt.else_body) {
        if (auto result = evalComptimeStmt(ctx, *s))
          return result;
      }
      return std::nullopt;
    }
    case Stmt::WhileStmt: {
      while (true) {
        bumpComptimeLoopIter(ctx);
        ComptimeValue cond = evalExpr(ctx, *stmt.while_stmt.condition);
        if (!comptimeTruthy(cond))
          break;
        for (const auto& s : stmt.while_stmt.body) {
          if (auto result = evalComptimeStmt(ctx, *s))
            return result;
        }
      }
      return std::nullopt;
    }
    case Stmt::ForStmt: {
      if (stmt.for_stmt.is_parallel)
        throw FarError("parallel for unsupported in comptime function");
      if (stmt.for_stmt.is_range) {
        ComptimeValue sv = evalExpr(ctx, *stmt.for_stmt.range_start);
        ComptimeValue ev = evalExpr(ctx, *stmt.for_stmt.range_end);
        requireComptimeInt(sv, "comptime range");
        requireComptimeInt(ev, "comptime range");
        bool exclusive = stmt.for_stmt.range_exclusive;
        int64_t i = sv.i64;
        for (;;) {
          bumpComptimeLoopIter(ctx);
          if (exclusive) {
            if (i >= ev.i64)
              break;
          } else if (i > ev.i64) {
            break;
          }
          ctx.vars[stmt.for_stmt.range_var] = makeIntVal(i);
          for (const auto& s : stmt.for_stmt.body) {
            if (auto result = evalComptimeStmt(ctx, *s))
              return result;
          }
          if (i == INT64_MAX)
            throw FarError("comptime range loop overflow");
          ++i;
        }
        return std::nullopt;
      }
      if (stmt.for_stmt.is_foreach) {
        if (foreachConstEmpty(*stmt.for_stmt.foreach_iter, ctx.program))
          return std::nullopt;
        if (stmt.for_stmt.foreach_iter->kind == Expr::ArrayLitExpr) {
          std::optional<ComptimeValue> early;
          for (const auto& el : stmt.for_stmt.foreach_iter->array_lit.elements) {
            ctx.vars[stmt.for_stmt.foreach_var] = evalExpr(ctx, *el);
            evalComptimeForeachBody(ctx, stmt.for_stmt.body, early);
            if (early)
              return early;
          }
          return std::nullopt;
        }
        if (auto elems = resolveComptimeArrayElems(ctx, *stmt.for_stmt.foreach_iter)) {
          std::optional<ComptimeValue> early;
          for (const auto& el : *elems) {
            ctx.vars[stmt.for_stmt.foreach_var] = el;
            evalComptimeForeachBody(ctx, stmt.for_stmt.body, early);
            if (early)
              return early;
          }
          return std::nullopt;
        }
        ComptimeValue iter_v;
        if (tryEvalExpr(ctx, *stmt.for_stmt.foreach_iter, iter_v) &&
            iter_v.kind == ComptimeValue::Kind::Array) {
          std::optional<ComptimeValue> early;
          for (const auto& el : iter_v.array_elems) {
            ctx.vars[stmt.for_stmt.foreach_var] = el;
            evalComptimeForeachBody(ctx, stmt.for_stmt.body, early);
            if (early)
              return early;
          }
          return std::nullopt;
        }
        throw FarError("comptime for-in requires a constexpr array");
      }
      if (stmt.for_stmt.init)
        if (auto result = evalComptimeStmt(ctx, *stmt.for_stmt.init))
          return result;
      while (true) {
        bumpComptimeLoopIter(ctx);
        if (!stmt.for_stmt.cond)
          break;
        ComptimeValue cond = evalExpr(ctx, *stmt.for_stmt.cond);
        if (!comptimeTruthy(cond))
          break;
        for (const auto& s : stmt.for_stmt.body) {
          if (auto result = evalComptimeStmt(ctx, *s))
            return result;
        }
        if (stmt.for_stmt.step)
          if (auto result = evalComptimeStmt(ctx, *stmt.for_stmt.step))
            return result;
      }
      return std::nullopt;
    }
    case Stmt::MatchStmtK: {
      ComptimeValue scrut = evalExpr(ctx, *stmt.match_stmt.scrutinee);
      for (const auto& arm : stmt.match_stmt.arms) {
        if (!arm.pat || !comptimePatternMatches(*arm.pat, scrut))
          continue;
        if (arm.pat->kind == PatKind::Bind)
          ctx.vars[arm.pat->bind_name] = scrut;
        for (const auto& s : arm.body) {
          if (auto result = evalComptimeStmt(ctx, *s))
            return result;
        }
        return std::nullopt;
      }
      throw FarError("non-exhaustive comptime match");
    }
    case Stmt::ExprStmtK:
      if (stmt.expr_stmt.expr) {
        if (stmt.expr_stmt.expr->kind == Expr::AssignExprK) {
          evalComptimeAssign(ctx, stmt.expr_stmt.expr->assign);
          return std::nullopt;
        }
        if (stmt.expr_stmt.expr->kind == Expr::PrefixExprK ||
            stmt.expr_stmt.expr->kind == Expr::PostfixExprK) {
          evalExpr(ctx, *stmt.expr_stmt.expr);
          return std::nullopt;
        }
      }
      throw FarError("unsupported statement in comptime function");
    default:
      throw FarError("unsupported statement in comptime function");
  }
}

static ComptimeValue evalComptimeBlockExpr(ComptimeContext& ctx,
                                           const std::vector<std::unique_ptr<Stmt>>& block) {
  if (block.empty())
    throw FarError("empty comptime block");
  ComptimeContext local = ctx;
  for (size_t i = 0; i + 1 < block.size(); ++i) {
    if (auto result = evalComptimeStmt(local, *block[i]))
      return *result;
  }
  const Stmt& last = *block.back();
  if (last.kind == Stmt::ReturnStmt) {
    if (!last.ret.has_value)
      throw FarError("comptime block must return a value");
    return evalExpr(local, *last.ret.value);
  }
  if (last.kind == Stmt::ExprStmtK && last.expr_stmt.expr) {
    if (last.expr_stmt.expr->kind == Expr::AssignExprK)
      throw FarError("comptime block must end with a value expression, not assignment");
    return evalExpr(local, *last.expr_stmt.expr);
  }
  throw FarError("comptime block must end with an expression or return");
}

bool tryEvalExpr(ComptimeContext& ctx, const Expr& expr, ComptimeValue& out) {
  try {
    out = evalExpr(ctx, expr);
    return true;
  } catch (const FarError& e) {
    if (e.message.find("division by zero") != std::string::npos ||
        e.message.find("modulo by zero") != std::string::npos)
      throw;
    return false;
  }
}

static bool comptimeUsesUnsigned(const Expr& left, const Expr& right) {
  auto isUnsigned = [](const TypeDesc& td) {
    return isPrimitiveDesc(td) && isIntegerType(td.primitive) && !typeInfo(td.primitive).is_signed;
  };
  return isUnsigned(left.type) || isUnsigned(right.type);
}

static bool comptimeIsUnsignedIntegerType(const TypeDesc& td) {
  return isPrimitiveDesc(td) && isIntegerType(td.primitive) && !typeInfo(td.primitive).is_signed;
}

static bool comptimeIsSignedIntegerType(const TypeDesc& td) {
  return isPrimitiveDesc(td) && isIntegerType(td.primitive) && typeInfo(td.primitive).is_signed;
}

static std::string comptimeFlipOrderedCmpOp(const std::string& op) {
  if (op == "<")
    return ">";
  if (op == ">")
    return "<";
  if (op == "<=")
    return ">=";
  if (op == ">=")
    return "<=";
  return op;
}

static int64_t comptimeMixedSignCompareResult(const TypeDesc& left_ty, const TypeDesc& right_ty,
                                              int64_t left_bits, int64_t right_bits,
                                              const std::string& op) {
  const bool left_u = comptimeIsUnsignedIntegerType(left_ty);
  const bool right_u = comptimeIsUnsignedIntegerType(right_ty);
  if (!((left_u && comptimeIsSignedIntegerType(right_ty)) ||
        (comptimeIsSignedIntegerType(left_ty) && right_u)))
    throw FarError("internal mixed-sign compare mismatch");
  const uint64_t U = left_u ? static_cast<uint64_t>(left_bits) : static_cast<uint64_t>(right_bits);
  const int64_t S = left_u ? right_bits : left_bits;
  const std::string effective_op = left_u ? op : comptimeFlipOrderedCmpOp(op);
  if (effective_op == "==")
    return (S >= 0 && U == static_cast<uint64_t>(S)) ? 1 : 0;
  if (effective_op == "!=")
    return (S < 0 || U != static_cast<uint64_t>(S)) ? 1 : 0;
  if (S < 0) {
    if (effective_op == "<" || effective_op == "<=")
      return 0;
    if (effective_op == ">=")
      return 1;
    if (effective_op == ">") {
      if (U <= static_cast<uint64_t>(INT64_MAX))
        return 1;
      return U > static_cast<uint64_t>(S) ? 1 : 0;
    }
  }
  if (effective_op == "<")
    return U < static_cast<uint64_t>(S) ? 1 : 0;
  if (effective_op == ">")
    return U > static_cast<uint64_t>(S) ? 1 : 0;
  if (effective_op == "<=")
    return U <= static_cast<uint64_t>(S) ? 1 : 0;
  if (effective_op == ">=")
    return U >= static_cast<uint64_t>(S) ? 1 : 0;
  throw FarError("unknown mixed-sign compare op");
}

static bool comptimeIsMixedSignIntegerCompare(const TypeDesc& left_ty, const TypeDesc& right_ty) {
  return (comptimeIsUnsignedIntegerType(left_ty) && comptimeIsSignedIntegerType(right_ty)) ||
         (comptimeIsSignedIntegerType(left_ty) && comptimeIsUnsignedIntegerType(right_ty));
}

static int64_t comptimeFloorDivI64(int64_t a, int64_t b) {
  if (b == 0)
    throw FarError("comptime division by zero");
  if (a == INT64_MIN && b == -1)
    throw FarError("comptime integer overflow");
  int64_t q = a / b;
  int64_t rem = a % b;
  if (rem != 0 && ((a < 0) != (b < 0)))
    q -= 1;
  return q;
}

static int64_t comptimeIpow(int64_t base, int64_t exp) {
  if (exp < 0)
    return 0;
  int64_t result = 1;
  while (exp > 0) {
    if (exp & 1) {
      int64_t out = 0;
      if (!checkedMulI64(result, base, out))
        throw FarError("comptime integer overflow");
      result = out;
    }
    if (exp > 1) {
      int64_t out = 0;
      if (!checkedMulI64(base, base, out))
        throw FarError("comptime integer overflow");
      base = out;
    }
    exp >>= 1;
  }
  return result;
}

static double comptimeIntToF64(const ComptimeValue& v, const TypeDesc& src_ty) {
  if (v.kind != ComptimeValue::Kind::Int)
    throw FarError("comptime float conversion requires integer operand");
  if (isPrimitiveDesc(src_ty) && isIntegerType(src_ty.primitive) && !typeInfo(src_ty.primitive).is_signed)
    return static_cast<double>(static_cast<uint64_t>(v.i64));
  return static_cast<double>(v.i64);
}

static int64_t comptimeNarrowInt(int64_t v, FarTypeId target) {
  const FarTypeInfo& info = typeInfo(target);
  if (!info.is_integer || info.bits <= 0 || info.bits >= 64)
    return v;
  if (!info.is_signed) {
    uint64_t mask = (1ULL << info.bits) - 1;
    return static_cast<int64_t>(static_cast<uint64_t>(v) & mask);
  }
  int64_t shift = 64 - info.bits;
  return (v << shift) >> shift;
}

static bool comptimeExprExplicitlyTyped(const Expr& e) {
  return e.kind == Expr::CastExpr || e.kind == Expr::AsExprK;
}

static bool comptimeShouldNarrowBinary(const TypeDesc& left, const TypeDesc& right, const Expr& left_expr,
                                     const Expr& right_expr) {
  auto bitsOf = [](const TypeDesc& td) -> int {
    if (!isPrimitiveDesc(td) || !isIntegerType(td.primitive))
      return 64;
    return typeInfo(td.primitive).bits;
  };
  const int ml = bitsOf(left);
  const int mr = bitsOf(right);
  if (ml < 32 || mr < 32)
    return true;
  if (ml == 32 && mr == 32) {
    if (comptimeUsesUnsigned(left_expr, right_expr))
      return true;
    if (comptimeExprExplicitlyTyped(left_expr) || comptimeExprExplicitlyTyped(right_expr))
      return true;
  }
  return false;
}

static std::optional<FarTypeId> comptimeBinaryIntType(const TypeDesc& left, const TypeDesc& right) {
  const FarTypeInfo* li = nullptr;
  const FarTypeInfo* ri = nullptr;
  if (isPrimitiveDesc(left) && isIntegerType(left.primitive))
    li = &typeInfo(left.primitive);
  if (isPrimitiveDesc(right) && isIntegerType(right.primitive))
    ri = &typeInfo(right.primitive);
  if (!li && !ri)
    return std::nullopt;
  if (!li)
    return right.primitive;
  if (!ri)
    return left.primitive;
  if (li->bits > ri->bits)
    return left.primitive;
  if (ri->bits > li->bits)
    return right.primitive;
  if (li->is_signed != ri->is_signed)
    return !li->is_signed ? left.primitive : right.primitive;
  return left.primitive;
}

static ComptimeValue comptimeIntBinaryResult(int64_t v, const Expr& left_expr, const Expr& right_expr) {
  if (!comptimeShouldNarrowBinary(left_expr.type, right_expr.type, left_expr, right_expr))
    return makeIntVal(v);
  if (auto ty = comptimeBinaryIntType(left_expr.type, right_expr.type))
    return makeIntVal(comptimeNarrowInt(v, *ty));
  return makeIntVal(v);
}

static ComptimeValue comptimeCastValue(const ComptimeValue& v, const TypeDesc& src_ty, const TypeDesc& target_ty) {
  if (isPrimitiveDesc(target_ty) && target_ty.primitive == FarTypeId::Bool)
    return makeIntVal(comptimeTruthy(v) ? 1 : 0);
  if (isPrimitiveDesc(target_ty) && target_ty.primitive == FarTypeId::String) {
    if (v.kind == ComptimeValue::Kind::String)
      return v;
    if (v.kind == ComptimeValue::Kind::Int)
      return makeStringVal(std::to_string(v.i64));
    throw FarError("comptime cast to string requires string or integer operand");
  }
  if (isPrimitiveDesc(target_ty) && isIntegerType(target_ty.primitive)) {
    int64_t out = 0;
    if (v.kind == ComptimeValue::Kind::Int)
      out = v.i64;
    else if (v.kind == ComptimeValue::Kind::Float) {
      if (!typeInfo(target_ty.primitive).is_signed) {
        if (v.f64 <= 0.0)
          out = 0;
        else if (v.f64 >= static_cast<double>(UINT64_MAX))
          out = static_cast<int64_t>(UINT64_MAX);
        else
          out = static_cast<int64_t>(static_cast<uint64_t>(v.f64));
      } else {
        const FarTypeInfo& info = typeInfo(target_ty.primitive);
        if (!std::isfinite(v.f64))
          throw FarError("comptime float-to-integer cast requires finite value");
        if (v.f64 < static_cast<double>(info.min_i) || v.f64 > static_cast<double>(info.max_i))
          throw FarError("comptime float-to-integer cast out of range");
        out = static_cast<int64_t>(v.f64);
      }
    } else {
      throw FarError("comptime cast to integer requires numeric operand");
    }
    return makeIntVal(comptimeNarrowInt(out, target_ty.primitive));
  }
  if (isFloatType(target_ty.primitive)) {
    if (v.kind == ComptimeValue::Kind::Float)
      return makeFloatVal(v.f64);
    if (v.kind == ComptimeValue::Kind::Int)
      return makeFloatVal(comptimeIntToF64(v, src_ty));
    throw FarError("comptime cast to float requires numeric operand");
  }
  if (v.kind == ComptimeValue::Kind::String || v.kind == ComptimeValue::Kind::Float)
    throw FarError("unsupported comptime cast to " + typeDescName(target_ty));
  return v;
}

ComptimeValue evalExpr(ComptimeContext& ctx, const Expr& expr) {
  ComptimeDepthGuard depth(ctx);
  switch (expr.kind) {
    case Expr::Int:
      return makeIntVal(expr.int_lit.value);
    case Expr::Float:
      return makeFloatVal(expr.float_lit.value);
    case Expr::Char:
      return makeIntVal(static_cast<int64_t>(expr.char_lit.value));
    case Expr::String:
      return makeStringVal(expr.string_lit.value);
    case Expr::DictLitExpr: {
      std::vector<ComptimeValue> keys;
      std::vector<ComptimeValue> values;
      keys.reserve(expr.dict_lit.entries.size());
      values.reserve(expr.dict_lit.entries.size());
      for (const auto& entry : expr.dict_lit.entries) {
        keys.push_back(evalExpr(ctx, *entry.key));
        values.push_back(evalExpr(ctx, *entry.value));
      }
      return makeDictVal(std::move(keys), std::move(values));
    }
    case Expr::ArrayLitExpr: {
      std::vector<ComptimeValue> elems;
      elems.reserve(expr.array_lit.elements.size());
      for (const auto& el : expr.array_lit.elements)
        elems.push_back(evalExpr(ctx, *el));
      return makeArrayVal(std::move(elems));
    }
    case Expr::Variable: {
      auto it = ctx.vars.find(expr.var.name);
      if (it == ctx.vars.end())
        throw FarError("unknown comptime variable '" + expr.var.name + "'");
      return it->second;
    }
    case Expr::AsExprK: {
      ComptimeValue v = evalExpr(ctx, *expr.as_expr.value);
      return comptimeCastValue(v, expr.as_expr.value->type, expr.as_expr.type);
    }
    case Expr::CastExpr: {
      ComptimeValue v = evalExpr(ctx, *expr.cast.value);
      return comptimeCastValue(v, expr.cast.value->type, expr.cast.target);
    }
    case Expr::Binary: {
      const std::string& op = expr.bin_op.op;
      if (op == "!" && expr.bin_op.left->kind == Expr::Int &&
          expr.bin_op.left->int_lit.value == 0) {
        ComptimeValue v = evalExpr(ctx, *expr.bin_op.right);
        return makeIntVal(comptimeTruthy(v) ? 0 : 1);
      }
      if (op == "and" || op == "&&") {
        ComptimeValue l = evalExpr(ctx, *expr.bin_op.left);
        if (!comptimeTruthy(l))
          return makeIntVal(0);
        ComptimeValue r = evalExpr(ctx, *expr.bin_op.right);
        return makeIntVal(comptimeTruthy(r) ? 1 : 0);
      }
      if (op == "or" || op == "||") {
        ComptimeValue l = evalExpr(ctx, *expr.bin_op.left);
        if (comptimeTruthy(l))
          return makeIntVal(1);
        ComptimeValue r = evalExpr(ctx, *expr.bin_op.right);
        return makeIntVal(comptimeTruthy(r) ? 1 : 0);
      }
      if (op == "??") {
        ComptimeValue l = evalExpr(ctx, *expr.bin_op.left);
        if (comptimeNullish(l))
          return evalExpr(ctx, *expr.bin_op.right);
        return l;
      }
      if (op == "in" || op == "not in") {
        ComptimeValue l = evalExpr(ctx, *expr.bin_op.left);
        if (expr.bin_op.right->kind == Expr::ArrayLitExpr) {
          bool found = false;
          for (const auto& el : expr.bin_op.right->array_lit.elements) {
            if (comptimeEq(l, evalExpr(ctx, *el)))
              found = true;
          }
          if (op == "not in")
            found = !found;
          return makeIntVal(found ? 1 : 0);
        }
        if (expr.bin_op.right->kind == Expr::DictLitExpr) {
          bool found = false;
          for (const auto& entry : expr.bin_op.right->dict_lit.entries) {
            if (comptimeEq(l, evalExpr(ctx, *entry.key)))
              found = true;
          }
          if (op == "not in")
            found = !found;
          return makeIntVal(found ? 1 : 0);
        }
        if (expr.bin_op.right->kind == Expr::Variable) {
          ComptimeValue container = evalExpr(ctx, *expr.bin_op.right);
          bool found = false;
          if (container.kind == ComptimeValue::Kind::Dict)
            found = comptimeDictContainsKey(container, l);
          else if (container.kind == ComptimeValue::Kind::Array)
            found = comptimeArrayContains(container, l);
          else
            throw FarError("comptime 'in' requires array or dict container");
          if (op == "not in")
            found = !found;
          return makeIntVal(found ? 1 : 0);
        }
      }
      ComptimeValue l = evalExpr(ctx, *expr.bin_op.left);
      ComptimeValue r = evalExpr(ctx, *expr.bin_op.right);
      const Expr& left_expr = *expr.bin_op.left;
      const Expr& right_expr = *expr.bin_op.right;
      if (comptimeIsMixedSignIntegerCompare(left_expr.type, right_expr.type) &&
          (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=")) {
        requireComptimeInt(l, "comptime compare");
        requireComptimeInt(r, "comptime compare");
        return makeIntVal(
            comptimeMixedSignCompareResult(left_expr.type, right_expr.type, l.i64, r.i64, op));
      }
      if (op == "==")
        return makeIntVal(comptimeEq(l, r));
      if (op == "!=")
        return makeIntVal(comptimeEq(l, r) ? 0 : 1);
      if (op == "+") {
        if (l.kind == ComptimeValue::Kind::String && r.kind == ComptimeValue::Kind::String) {
          if (l.str.size() > kMaxComptimeStringLen - r.str.size())
            throw FarError("comptime string concatenation exceeds maximum length");
          return makeStringVal(l.str + r.str);
        }
        if (l.kind == ComptimeValue::Kind::Float || r.kind == ComptimeValue::Kind::Float) {
          auto asF64 = [](const ComptimeValue& v, const TypeDesc& ty) -> double {
            if (v.kind == ComptimeValue::Kind::Float)
              return v.f64;
            if (v.kind == ComptimeValue::Kind::Int)
              return comptimeIntToF64(v, ty);
            throw FarError("comptime float op requires numeric operand");
          };
          return makeFloatVal(asF64(l, expr.bin_op.left->type) + asF64(r, expr.bin_op.right->type));
        }
        requireComptimeInt(l, "comptime '+'");
        requireComptimeInt(r, "comptime '+'");
        if (comptimeUsesUnsigned(*expr.bin_op.left, *expr.bin_op.right)) {
          if (!comptimeShouldNarrowBinary(expr.bin_op.left->type, expr.bin_op.right->type,
                                          *expr.bin_op.left, *expr.bin_op.right)) {
            uint64_t out = 0;
            if (!checkedAddU64(static_cast<uint64_t>(l.i64), static_cast<uint64_t>(r.i64), out))
              throw FarError("comptime integer overflow");
            return comptimeIntBinaryResult(static_cast<int64_t>(out), *expr.bin_op.left,
                                           *expr.bin_op.right);
          }
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 + (uint64_t)r.i64), *expr.bin_op.left,
                                         *expr.bin_op.right);
        }
        if (comptimeShouldNarrowBinary(expr.bin_op.left->type, expr.bin_op.right->type, *expr.bin_op.left,
                                       *expr.bin_op.right))
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 + (uint64_t)r.i64), *expr.bin_op.left,
                                         *expr.bin_op.right);
        int64_t out = 0;
        if (!checkedAddI64(l.i64, r.i64, out))
          throw FarError("comptime integer overflow");
        return comptimeIntBinaryResult(out, *expr.bin_op.left, *expr.bin_op.right);
      }
      if (l.kind == ComptimeValue::Kind::Float || r.kind == ComptimeValue::Kind::Float) {
        auto asF64 = [](const ComptimeValue& v, const TypeDesc& ty) -> double {
          if (v.kind == ComptimeValue::Kind::Float)
            return v.f64;
          if (v.kind == ComptimeValue::Kind::Int)
            return comptimeIntToF64(v, ty);
          throw FarError("comptime float op requires numeric operand");
        };
        double lf = asF64(l, expr.bin_op.left->type);
        double rf = asF64(r, expr.bin_op.right->type);
        if (op == "-")
          return makeFloatVal(lf - rf);
        if (op == "*")
          return makeFloatVal(lf * rf);
        if (op == "/") {
          if (lf != lf || rf != rf)
            return makeFloatVal(0.0);
          if (rf == 0.0) {
            if (lf == 0.0)
              return makeFloatVal(lf / rf);
            return makeFloatVal(0.0);
          }
          return makeFloatVal(lf / rf);
        }
        if (op == "%") {
          if (lf != lf || rf != rf || rf == 0.0)
            return makeFloatVal(0.0);
          return makeFloatVal(fmod(lf, rf));
        }
        if (op == "??") {
          if (!comptimeNullish(l))
            return l;
          return r;
        }
        throw FarError("unsupported comptime float binary op '" + op + "'");
      }
      requireComptimeInt(l, "comptime arithmetic");
      requireComptimeInt(r, "comptime arithmetic");
      const bool unsigned_op = comptimeUsesUnsigned(*expr.bin_op.left, *expr.bin_op.right);
      if (op == "**")
        return comptimeIntBinaryResult(comptimeIpow(l.i64, r.i64), left_expr, right_expr);
      if (op == "//") {
        if (unsigned_op)
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 / (uint64_t)r.i64), left_expr, right_expr);
        return comptimeIntBinaryResult(comptimeFloorDivI64(l.i64, r.i64), left_expr, right_expr);
      }
      if (op == "-") {
        if (unsigned_op) {
          if (!comptimeShouldNarrowBinary(left_expr.type, right_expr.type, left_expr, right_expr)) {
            uint64_t out = 0;
            if (!checkedSubU64(static_cast<uint64_t>(l.i64), static_cast<uint64_t>(r.i64), out))
              throw FarError("comptime integer overflow");
            return comptimeIntBinaryResult(static_cast<int64_t>(out), left_expr, right_expr);
          }
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 - (uint64_t)r.i64), left_expr, right_expr);
        }
        if (comptimeShouldNarrowBinary(left_expr.type, right_expr.type, left_expr, right_expr))
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 - (uint64_t)r.i64), left_expr, right_expr);
        int64_t out = 0;
        if (!checkedSubI64(l.i64, r.i64, out))
          throw FarError("comptime integer overflow");
        return comptimeIntBinaryResult(out, left_expr, right_expr);
      }
      if (op == "*") {
        if (unsigned_op) {
          if (!comptimeShouldNarrowBinary(left_expr.type, right_expr.type, left_expr, right_expr)) {
            uint64_t out = 0;
            if (!checkedMulU64(static_cast<uint64_t>(l.i64), static_cast<uint64_t>(r.i64), out))
              throw FarError("comptime integer overflow");
            return comptimeIntBinaryResult(static_cast<int64_t>(out), left_expr, right_expr);
          }
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 * (uint64_t)r.i64), left_expr, right_expr);
        }
        if (comptimeShouldNarrowBinary(left_expr.type, right_expr.type, left_expr, right_expr))
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 * (uint64_t)r.i64), left_expr, right_expr);
        int64_t out = 0;
        if (!checkedMulI64(l.i64, r.i64, out))
          throw FarError("comptime integer overflow");
        return comptimeIntBinaryResult(out, left_expr, right_expr);
      }
      if (op == "/") {
        if (r.i64 == 0)
          throw FarError("comptime division by zero");
        if (unsigned_op)
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 / (uint64_t)r.i64), left_expr, right_expr);
        if (l.i64 == INT64_MIN && r.i64 == -1)
          throw FarError("comptime integer overflow");
        return comptimeIntBinaryResult(l.i64 / r.i64, left_expr, right_expr);
      }
      if (op == "%") {
        if (r.i64 == 0)
          throw FarError("comptime modulo by zero");
        if (unsigned_op)
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 % (uint64_t)r.i64), left_expr, right_expr);
        if (l.i64 == INT64_MIN && r.i64 == -1)
          throw FarError("comptime integer overflow");
        return comptimeIntBinaryResult(l.i64 % r.i64, left_expr, right_expr);
      }
      if (unsigned_op) {
        uint64_t ul = (uint64_t)l.i64;
        uint64_t ur = (uint64_t)r.i64;
        if (op == "<")
          return makeIntVal(ul < ur ? 1 : 0);
        if (op == ">")
          return makeIntVal(ul > ur ? 1 : 0);
        if (op == "<=")
          return makeIntVal(ul <= ur ? 1 : 0);
        if (op == ">=")
          return makeIntVal(ul >= ur ? 1 : 0);
      }
      if (op == "<")
        return makeIntVal(l.i64 < r.i64 ? 1 : 0);
      if (op == ">")
        return makeIntVal(l.i64 > r.i64 ? 1 : 0);
      if (op == "<=")
        return makeIntVal(l.i64 <= r.i64 ? 1 : 0);
      if (op == ">=")
        return makeIntVal(l.i64 >= r.i64 ? 1 : 0);
      if (op == "&")
        return comptimeIntBinaryResult(l.i64 & r.i64, left_expr, right_expr);
      if (op == "|")
        return comptimeIntBinaryResult(l.i64 | r.i64, left_expr, right_expr);
      if (op == "^")
        return comptimeIntBinaryResult(l.i64 ^ r.i64, left_expr, right_expr);
      if (op == "<<") {
        if (r.i64 < 0 || r.i64 >= 64)
          throw FarError("comptime shift out of range");
        uint64_t ul = (uint64_t)(int64_t)l.i64;
        uint64_t ur = (uint64_t)r.i64;
        if (ur > 0 && ul > (UINT64_MAX >> ur))
          throw FarError("comptime integer overflow");
        int64_t out = (int64_t)(ul << ur);
        const bool unsigned_shift =
            isPrimitiveDesc(expr.bin_op.left->type) && isIntegerType(expr.bin_op.left->type.primitive) &&
            !typeInfo(expr.bin_op.left->type.primitive).is_signed;
        if (!unsigned_shift && l.i64 >= 0 && out < 0)
          throw FarError("comptime integer overflow");
        return comptimeIntBinaryResult(out, left_expr, right_expr);
      }
      if (op == ">>") {
        if (r.i64 < 0 || r.i64 >= 64)
          throw FarError("comptime shift out of range");
        auto isUnsigned = [](const TypeDesc& td) {
          return isPrimitiveDesc(td) && isIntegerType(td.primitive) && !typeInfo(td.primitive).is_signed;
        };
        const bool unsigned_shift = isUnsigned(expr.bin_op.left->type);
        if (unsigned_shift)
          return comptimeIntBinaryResult((int64_t)((uint64_t)l.i64 >> (uint64_t)r.i64), left_expr, right_expr);
        return comptimeIntBinaryResult(l.i64 >> r.i64, left_expr, right_expr);
      }
      if (op == "===")
        return makeIntVal(l.i64 == r.i64 ? 1 : 0);
      if (op == "!==")
        return makeIntVal(l.i64 != r.i64 ? 1 : 0);
      if (op == "in" || op == "not in") {
        if (l.kind == ComptimeValue::Kind::String && r.kind == ComptimeValue::Kind::String) {
          bool found = r.str.find(l.str) != std::string::npos;
          if (op == "not in")
            found = !found;
          return makeIntVal(found ? 1 : 0);
        }
        throw FarError("comptime '" + op + "' requires string operands");
      }
      throw FarError("unsupported comptime binary op '" + op + "'");
    }
    case Expr::PrefixExprK: {
      ComptimeValue v = evalExpr(ctx, *expr.prefix.operand);
      if (expr.prefix.op == "-") {
        requireComptimeInt(v, "comptime unary '-'");
        if (v.i64 == INT64_MIN)
          throw FarError("comptime integer overflow");
        return makeIntVal(-v.i64);
      }
      if (expr.prefix.op == "!")
        return makeIntVal(comptimeTruthy(v) ? 0 : 1);
      if (expr.prefix.op == "~") {
        requireComptimeInt(v, "comptime '~'");
        return makeIntVal(~v.i64);
      }
      if (expr.prefix.op == "++" || expr.prefix.op == "--") {
        if (expr.prefix.operand->kind != Expr::Variable)
          throw FarError("comptime ++/-- requires a variable operand");
        const std::string& name = expr.prefix.operand->var.name;
        auto it = ctx.vars.find(name);
        if (it == ctx.vars.end())
          throw FarError("unknown comptime variable '" + name + "'");
        requireComptimeInt(it->second, "comptime ++/--");
        int64_t delta = expr.prefix.op == "++" ? 1 : -1;
        int64_t out = 0;
        if (!checkedAddI64(it->second.i64, delta, out))
          throw FarError("comptime integer overflow");
        it->second = comptimeIntBinaryResult(out, *expr.prefix.operand, *expr.prefix.operand);
        return it->second;
      }
      throw FarError("unsupported comptime prefix op");
    }
    case Expr::PostfixExprK: {
      if (expr.postfix.op != "++" && expr.postfix.op != "--")
        throw FarError("unsupported comptime postfix op");
      if (expr.postfix.operand->kind != Expr::Variable)
        throw FarError("comptime ++/-- requires a variable operand");
      const std::string& name = expr.postfix.operand->var.name;
      auto it = ctx.vars.find(name);
      if (it == ctx.vars.end())
        throw FarError("unknown comptime variable '" + name + "'");
      requireComptimeInt(it->second, "comptime ++/--");
      ComptimeValue old = it->second;
      int64_t delta = expr.postfix.op == "++" ? 1 : -1;
      int64_t out = 0;
      if (!checkedAddI64(it->second.i64, delta, out))
        throw FarError("comptime integer overflow");
      it->second = comptimeIntBinaryResult(out, *expr.postfix.operand, *expr.postfix.operand);
      return old;
    }
    case Expr::TernaryExprK: {
      ComptimeValue cond = evalExpr(ctx, *expr.ternary.cond);
      if (comptimeTruthy(cond))
        return evalExpr(ctx, *expr.ternary.then_br);
      return evalExpr(ctx, *expr.ternary.else_br);
    }
    case Expr::ComptimeExprK:
      if (expr.comptime_expr.is_block)
        return evalComptimeBlockExpr(ctx, expr.comptime_expr.block);
      return evalExpr(ctx, *expr.comptime_expr.value);
    case Expr::FnCall: {
      if (expr.call.name == "reflect_compile_value") {
        if (expr.call.args.size() != 1)
          throw FarError("reflect_compile_value() expects 1 argument");
        return evalExpr(ctx, *expr.call.args[0].value);
      }
      if (expr.call.name == "reflect_name") {
        if (expr.call.args.size() != 1)
          throw FarError("reflect_name() expects 1 argument");
        const Expr& arg = *expr.call.args[0].value;
        std::string type_name = typeNameFromTypeof(ctx, arg);
        if (!type_name.empty())
          return makeIntVal(reflectNameHash(type_name));
        throw FarError("reflect_name() requires typeof(Type)");
      }
      if (expr.call.name == "reflect_kind" || expr.call.name == "reflect_fields") {
        if (expr.call.args.size() != 1)
          throw FarError(expr.call.name + "() expects 1 argument");
        const Expr& arg = *expr.call.args[0].value;
        int64_t tag = 0;
        if (arg.kind == Expr::TypeUnaryExprK &&
            (arg.type_unary.op == "type_tag" ||
             (arg.type_unary.op == "typeof" && arg.type_unary.has_type))) {
          tag = typeTagFromUnary(ctx, arg);
        } else {
          ComptimeValue v = evalExpr(ctx, arg);
          tag = v.i64;
        }
        if (expr.call.name == "reflect_kind")
          return makeIntVal(reflectKindFromTag(tag));
        return makeIntVal(reflectFieldsFromTag(tag));
      }
      const Function* fn = nullptr;
      auto fit = ctx.fns.find(expr.call.name);
      if (fit != ctx.fns.end())
        fn = fit->second;
      if (!fn && ctx.program) {
        for (const auto& f : ctx.program->functions) {
          if (f.name == expr.call.name && (f.is_constexpr || f.is_consteval || f.is_codegen)) {
            fn = &f;
            break;
          }
        }
      }
      if (!fn)
        throw FarError("undefined comptime function '" + expr.call.name + "'");
      if (fn->is_consteval && !ctx.in_comptime)
        throw FarError("consteval function '" + fn->name + "' requires comptime context");
      std::vector<ComptimeValue> args;
      for (const auto& a : expr.call.args)
        args.push_back(evalExpr(ctx, *a.value));
      return evalFunctionBody(ctx, *fn, args);
    }
    case Expr::TypeUnaryExprK: {
      if (expr.type_unary.op == "type_tag")
        return makeIntVal(typeTagFromUnary(ctx, expr));
      if (expr.type_unary.op == "typeof") {
        ComptimeValue cv;
        cv.kind = ComptimeValue::Kind::String;
        cv.str = typeDescName(typeDescFromUnary(expr));
        return cv;
      }
      if (expr.type_unary.op == "sizeof") {
        TypeDesc td = typeDescFromUnary(expr);
        return makeIntVal(elemSizeBytes(td));
      }
      if (expr.type_unary.op == "alignof") {
        TypeDesc td = typeDescFromUnary(expr);
        return makeIntVal(elemAlignBytes(td));
      }
      throw FarError("unsupported comptime type operator");
    }
    default:
      throw FarError("expression is not evaluable at compile time");
  }
}

void resolveComptimeTypes(TypeDesc& td, ComptimeContext& ctx) {
  if (td.comptime_size) {
    ComptimeValue v = evalExpr(ctx, *td.comptime_size);
    td.const_n = v.i64;
    td.comptime_size.reset();
  }
  if (td.form == TypeForm::FixedArray) {
    if (td.const_n <= 0)
      throw FarError("FixedArray size must be positive");
    if (td.const_n > (1 << 24))
      throw FarError("FixedArray size exceeds maximum (16777216)");
  }
  for (auto& a : td.args)
    resolveComptimeTypes(a, ctx);
}

static bool isBoolProducingOp(const std::string& op) {
  return op == "==" || op == "!=" || op == "===" || op == "!==" || op == "<" || op == ">" ||
         op == "<=" || op == ">=" || op == "and" || op == "or" || op == "&&" || op == "||" ||
         op == "in" || op == "not in";
}

static void replaceWithComptimeValue(Expr& expr, const ComptimeValue& v) {
  if (v.kind == ComptimeValue::Kind::String) {
    TypeDesc ty = TypeDesc::prim(FarTypeId::String);
    expr = Expr();
    expr.kind = Expr::String;
    expr.type = ty;
    expr.string_lit.value = v.str;
    return;
  }
  TypeDesc ty = expr.type;
  if (expr.kind == Expr::Binary && isBoolProducingOp(expr.bin_op.op))
    ty = TypeDesc::prim(FarTypeId::Bool);
  else if (expr.kind == Expr::PrefixExprK && expr.prefix.op == "!")
    ty = TypeDesc::prim(FarTypeId::Bool);
  else if (expr.kind == Expr::Binary && expr.bin_op.op == "!" && expr.bin_op.left->kind == Expr::Int &&
           expr.bin_op.left->int_lit.value == 0)
    ty = TypeDesc::prim(FarTypeId::Bool);
  else if (expr.kind == Expr::IsExprK)
    ty = TypeDesc::prim(FarTypeId::Bool);
  if (v.kind == ComptimeValue::Kind::Bool) {
    expr = Expr();
    expr.kind = Expr::Int;
    expr.type = TypeDesc::prim(FarTypeId::Bool);
    expr.int_lit.value = v.b ? 1 : 0;
    return;
  }
  if (v.kind == ComptimeValue::Kind::Float ||
      (isPrimitiveDesc(ty) && (ty.primitive == FarTypeId::F64 || ty.primitive == FarTypeId::F32))) {
    TypeDesc src_ty = expr.type;
    if (expr.kind == Expr::AsExprK && expr.as_expr.value)
      src_ty = expr.as_expr.value->type;
    expr = Expr();
    expr.kind = Expr::Float;
    if (isPrimitiveDesc(ty) && (ty.primitive == FarTypeId::F64 || ty.primitive == FarTypeId::F32))
      expr.type = ty;
    else
      expr.type = TypeDesc::prim(FarTypeId::F64);
    expr.float_lit.is_float = expr.type.primitive == FarTypeId::F32;
    if (v.kind == ComptimeValue::Kind::Float)
      expr.float_lit.value = v.f64;
    else
      expr.float_lit.value = comptimeIntToF64(v, src_ty);
    return;
  }
  expr = Expr();
  expr.kind = Expr::Int;
  expr.type = ty;
  expr.int_lit.value = v.i64;
}

static void replaceWithInt(Expr& expr, int64_t v) {
  replaceWithComptimeValue(expr, makeIntVal(v));
}

static void foldStmtComptime(Stmt& stmt, ComptimeContext& ctx);

static void mergeComptimeVarsExcept(std::unordered_map<std::string, ComptimeValue>& into,
                                    const std::unordered_map<std::string, ComptimeValue>& from,
                                    const std::string& skip) {
  for (const auto& kv : from) {
    if (kv.first != skip)
      into[kv.first] = kv.second;
  }
}

// Fold only constexpr/comptime statements without rewriting runtime control flow.
static void foldConstexprOnlyStmts(const std::vector<std::unique_ptr<Stmt>>& stmts, ComptimeContext& ctx) {
  for (const auto& stmt : stmts) {
    if (!stmt)
      continue;
    switch (stmt->kind) {
      case Stmt::LetStmt:
        if (stmt->let.is_constexpr)
          foldStmtComptime(*stmt, ctx);
        break;
      case Stmt::ComptimeBlockK:
        foldStmtComptime(*stmt, ctx);
        break;
      case Stmt::IfStmt:
        for (auto& c : stmt->if_stmt.clauses)
          foldConstexprOnlyStmts(c.body, ctx);
        foldConstexprOnlyStmts(stmt->if_stmt.else_body, ctx);
        break;
      case Stmt::TryStmtK:
        foldConstexprOnlyStmts(stmt->try_stmt.try_body, ctx);
        foldConstexprOnlyStmts(stmt->try_stmt.catch_body, ctx);
        foldConstexprOnlyStmts(stmt->try_stmt.finally_body, ctx);
        break;
      case Stmt::WhileStmt:
        foldConstexprOnlyStmts(stmt->while_stmt.body, ctx);
        break;
      case Stmt::ForStmt:
        foldConstexprOnlyStmts(stmt->for_stmt.body, ctx);
        break;
      default:
        break;
    }
  }
}

static std::optional<ComptimeValue> comptimeTryBodyThrowLiteral(ComptimeContext& ctx,
                                                                const std::vector<std::unique_ptr<Stmt>>& try_body) {
  for (const auto& s : try_body) {
    if (s && s->kind == Stmt::ThrowStmtK && s->throw_stmt.value)
      return evalExpr(ctx, *s->throw_stmt.value);
  }
  return std::nullopt;
}

static void foldExprComptime(Expr& expr, ComptimeContext& ctx) {
  if (expr.kind == Expr::ComptimeExprK) {
    if (expr.comptime_expr.is_block) {
      ComptimeValue v = evalComptimeBlockExpr(ctx, expr.comptime_expr.block);
      replaceWithComptimeValue(expr, v);
      return;
    }
    ComptimeValue v = evalExpr(ctx, *expr.comptime_expr.value);
    replaceWithComptimeValue(expr, v);
    return;
  }
  if (expr.kind == Expr::Variable) {
    return;
  }
  if (expr.kind == Expr::Binary) {
    const std::string& op = expr.bin_op.op;
    foldExprComptime(*expr.bin_op.left, ctx);
    ComptimeValue lv;
    const bool left_known = tryEvalExpr(ctx, *expr.bin_op.left, lv);
    if (op == "and" || op == "&&") {
      if (left_known && !comptimeTruthy(lv)) {
        ComptimeValue v;
        if (tryEvalExpr(ctx, expr, v))
          replaceWithComptimeValue(expr, v);
        return;
      }
      if (!left_known)
        return;
    } else if (op == "or" || op == "||") {
      if (left_known && comptimeTruthy(lv)) {
        ComptimeValue v;
        if (tryEvalExpr(ctx, expr, v))
          replaceWithComptimeValue(expr, v);
        return;
      }
      if (!left_known)
        return;
    } else if (op == "??") {
      if (left_known) {
        if (!comptimeNullish(lv)) {
          ComptimeValue v;
          if (tryEvalExpr(ctx, expr, v))
            replaceWithComptimeValue(expr, v);
          return;
        }
        foldExprComptime(*expr.bin_op.right, ctx);
        ComptimeValue v;
        if (tryEvalExpr(ctx, expr, v))
          replaceWithComptimeValue(expr, v);
        return;
      }
      return;
    }
    foldExprComptime(*expr.bin_op.right, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithComptimeValue(expr, v);
    return;
  }
  if (expr.kind == Expr::PrefixExprK) {
    foldExprComptime(*expr.prefix.operand, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithComptimeValue(expr, v);
    return;
  }
  if (expr.kind == Expr::PostfixExprK) {
    foldExprComptime(*expr.postfix.operand, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithComptimeValue(expr, v);
    return;
  }
  if (expr.kind == Expr::AssignExprK) {
    if (expr.assign.target)
      foldExprComptime(*expr.assign.target, ctx);
    foldExprComptime(*expr.assign.value, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithComptimeValue(expr, v);
    return;
  }
  if (expr.kind == Expr::FnCall) {
    for (auto& a : expr.call.args)
      foldExprComptime(*a.value, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithComptimeValue(expr, v);
    return;
  }
  if (expr.kind == Expr::TernaryExprK) {
    foldExprComptime(*expr.ternary.cond, ctx);
    ComptimeValue cv;
    if (tryEvalExpr(ctx, *expr.ternary.cond, cv)) {
      auto saved = ctx.vars;
      if (comptimeTruthy(cv))
        foldExprComptime(*expr.ternary.then_br, ctx);
      else
        foldExprComptime(*expr.ternary.else_br, ctx);
      ctx.vars = saved;
    } else {
      // Runtime condition: preserve short-circuit semantics; do not fold branches.
      return;
    }
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithComptimeValue(expr, v);
    return;
  }
  if (expr.kind == Expr::CastExpr || expr.kind == Expr::AsExprK) {
    if (expr.kind == Expr::CastExpr)
      foldExprComptime(*expr.cast.value, ctx);
    else
      foldExprComptime(*expr.as_expr.value, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithComptimeValue(expr, v);
    return;
  }
}

static bool isNonScalarConstexprValue(const Expr& expr) {
  return expr.kind == Expr::ArrayLitExpr && expr.array_lit.elements.empty();
}

static void storeConstexprBinding(ComptimeContext& ctx, const std::string& name, const Expr& value) {
  if (isNonScalarConstexprValue(value))
    return;
  ctx.vars[name] = evalExpr(ctx, value);
}

static bool foreachConstEmpty(const Expr& iter, const Program* program) {
  if (iter.kind == Expr::ArrayLitExpr)
    return iter.array_lit.elements.empty();
  if (iter.kind == Expr::String)
    return iter.string_lit.value.empty();
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

static void foldStmtComptime(Stmt& stmt, ComptimeContext& ctx) {
  switch (stmt.kind) {
    case Stmt::LetStmt:
      if (stmt.let.value)
        foldExprComptime(*stmt.let.value, ctx);
      if (stmt.let.is_constexpr && stmt.let.value)
        storeConstexprBinding(ctx, stmt.let.name, *stmt.let.value);
      return;
    case Stmt::ReturnStmt:
      if (stmt.ret.has_value)
        foldExprComptime(*stmt.ret.value, ctx);
      return;
    case Stmt::ExprStmtK:
      foldExprComptime(*stmt.expr_stmt.expr, ctx);
      return;
    case Stmt::PrintStmt:
      foldExprComptime(*stmt.print.value, ctx);
      return;
    case Stmt::IfStmt: {
      bool matched = false;
      bool unknown = false;
      for (auto& c : stmt.if_stmt.clauses) {
        foldExprComptime(*c.condition, ctx);
        ComptimeValue cv;
        if (!tryEvalExpr(ctx, *c.condition, cv)) {
          unknown = true;
          break;
        }
        if (comptimeTruthy(cv)) {
          for (auto& s : c.body)
            foldStmtComptime(*s, ctx);
          matched = true;
          break;
        }
      }
      if (matched || unknown)
        return;
      for (auto& s : stmt.if_stmt.else_body)
        foldStmtComptime(*s, ctx);
      return;
    }
    case Stmt::WhileStmt: {
      ComptimeValue cv;
      if (tryEvalExpr(ctx, *stmt.while_stmt.condition, cv)) {
        if (!comptimeTruthy(cv))
          return;
        for (auto& s : stmt.while_stmt.body)
          foldStmtComptime(*s, ctx);
        return;
      }
      foldExprComptime(*stmt.while_stmt.condition, ctx);
      return;
    }
    case Stmt::ForStmt:
      if (stmt.for_stmt.is_foreach) {
        foldExprComptime(*stmt.for_stmt.foreach_iter, ctx);
        if (stmt.for_stmt.foreach_iter->kind == Expr::ArrayLitExpr) {
          if (stmt.for_stmt.foreach_iter->array_lit.elements.empty())
            return;
          ComptimeValue el =
              evalExpr(ctx, *stmt.for_stmt.foreach_iter->array_lit.elements.front());
          auto saved = ctx.vars;
          ctx.vars[stmt.for_stmt.foreach_var] = el;
          foldConstexprOnlyStmts(stmt.for_stmt.body, ctx);
          mergeComptimeVarsExcept(saved, ctx.vars, stmt.for_stmt.foreach_var);
          ctx.vars = std::move(saved);
          return;
        }
        if (foreachConstEmpty(*stmt.for_stmt.foreach_iter, ctx.program))
          return;
        if (auto elems = resolveComptimeArrayElems(ctx, *stmt.for_stmt.foreach_iter)) {
          if (elems->empty())
            return;
          for (auto& s : stmt.for_stmt.body)
            foldStmtComptime(*s, ctx);
          return;
        }
        return;
      }
      if (stmt.for_stmt.is_parallel || stmt.for_stmt.is_range) {
        if (stmt.for_stmt.range_start)
          foldExprComptime(*stmt.for_stmt.range_start, ctx);
        if (stmt.for_stmt.range_end)
          foldExprComptime(*stmt.for_stmt.range_end, ctx);
        if (stmt.for_stmt.is_range && stmt.for_stmt.range_start && stmt.for_stmt.range_end) {
          ComptimeValue sv;
          ComptimeValue ev;
          if (tryEvalExpr(ctx, *stmt.for_stmt.range_start, sv) &&
              tryEvalExpr(ctx, *stmt.for_stmt.range_end, ev) &&
              sv.kind == ComptimeValue::Kind::Int && ev.kind == ComptimeValue::Kind::Int) {
            bool exclusive = stmt.for_stmt.range_exclusive;
            if (sv.i64 <= ev.i64 ? (exclusive ? sv.i64 >= ev.i64 : sv.i64 > ev.i64) : false)
              return;
            int64_t sample = sv.i64 <= ev.i64 ? (exclusive ? ev.i64 - 1 : ev.i64)
                                              : (exclusive ? ev.i64 + 1 : ev.i64);
            auto saved = ctx.vars;
            ctx.vars[stmt.for_stmt.range_var] = makeIntVal(sample);
            foldConstexprOnlyStmts(stmt.for_stmt.body, ctx);
            mergeComptimeVarsExcept(saved, ctx.vars, stmt.for_stmt.range_var);
            ctx.vars = std::move(saved);
            return;
          }
        }
      }
      if (stmt.for_stmt.init)
        foldStmtComptime(*stmt.for_stmt.init, ctx);
      if (stmt.for_stmt.cond) {
        foldExprComptime(*stmt.for_stmt.cond, ctx);
        ComptimeValue cv;
        if (tryEvalExpr(ctx, *stmt.for_stmt.cond, cv)) {
          if (!comptimeTruthy(cv))
            return;
          for (auto& s : stmt.for_stmt.body)
            foldStmtComptime(*s, ctx);
          if (stmt.for_stmt.step)
            foldStmtComptime(*stmt.for_stmt.step, ctx);
          return;
        }
      }
      {
        auto saved = ctx.vars;
        for (auto& s : stmt.for_stmt.body)
          foldStmtComptime(*s, ctx);
        if (stmt.for_stmt.step)
          foldStmtComptime(*stmt.for_stmt.step, ctx);
        ctx.vars = saved;
      }
      return;
    case Stmt::TryStmtK: {
      auto saved = ctx.vars;
      bool catch_path = comptimeTryBodyMayThrow(stmt.try_stmt.try_body);
      if (!catch_path) {
        for (auto& s : stmt.try_stmt.try_body)
          foldStmtComptime(*s, ctx);
        for (const auto& kv : ctx.vars)
          saved[kv.first] = kv.second;
      }
      ctx.vars = std::move(saved);
      if (stmt.try_stmt.has_catch) {
        if (catch_path) {
          auto catch_base = ctx.vars;
          if (auto thrown = comptimeTryBodyThrowLiteral(ctx, stmt.try_stmt.try_body))
            ctx.vars[stmt.try_stmt.catch_var] = *thrown;
          foldConstexprOnlyStmts(stmt.try_stmt.catch_body, ctx);
          mergeComptimeVarsExcept(catch_base, ctx.vars, stmt.try_stmt.catch_var);
          ctx.vars = std::move(catch_base);
        } else {
          auto catch_saved = ctx.vars;
          for (auto& s : stmt.try_stmt.catch_body)
            foldStmtComptime(*s, ctx);
          ctx.vars = std::move(catch_saved);
        }
      }
      if (stmt.try_stmt.has_finally) {
        auto fin_saved = ctx.vars;
        for (auto& s : stmt.try_stmt.finally_body)
          foldStmtComptime(*s, ctx);
        for (const auto& kv : ctx.vars)
          fin_saved[kv.first] = kv.second;
        ctx.vars = std::move(fin_saved);
      }
      return;
    }
    case Stmt::ThrowStmtK:
      foldExprComptime(*stmt.throw_stmt.value, ctx);
      return;
    case Stmt::DeferStmtK:
      foldExprComptime(*stmt.defer.expr, ctx);
      return;
    case Stmt::UnsafeStmtK: {
      auto saved = ctx.vars;
      for (auto& s : stmt.unsafe.body)
        foldStmtComptime(*s, ctx);
      ctx.vars = saved;
      return;
    }
    case Stmt::MatchStmtK:
      foldExprComptime(*stmt.match_stmt.scrutinee, ctx);
      {
        ComptimeValue scrut;
        if (tryEvalExpr(ctx, *stmt.match_stmt.scrutinee, scrut)) {
          for (auto& arm : stmt.match_stmt.arms) {
            if (!arm.pat || !comptimePatternMatches(*arm.pat, scrut))
              continue;
            auto saved = ctx.vars;
            std::string bind;
            if (arm.pat->kind == PatKind::Bind)
              bind = arm.pat->bind_name;
            if (!bind.empty())
              ctx.vars[bind] = scrut;
            for (auto& s : arm.body)
              foldStmtComptime(*s, ctx);
            for (const auto& kv : ctx.vars) {
              if (kv.first == bind)
                continue;
              saved[kv.first] = kv.second;
            }
            ctx.vars = std::move(saved);
            return;
          }
          return;
        }
        for (auto& arm : stmt.match_stmt.arms) {
          auto saved = ctx.vars;
          for (auto& s : arm.body)
            foldStmtComptime(*s, ctx);
          ctx.vars = saved;
        }
      }
      return;
    default:
      return;
  }
}

static void runComptimeStmts(const std::vector<std::unique_ptr<Stmt>>& stmts, ComptimeContext& ctx) {
  for (const auto& stmt : stmts) {
    if (stmt->kind == Stmt::LetStmt && stmt->let.value) {
      foldExprComptime(*stmt->let.value, ctx);
      if (!isNonScalarConstexprValue(*stmt->let.value))
        ctx.vars[stmt->let.name] = evalExpr(ctx, *stmt->let.value);
      if (stmt->let.is_constexpr)
        storeConstexprBinding(ctx, stmt->let.name, *stmt->let.value);
      continue;
    }
    if (evalComptimeStmt(ctx, *stmt))
      continue;
    foldStmtComptime(*stmt, ctx);
  }
}

static void materializeCodegenFunction(Program& program, Function& fn, ComptimeContext& ctx) {
  ComptimeValue result = evalFunctionBody(ctx, fn, {});
  fn.is_comptime_materialized = true;
  fn.body.clear();
  auto ret = std::make_unique<Stmt>();
  ret->kind = Stmt::ReturnStmt;
  ret->ret.has_value = true;
  auto val = Expr::makeInt(0);
  replaceWithComptimeValue(*val, result);
  ret->ret.value = std::move(val);
  fn.body.push_back(std::move(ret));
}

static void seedComptimeGlobals(Program& program, ComptimeContext& ctx) {
  for (const auto& fn : program.functions) {
    if (fn.is_constexpr || fn.is_consteval || fn.is_codegen)
      ctx.fns[fn.name] = &fn;
  }
  for (auto& stmt : program.comptime_stmts) {
    if (stmt->kind == Stmt::ComptimeBlockK)
      runComptimeStmts(stmt->comptime_block, ctx);
    else if (stmt->kind == Stmt::LetStmt && stmt->let.is_constexpr && stmt->let.value)
      storeConstexprBinding(ctx, stmt->let.name, *stmt->let.value);
  }
}

void prepareProgram(Program& program) {
  expandMacros(program);

  ObjectRegistry reg;
  reg.build(program, false);
  reg.program = &program;

  ComptimeContext ctx;
  ctx.program = &program;
  ctx.obj_reg = &reg;
  seedComptimeGlobals(program, ctx);

  for (auto& fn : program.functions) {
    if (fn.is_codegen && !fn.is_comptime_materialized)
      materializeCodegenFunction(program, fn, ctx);
  }

  for (auto& td : program.user_types) {
    for (auto& f : td.fields)
      resolveComptimeTypes(f.type, ctx);
  }

  for (auto& fn : program.functions) {
    for (auto& p : fn.params)
      resolveComptimeTypes(p.type, ctx);
    resolveComptimeTypes(fn.return_type, ctx);
  }
}

void foldProgramExpressions(Program& program) {
  ObjectRegistry reg;
  reg.build(program, false);
  ComptimeContext ctx;
  ctx.program = &program;
  ctx.obj_reg = &reg;
  seedComptimeGlobals(program, ctx);
  for (auto& stmt : program.comptime_stmts) {
    if (stmt->kind == Stmt::ComptimeBlockK)
      runComptimeStmts(stmt->comptime_block, ctx);
    else
      foldStmtComptime(*stmt, ctx);
  }
  for (auto& fn : program.functions) {
    auto saved_vars = ctx.vars;
    for (auto& stmt : fn.body)
      foldStmtComptime(*stmt, ctx);
    ctx.vars = saved_vars;
  }
}

}  // namespace far
