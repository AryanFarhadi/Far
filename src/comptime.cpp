#include "comptime.h"

#include "error.h"
#include "macros.h"
#include "type_desc.h"

#include <functional>
#include <limits>

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
  if (isUserDesc(td) && ctx.obj_reg) {
    const UserTypeDef* ut = ctx.obj_reg->lookup(td.user_name);
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

static ComptimeValue evalFunctionBody(ComptimeContext& ctx, const Function& fn,
                                      const std::vector<ComptimeValue>& args) {
  if (ctx.depth > 32)
    throw FarError("comptime evaluation depth exceeded");
  ComptimeContext local = ctx;
  local.depth = ctx.depth + 1;
  for (size_t i = 0; i < fn.params.size(); ++i) {
    if (i < args.size())
      local.vars[fn.params[i].name] = args[i];
  }
  for (const auto& stmt : fn.body) {
    if (stmt->kind == Stmt::ReturnStmt) {
      if (!stmt->ret.has_value)
        throw FarError("comptime function must return a value");
      return evalExpr(local, *stmt->ret.value);
    }
    if (stmt->kind == Stmt::LetStmt) {
      ComptimeValue v = evalExpr(local, *stmt->let.value);
      local.vars[stmt->let.name] = v;
      continue;
    }
    if (stmt->kind == Stmt::IfStmt) {
      for (const auto& c : stmt->if_stmt.clauses) {
        ComptimeValue cond = evalExpr(local, *c.condition);
        if (cond.i64 != 0) {
          for (const auto& s : c.body) {
            if (s->kind == Stmt::ReturnStmt && s->ret.has_value)
              return evalExpr(local, *s->ret.value);
          }
          break;
        }
      }
      continue;
    }
    throw FarError("unsupported statement in comptime function");
  }
  throw FarError("comptime function fell through without return");
}

bool tryEvalExpr(ComptimeContext& ctx, const Expr& expr, ComptimeValue& out) {
  try {
    out = evalExpr(ctx, expr);
    return true;
  } catch (...) {
    return false;
  }
}

ComptimeValue evalExpr(ComptimeContext& ctx, const Expr& expr) {
  switch (expr.kind) {
    case Expr::Int:
      return makeIntVal(expr.int_lit.value);
    case Expr::Variable: {
      auto it = ctx.vars.find(expr.var.name);
      if (it == ctx.vars.end())
        throw FarError("unknown comptime variable '" + expr.var.name + "'");
      return it->second;
    }
    case Expr::Binary: {
      ComptimeValue l = evalExpr(ctx, *expr.bin_op.left);
      ComptimeValue r = evalExpr(ctx, *expr.bin_op.right);
      const std::string& op = expr.bin_op.op;
      if (op == "==") {
        if (l.kind == ComptimeValue::Kind::String && r.kind == ComptimeValue::Kind::String)
          return makeIntVal(l.str == r.str ? 1 : 0);
        return makeIntVal(l.i64 == r.i64 ? 1 : 0);
      }
      if (op == "!=") {
        if (l.kind == ComptimeValue::Kind::String && r.kind == ComptimeValue::Kind::String)
          return makeIntVal(l.str != r.str ? 1 : 0);
        return makeIntVal(l.i64 != r.i64 ? 1 : 0);
      }
      if (op == "+")
        return makeIntVal(l.i64 + r.i64);
      if (op == "-")
        return makeIntVal(l.i64 - r.i64);
      if (op == "*")
        return makeIntVal(l.i64 * r.i64);
      if (op == "/") {
        if (r.i64 == 0)
          throw FarError("comptime division by zero");
        return makeIntVal(l.i64 / r.i64);
      }
      if (op == "%") {
        if (r.i64 == 0)
          throw FarError("comptime modulo by zero");
        return makeIntVal(l.i64 % r.i64);
      }
      if (op == "<")
        return makeIntVal(l.i64 < r.i64 ? 1 : 0);
      if (op == ">")
        return makeIntVal(l.i64 > r.i64 ? 1 : 0);
      if (op == "<=")
        return makeIntVal(l.i64 <= r.i64 ? 1 : 0);
      if (op == ">=")
        return makeIntVal(l.i64 >= r.i64 ? 1 : 0);
      if (op == "&&")
        return makeIntVal((l.i64 != 0 && r.i64 != 0) ? 1 : 0);
      if (op == "||")
        return makeIntVal((l.i64 != 0 || r.i64 != 0) ? 1 : 0);
      if (op == "===")
        return makeIntVal(l.i64 == r.i64 ? 1 : 0);
      if (op == "!==")
        return makeIntVal(l.i64 != r.i64 ? 1 : 0);
      throw FarError("unsupported comptime binary op '" + op + "'");
    }
    case Expr::PrefixExprK: {
      ComptimeValue v = evalExpr(ctx, *expr.prefix.operand);
      if (expr.prefix.op == "-")
        return makeIntVal(-v.i64);
      if (expr.prefix.op == "!")
        return makeIntVal(v.i64 == 0 ? 1 : 0);
      throw FarError("unsupported comptime prefix op");
    }
    case Expr::ComptimeExprK:
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
  for (auto& a : td.args)
    resolveComptimeTypes(a, ctx);
}

static bool isBoolProducingOp(const std::string& op) {
  return op == "==" || op == "!=" || op == "===" || op == "!==" || op == "<" || op == ">" ||
         op == "<=" || op == ">=" || op == "and" || op == "or" || op == "&&" || op == "||" ||
         op == "in" || op == "not in";
}

static void replaceWithInt(Expr& expr, int64_t v) {
  TypeDesc ty = expr.type;
  if (expr.kind == Expr::Binary && isBoolProducingOp(expr.bin_op.op))
    ty = TypeDesc::prim(FarTypeId::Bool);
  else if (expr.kind == Expr::PrefixExprK && expr.prefix.op == "!")
    ty = TypeDesc::prim(FarTypeId::Bool);
  else if (expr.kind == Expr::IsExprK)
    ty = TypeDesc::prim(FarTypeId::Bool);
  expr = Expr();
  expr.kind = Expr::Int;
  expr.type = ty;
  expr.int_lit.value = v;
}

static void foldExprComptime(Expr& expr, ComptimeContext& ctx) {
  if (expr.kind == Expr::ComptimeExprK) {
    ComptimeValue v = evalExpr(ctx, *expr.comptime_expr.value);
    replaceWithInt(expr, v.i64);
    return;
  }
  if (expr.kind == Expr::Binary) {
    foldExprComptime(*expr.bin_op.left, ctx);
    foldExprComptime(*expr.bin_op.right, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithInt(expr, v.i64);
    return;
  }
  if (expr.kind == Expr::PrefixExprK) {
    if (expr.prefix.op == "++" || expr.prefix.op == "--")
      return;
    foldExprComptime(*expr.prefix.operand, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithInt(expr, v.i64);
    return;
  }
  if (expr.kind == Expr::FnCall) {
    for (auto& a : expr.call.args)
      foldExprComptime(*a.value, ctx);
    ComptimeValue v;
    if (tryEvalExpr(ctx, expr, v))
      replaceWithInt(expr, v.i64);
    return;
  }
}

static void foldStmtComptime(Stmt& stmt, ComptimeContext& ctx) {
  switch (stmt.kind) {
    case Stmt::LetStmt:
      if (stmt.let.value)
        foldExprComptime(*stmt.let.value, ctx);
      if (stmt.let.is_constexpr && stmt.let.value) {
        ComptimeValue v = evalExpr(ctx, *stmt.let.value);
        ctx.vars[stmt.let.name] = v;
      }
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
    case Stmt::IfStmt:
      for (auto& c : stmt.if_stmt.clauses) {
        foldExprComptime(*c.condition, ctx);
        for (auto& s : c.body)
          foldStmtComptime(*s, ctx);
      }
      for (auto& s : stmt.if_stmt.else_body)
        foldStmtComptime(*s, ctx);
      return;
    case Stmt::WhileStmt:
      foldExprComptime(*stmt.while_stmt.condition, ctx);
      for (auto& s : stmt.while_stmt.body)
        foldStmtComptime(*s, ctx);
      return;
    case Stmt::ForStmt:
      if (stmt.for_stmt.is_parallel || stmt.for_stmt.is_range) {
        if (stmt.for_stmt.range_start)
          foldExprComptime(*stmt.for_stmt.range_start, ctx);
        if (stmt.for_stmt.range_end)
          foldExprComptime(*stmt.for_stmt.range_end, ctx);
      } else if (stmt.for_stmt.is_foreach) {
        foldExprComptime(*stmt.for_stmt.foreach_iter, ctx);
      }
      if (stmt.for_stmt.init)
        foldStmtComptime(*stmt.for_stmt.init, ctx);
      if (stmt.for_stmt.cond)
        foldExprComptime(*stmt.for_stmt.cond, ctx);
      if (stmt.for_stmt.step)
        foldStmtComptime(*stmt.for_stmt.step, ctx);
      for (auto& s : stmt.for_stmt.body)
        foldStmtComptime(*s, ctx);
      return;
    case Stmt::TryStmtK:
      for (auto& s : stmt.try_stmt.try_body)
        foldStmtComptime(*s, ctx);
      for (auto& s : stmt.try_stmt.catch_body)
        foldStmtComptime(*s, ctx);
      for (auto& s : stmt.try_stmt.finally_body)
        foldStmtComptime(*s, ctx);
      return;
    case Stmt::ThrowStmtK:
      foldExprComptime(*stmt.throw_stmt.value, ctx);
      return;
    case Stmt::DeferStmtK:
      foldExprComptime(*stmt.defer.expr, ctx);
      return;
    case Stmt::UnsafeStmtK:
      for (auto& s : stmt.unsafe.body)
        foldStmtComptime(*s, ctx);
      return;
    case Stmt::MatchStmtK:
      foldExprComptime(*stmt.match_stmt.scrutinee, ctx);
      for (auto& arm : stmt.match_stmt.arms) {
        for (auto& st : arm.body)
          foldStmtComptime(*st, ctx);
      }
      return;
    default:
      return;
  }
}

static void runComptimeStmts(const std::vector<std::unique_ptr<Stmt>>& stmts, ComptimeContext& ctx) {
  for (const auto& stmt : stmts) {
    if (stmt->kind == Stmt::LetStmt && stmt->let.is_constexpr) {
      ComptimeValue v = evalExpr(ctx, *stmt->let.value);
      ctx.vars[stmt->let.name] = v;
      continue;
    }
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
  ret->ret.value = Expr::makeInt(result.i64);
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
    else if (stmt->kind == Stmt::LetStmt && stmt->let.is_constexpr && stmt->let.value) {
      ComptimeValue v = evalExpr(ctx, *stmt->let.value);
      ctx.vars[stmt->let.name] = v;
    }
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
  for (auto& fn : program.functions) {
    for (auto& stmt : fn.body)
      foldStmtComptime(*stmt, ctx);
  }
}

}  // namespace far
