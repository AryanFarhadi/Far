#include "typecheck.h"

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
         td.form == TypeForm::Span || td.form == TypeForm::Slice;
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

static void inferReturnType(Function& fn) {
  if (fn.return_type_explicit)
    return;
  std::unordered_map<std::string, TypeDesc> param_types;
  for (const auto& p : fn.params)
    param_types[p.name] = p.type;
  for (const auto& stmt : *functionBody(fn)) {
    if (stmt->kind == Stmt::ReturnStmt && stmt->ret.has_value &&
        exprLooksString(*stmt->ret.value, param_types)) {
      fn.return_type = TypeDesc::prim(FarTypeId::String);
      return;
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
    for (auto& fn : program_.functions) {
      if (!fn.type_params.empty())
        continue;
      checkFunction(fn);
    }
    for (auto& fn : program_.synthetic_functions)
      checkFunction(fn);
  }

 private:
  Program& program_;
  ObjectRegistry obj_reg_;
  std::unordered_map<std::string, std::vector<const Function*>> fn_overloads_;
  std::unordered_map<std::string, TypeDesc> locals_;
  const Function* current_fn_ = nullptr;
  const UserTypeDef* current_user_type_ = nullptr;
  int lambda_counter_ = 0;
  int unsafe_depth_ = 0;
  int comptime_depth_ = 0;

  static bool isIntegerScrutinee(const TypeDesc& ty) {
    return isPrimitiveDesc(ty) && isIntegerType(ty.primitive);
  }

  void injectConstexprFromStmts(const std::vector<std::unique_ptr<Stmt>>& stmts) {
    for (const auto& stmt : stmts) {
      if (stmt->kind == Stmt::LetStmt && stmt->let.is_constexpr) {
        if (!locals_.count(stmt->let.name)) {
          TypeDesc ty =
              stmt->let.explicit_type ? stmt->let.type : TypeDesc::prim(FarTypeId::I64);
          locals_[stmt->let.name] = ty;
        }
      } else if (stmt->kind == Stmt::ComptimeBlockK) {
        injectConstexprFromStmts(stmt->comptime_block);
      }
    }
  }

  void injectConstexprGlobals() {
    injectConstexprFromStmts(program_.comptime_stmts);
  }

  void checkPattern(Pattern& pat, const TypeDesc& scrut_ty) {
    switch (pat.kind) {
      case PatKind::Wildcard:
        return;
      case PatKind::Bind:
        locals_[pat.bind_name] = scrut_ty;
        return;
      case PatKind::Literal:
        if (!isIntegerScrutinee(scrut_ty))
          throw FarError("literal pattern requires integer scrutinee");
        return;
      case PatKind::EnumVariant: {
        int v = obj_reg_.enumVariantValue(pat.type_name, pat.variant);
        if (v < 0)
          throw FarError("unknown enum variant " + pat.type_name + "." + pat.variant);
        pat.variant_value = v;
        if (!isIntegerScrutinee(scrut_ty))
          throw FarError("enum pattern requires integer scrutinee");
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
        if (!isUserDesc(scrut_ty) || scrut_ty.user_name != pat.type_name)
          throw FarError("type pattern mismatch");
        return;
      case PatKind::StructDestructure: {
        const UserTypeDef* td = obj_reg_.lookup(pat.type_name);
        if (!td)
          throw FarError("unknown type '" + pat.type_name + "'");
        if (!isUserDesc(scrut_ty) || scrut_ty.user_name != pat.type_name)
          throw FarError("struct pattern type mismatch");
        for (size_t i = 0; i < pat.fields.size(); ++i) {
          int fidx = static_cast<int>(i);
          if (!pat.field_names.empty()) {
            fidx = obj_reg_.lookupFieldIndex(TypeDesc::user(pat.type_name), pat.field_names[i]);
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
    for (auto& p : fn.params) {
      if (p.default_value) {
        TypeDesc dv = checkExpr(*p.default_value);
        if (!p.type_explicit && !typeDescEquals(p.type, dv))
          p.type = dv;
      }
      locals_[p.name] = p.type;
    }
    injectConstexprGlobals();
    const std::vector<std::unique_ptr<Stmt>>* body = &fn.body;
    if (fn.body_source)
      body = &fn.body_source->body;
    else if (fn.shared_body)
      body = fn.shared_body;
    for (const auto& stmt : *body)
      checkStmt(*stmt);
    current_fn_ = nullptr;
    current_user_type_ = nullptr;
  }

  void checkStmt(Stmt& stmt) {
    switch (stmt.kind) {
      case Stmt::LetStmt: {
        TypeDesc ty = checkExpr(*stmt.let.value);
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
          }
          if (!canAssignTypes(ty, stmt.let.type))
            throw FarError("type mismatch in let: expected " + typeDescName(stmt.let.type) + ", got " +
                           typeDescName(ty));
          locals_[stmt.let.name] = stmt.let.type;
        } else {
          locals_[stmt.let.name] = ty;
        }
        break;
      }
      case Stmt::ReturnStmt:
        if (stmt.ret.has_value) {
          TypeDesc ty = checkExpr(*stmt.ret.value);
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
      case Stmt::IfStmt:
        for (const auto& c : stmt.if_stmt.clauses) {
          checkExpr(*c.condition);
          for (const auto& s : c.body)
            checkStmt(*s);
        }
        for (const auto& s : stmt.if_stmt.else_body)
          checkStmt(*s);
        break;
      case Stmt::WhileStmt:
        checkExpr(*stmt.while_stmt.condition);
        for (const auto& s : stmt.while_stmt.body)
          checkStmt(*s);
        break;
      case Stmt::ForStmt:
        if (stmt.for_stmt.is_parallel) {
          checkExpr(*stmt.for_stmt.range_start);
          checkExpr(*stmt.for_stmt.range_end);
          break;
        }
        if (stmt.for_stmt.is_range) {
          checkExpr(*stmt.for_stmt.range_start);
          checkExpr(*stmt.for_stmt.range_end);
          locals_[stmt.for_stmt.range_var] = defaultIntType();
          for (const auto& s : stmt.for_stmt.body)
            checkStmt(*s);
          locals_.erase(stmt.for_stmt.range_var);
          break;
        }
        if (stmt.for_stmt.is_foreach) {
          TypeDesc coll_ty = checkExpr(*stmt.for_stmt.foreach_iter);
          if (!isIndexable(coll_ty))
            throw FarError("for-in requires an indexable collection (array, list, slice, ...), not " +
                           typeDescName(coll_ty));
          TypeDesc elem = elemTypeOf(coll_ty);
          locals_[stmt.for_stmt.foreach_var] = elem;
          for (const auto& s : stmt.for_stmt.body)
            checkStmt(*s);
          locals_.erase(stmt.for_stmt.foreach_var);
          break;
        }
        if (stmt.for_stmt.init)
          checkStmt(*stmt.for_stmt.init);
        if (stmt.for_stmt.cond)
          checkExpr(*stmt.for_stmt.cond);
        if (stmt.for_stmt.step)
          checkStmt(*stmt.for_stmt.step);
        for (const auto& s : stmt.for_stmt.body)
          checkStmt(*s);
        break;
      case Stmt::YieldStmtK:
        if (!current_fn_->is_generator && !current_fn_->is_coroutine)
          throw FarError("yield only allowed in generator/coroutine functions");
        if (stmt.yield.has_value)
          checkExpr(*stmt.yield.value);
        break;
      case Stmt::BreakStmt:
      case Stmt::ContinueStmt:
        break;
      case Stmt::DeferStmtK:
        checkExpr(*stmt.defer.expr);
        break;
      case Stmt::UnsafeStmtK:
        ++unsafe_depth_;
        for (const auto& s : stmt.unsafe.body)
          checkStmt(*s);
        --unsafe_depth_;
        break;
      case Stmt::TryStmtK:
        for (const auto& s : stmt.try_stmt.try_body)
          checkStmt(*s);
        if (stmt.try_stmt.has_catch) {
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
          locals_[stmt.try_stmt.catch_var] = catch_ty;
          for (const auto& s : stmt.try_stmt.catch_body)
            checkStmt(*s);
          locals_.erase(stmt.try_stmt.catch_var);
        }
        if (stmt.try_stmt.has_finally) {
          for (const auto& s : stmt.try_stmt.finally_body)
            checkStmt(*s);
        }
        break;
      case Stmt::ThrowStmtK:
        checkExpr(*stmt.throw_stmt.value);
        break;
      case Stmt::MatchStmtK: {
        TypeDesc st = checkExpr(*stmt.match_stmt.scrutinee);
        for (auto& arm : stmt.match_stmt.arms) {
          auto saved = locals_;
          checkPattern(*arm.pat, st);
          for (const auto& s : arm.body)
            checkStmt(*s);
          locals_ = saved;
        }
        break;
      }
      case Stmt::ComptimeBlockK:
      case Stmt::CodegenBlockK:
        break;
    }
  }

  TypeDesc checkExpr(Expr& expr) {
    TypeDesc ty = TypeDesc::prim(FarTypeId::I64);
    switch (expr.kind) {
      case Expr::Int:
        if (isPrim(expr.type, FarTypeId::Bool))
          ty = TypeDesc::prim(FarTypeId::Bool);
        else
          ty = defaultIntType();
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
        if (it == locals_.end())
          throwAt(expr, "undefined variable '" + expr.var.name + "'");
        ty = it->second;
        break;
      }
      case Expr::Binary: {
        TypeDesc lt = checkExpr(*expr.bin_op.left);
        TypeDesc rt = checkExpr(*expr.bin_op.right);
        const std::string& op = expr.bin_op.op;
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
        } else if (op == "and" || op == "or" || op == "&&" || op == "||" || op == "==" || op == "!=" ||
                 op == "===" || op == "!==" || op == "<" || op == ">" || op == "<=" || op == ">=")
          ty = TypeDesc::prim(FarTypeId::Bool);
        else if (op == "??") {
          try {
            ty = unifyTernaryBranches(lt, rt);
          } catch (const FarError& e) {
            throwAt(expr, e.what());
          }
        }
        else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>" || op == "?.")
          ty = TypeDesc::prim(FarTypeId::I64);
        else if (op == "//" &&
                 ((isPrimitiveDesc(lt) && isFloatType(lt.primitive)) ||
                  (isPrimitiveDesc(rt) && isFloatType(rt.primitive))))
          throw FarError("operator '" + op + "' requires integer operands");
        else if (op == "%" &&
                 ((isPrimitiveDesc(lt) && isFloatType(lt.primitive)) ||
                  (isPrimitiveDesc(rt) && isFloatType(rt.primitive))))
          ty = TypeDesc::prim(FarTypeId::F64);
        else if (isUserDesc(lt)) {
          const UserMethod* om = obj_reg_.lookupMethod(lt, userOpMethodName(op));
          if (om)
            ty = om->return_type;
          else
            ty = promoteNumeric(lt, rt);
        } else
          ty = promoteNumeric(lt, rt);
        break;
      }
      case Expr::AssignExprK: {
        if (expr.assign.target->kind != Expr::Variable && expr.assign.target->kind != Expr::IndexExpr &&
            expr.assign.target->kind != Expr::MemberExpr &&
            !(expr.assign.target->kind == Expr::PrefixExprK &&
              expr.assign.target->prefix.op == "*"))
          throw FarError("assignment requires variable, member, index, or *pointer target");
        checkExpr(*expr.assign.target);
        ty = checkExpr(*expr.assign.value);
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
          } else if (isIndexable(arr_ty) && !canAssignTypes(ty, elemTypeOf(arr_ty)))
            throwAt(expr, "index assignment type mismatch");
        }
        break;
      }
      case Expr::TernaryExprK: {
        checkExpr(*expr.ternary.cond);
        TypeDesc a = checkExpr(*expr.ternary.then_br);
        TypeDesc b = checkExpr(*expr.ternary.else_br);
        try {
          ty = unifyTernaryBranches(a, b);
        } catch (const FarError& e) {
          throwAt(expr, e.what());
        }
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
        ty = inner;
        break;
      }
      case Expr::TypeUnaryExprK:
        if (expr.type_unary.op == "stackalloc") {
          TypeDesc elem = expr.type_unary.has_type ? expr.type_unary.type_arg
                                                   : checkExpr(*expr.type_unary.value);
          if (expr.type_unary.has_type && expr.type_unary.value)
            checkExpr(*expr.type_unary.value);
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
        checkExpr(*expr.is_expr.value);
        ty = TypeDesc::prim(FarTypeId::Bool);
        break;
      }
      case Expr::AsExprK: {
        checkExpr(*expr.as_expr.value);
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
        } else if (isIndexable(arr_ty))
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
            size_t idx = static_cast<size_t>(std::stoll(expr.member.member.substr(1)));
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
        for (auto& a : expr.union_variant.args)
          checkExpr(*a);
        expr.union_variant.value = uv->value;
        ty = TypeDesc::user(td->name);
        break;
      }
      case Expr::MacroSubstExprK:
      case Expr::MacroInvokeExprK:
        throw FarError("unexpanded macro in typecheck");
      case Expr::ComptimeExprK:
        ++comptime_depth_;
        ty = checkExpr(*expr.comptime_expr.value);
        --comptime_depth_;
        break;
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
      for (size_t i = 0; i < expr.method_call.args.size(); ++i)
        checkExpr(*expr.method_call.args[i]);
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
        if (!canAssignTypes(at, TypeDesc::prim(sc)) &&
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
        for (const auto& a : call.args)
          checkExpr(*a.value);
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
        for (const auto& a : call.args)
          checkExpr(*a.value);
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
        if (call.args.size() != mono->fields.size())
          throw FarError(call.name + "() expects " + std::to_string(mono->fields.size()) + " argument(s)");
        for (size_t i = 0; i < call.args.size(); ++i) {
          TypeDesc at = checkExpr(*call.args[i].value);
          if (!canAssignTypes(at, mono->fields[i].type))
            throw FarError(call.name + "() argument type mismatch");
        }
        return userTypeDesc(ut->name, gargs);
      }
      if (ut->kind == UserTypeKind::Exception) {
        if (call.args.size() != ut->fields.size())
          throw FarError(call.name + "() expects " + std::to_string(ut->fields.size()) + " argument(s)");
        for (const auto& a : call.args)
          checkExpr(*a.value);
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
    for (const auto& a : call.args)
      checkExpr(*a.value);
    if (annotated.form == TypeForm::Dict && annotated.args.size() == 2)
      return annotated;
    if (annotated.form == TypeForm::List && annotated.args.size() == 1)
      return annotated;
    if (annotated.form == TypeForm::Set && annotated.args.size() == 1)
      return annotated;
    if (annotated.form == TypeForm::Queue && annotated.args.size() == 1)
      return annotated;
    if (annotated.form == TypeForm::Stack && annotated.args.size() == 1)
      return annotated;
    if (annotated.form == TypeForm::LinkedList && annotated.args.size() == 1)
      return annotated;
    if (call.name == "List")
      return TypeDesc::list(TypeDesc::prim(FarTypeId::I64));
    if (call.name == "Dict")
      return TypeDesc::dict(TypeDesc::prim(FarTypeId::I64), TypeDesc::prim(FarTypeId::I64));
    if (call.name == "Set")
      return TypeDesc::set(TypeDesc::prim(FarTypeId::I64));
    if (call.name == "Queue")
      return TypeDesc::queue(TypeDesc::prim(FarTypeId::I64));
    if (call.name == "Stack")
      return TypeDesc::stack(TypeDesc::prim(FarTypeId::I64));
    if (call.name == "LinkedList")
      return TypeDesc::linkedList(TypeDesc::prim(FarTypeId::I64));
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
