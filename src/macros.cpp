#include "macros.h"

#include "error.h"
#include "parser.h"

#include <memory>

namespace far {

static void expandStmtMacros(Stmt& stmt, const Program& program);

static std::unique_ptr<Expr> cloneExpr(const Expr& src);

static std::unique_ptr<Expr> substituteMacroExpr(const Expr& tmpl, const std::vector<std::string>& params,
                                                 const std::vector<std::unique_ptr<Expr>>& args) {
  switch (tmpl.kind) {
    case Expr::Int:
    case Expr::Float:
    case Expr::String:
    case Expr::Char:
    case Expr::Variable:
    case Expr::TypeConstExpr:
    case Expr::EnumVariantExprK:
    case Expr::UnionVariantExprK:
      return cloneExpr(tmpl);
    case Expr::MacroSubstExprK: {
      for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == tmpl.macro_subst.param)
          return cloneExpr(*args[i]);
      }
      throw FarError("unknown macro parameter $" + tmpl.macro_subst.param);
    }
    case Expr::MacroInvokeExprK: {
      std::vector<std::unique_ptr<Expr>> nargs;
      for (const auto& a : tmpl.macro_invoke.args)
        nargs.push_back(substituteMacroExpr(*a, params, args));
      auto e = std::make_unique<Expr>();
      e->kind = Expr::MacroInvokeExprK;
      e->macro_invoke.name = tmpl.macro_invoke.name;
      e->macro_invoke.args = std::move(nargs);
      return e;
    }
    case Expr::Binary: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Binary;
      e->bin_op.op = tmpl.bin_op.op;
      e->bin_op.left = substituteMacroExpr(*tmpl.bin_op.left, params, args);
      e->bin_op.right = substituteMacroExpr(*tmpl.bin_op.right, params, args);
      return e;
    }
    case Expr::PrefixExprK: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::PrefixExprK;
      e->prefix.op = tmpl.prefix.op;
      e->prefix.operand = substituteMacroExpr(*tmpl.prefix.operand, params, args);
      return e;
    }
    case Expr::FnCall: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::FnCall;
      e->call.name = tmpl.call.name;
      e->call.type_args = tmpl.call.type_args;
      for (const auto& a : tmpl.call.args) {
        CallArg ca;
        ca.name = a.name;
        ca.value = substituteMacroExpr(*a.value, params, args);
        e->call.args.push_back(std::move(ca));
      }
      return e;
    }
    case Expr::ComptimeExprK: {
      if (tmpl.comptime_expr.is_block)
        throw FarError("comptime block expressions are not supported in macros");
      auto e = std::make_unique<Expr>();
      e->kind = Expr::ComptimeExprK;
      e->comptime_expr.value = substituteMacroExpr(*tmpl.comptime_expr.value, params, args);
      return e;
    }
    default:
      throw FarError("macro body contains unsupported expression");
  }
}

static std::unique_ptr<Expr> cloneExpr(const Expr& src) {
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
    case Expr::String:
      return Expr::makeString(src.string_lit.value);
    case Expr::Char:
      return Expr::makeChar(src.char_lit.value);
    case Expr::Variable:
      return Expr::makeVar(src.var.name);
    case Expr::MacroSubstExprK: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::MacroSubstExprK;
      e->macro_subst.param = src.macro_subst.param;
      return e;
    }
    case Expr::MacroInvokeExprK: {
      std::vector<std::unique_ptr<Expr>> args;
      for (const auto& a : src.macro_invoke.args)
        args.push_back(cloneExpr(*a));
      auto e = std::make_unique<Expr>();
      e->kind = Expr::MacroInvokeExprK;
      e->macro_invoke.name = src.macro_invoke.name;
      e->macro_invoke.args = std::move(args);
      return e;
    }
    case Expr::Binary: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Binary;
      e->bin_op.op = src.bin_op.op;
      e->bin_op.left = cloneExpr(*src.bin_op.left);
      e->bin_op.right = cloneExpr(*src.bin_op.right);
      return e;
    }
    case Expr::PrefixExprK: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::PrefixExprK;
      e->prefix.op = src.prefix.op;
      e->prefix.operand = cloneExpr(*src.prefix.operand);
      return e;
    }
    case Expr::FnCall: {
      std::vector<CallArg> args;
      for (const auto& a : src.call.args) {
        CallArg ca;
        ca.name = a.name;
        ca.value = cloneExpr(*a.value);
        args.push_back(std::move(ca));
      }
      return Expr::makeCallArgs(src.call.name, std::move(args), src.type, src.call.type_args);
    }
    case Expr::ComptimeExprK: {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::ComptimeExprK;
      e->comptime_expr.is_block = src.comptime_expr.is_block;
      if (src.comptime_expr.is_block)
        throw FarError("comptime block clone not implemented");
      e->comptime_expr.value = cloneExpr(*src.comptime_expr.value);
      return e;
    }
    default:
      throw FarError("cannot clone expression for macro expansion");
  }
}

static void expandExprMacros(Expr& expr, const Program& program) {
  static thread_local int macro_depth = 0;
  struct MacroDepthGuard {
    MacroDepthGuard() {
      if (++macro_depth > 64)
        throw FarError("macro expansion depth exceeded");
    }
    ~MacroDepthGuard() { --macro_depth; }
  };
  switch (expr.kind) {
    case Expr::MacroInvokeExprK: {
      MacroDepthGuard guard;
      const MacroDef* macro = nullptr;
      for (const auto& m : program.macros) {
        if (m.name == expr.macro_invoke.name) {
          macro = &m;
          break;
        }
      }
      if (!macro)
        throw FarError("undefined macro '" + expr.macro_invoke.name + "'");
      if (macro->params.size() != expr.macro_invoke.args.size())
        throw FarError("macro '" + macro->name + "' argument count mismatch");
      std::unique_ptr<Expr> expanded = substituteMacroExpr(*macro->body, macro->params, expr.macro_invoke.args);
      expr = std::move(*expanded);
      expandExprMacros(expr, program);
      return;
    }
    case Expr::Binary:
      expandExprMacros(*expr.bin_op.left, program);
      expandExprMacros(*expr.bin_op.right, program);
      return;
    case Expr::PrefixExprK:
      expandExprMacros(*expr.prefix.operand, program);
      return;
    case Expr::ComptimeExprK:
      if (expr.comptime_expr.is_block) {
        for (auto& s : expr.comptime_expr.block)
          expandStmtMacros(*s, program);
      } else {
        expandExprMacros(*expr.comptime_expr.value, program);
      }
      return;
    case Expr::FnCall:
      for (auto& a : expr.call.args)
        expandExprMacros(*a.value, program);
      return;
    case Expr::CastExpr:
      expandExprMacros(*expr.cast.value, program);
      return;
    case Expr::TernaryExprK:
      expandExprMacros(*expr.ternary.cond, program);
      expandExprMacros(*expr.ternary.then_br, program);
      expandExprMacros(*expr.ternary.else_br, program);
      return;
    case Expr::MemberExpr:
      expandExprMacros(*expr.member.object, program);
      return;
    case Expr::MethodExpr:
      expandExprMacros(*expr.method_call.object, program);
      for (auto& a : expr.method_call.args)
        expandExprMacros(*a, program);
      return;
    case Expr::ArrayLitExpr:
      for (auto& el : expr.array_lit.elements)
        expandExprMacros(*el, program);
      return;
    case Expr::DictLitExpr:
      for (auto& entry : expr.dict_lit.entries) {
        expandExprMacros(*entry.key, program);
        expandExprMacros(*entry.value, program);
      }
      return;
    default:
      return;
  }
}

static void expandStmtMacros(Stmt& stmt, const Program& program) {
  switch (stmt.kind) {
    case Stmt::LetStmt:
      if (stmt.let.value)
        expandExprMacros(*stmt.let.value, program);
      return;
    case Stmt::ReturnStmt:
      if (stmt.ret.has_value)
        expandExprMacros(*stmt.ret.value, program);
      return;
    case Stmt::ExprStmtK:
      expandExprMacros(*stmt.expr_stmt.expr, program);
      return;
    case Stmt::PrintStmt:
      expandExprMacros(*stmt.print.value, program);
      return;
    case Stmt::IfStmt:
      for (auto& c : stmt.if_stmt.clauses) {
        expandExprMacros(*c.condition, program);
        for (auto& s : c.body)
          expandStmtMacros(*s, program);
      }
      for (auto& s : stmt.if_stmt.else_body)
        expandStmtMacros(*s, program);
      return;
    case Stmt::WhileStmt:
      expandExprMacros(*stmt.while_stmt.condition, program);
      for (auto& s : stmt.while_stmt.body)
        expandStmtMacros(*s, program);
      return;
    case Stmt::ForStmt:
      if (stmt.for_stmt.is_parallel || stmt.for_stmt.is_range) {
        if (stmt.for_stmt.range_start)
          expandExprMacros(*stmt.for_stmt.range_start, program);
        if (stmt.for_stmt.range_end)
          expandExprMacros(*stmt.for_stmt.range_end, program);
      } else if (stmt.for_stmt.is_foreach) {
        expandExprMacros(*stmt.for_stmt.foreach_iter, program);
      }
      if (stmt.for_stmt.init)
        expandStmtMacros(*stmt.for_stmt.init, program);
      if (stmt.for_stmt.cond)
        expandExprMacros(*stmt.for_stmt.cond, program);
      if (stmt.for_stmt.step)
        expandStmtMacros(*stmt.for_stmt.step, program);
      for (auto& s : stmt.for_stmt.body)
        expandStmtMacros(*s, program);
      return;
    case Stmt::TryStmtK:
      for (auto& s : stmt.try_stmt.try_body)
        expandStmtMacros(*s, program);
      for (auto& s : stmt.try_stmt.catch_body)
        expandStmtMacros(*s, program);
      for (auto& s : stmt.try_stmt.finally_body)
        expandStmtMacros(*s, program);
      return;
    case Stmt::ThrowStmtK:
      expandExprMacros(*stmt.throw_stmt.value, program);
      return;
    case Stmt::DeferStmtK:
      expandExprMacros(*stmt.defer.expr, program);
      return;
    case Stmt::UnsafeStmtK:
      for (auto& s : stmt.unsafe.body)
        expandStmtMacros(*s, program);
      return;
    case Stmt::MatchStmtK:
      expandExprMacros(*stmt.match_stmt.scrutinee, program);
      for (auto& arm : stmt.match_stmt.arms) {
        for (auto& s : arm.body)
          expandStmtMacros(*s, program);
      }
      return;
    default:
      return;
  }
}

void expandMacros(Program& program) {
  for (auto& fn : program.functions) {
    for (auto& st : fn.body)
      expandStmtMacros(*st, program);
  }
  for (auto& fn : program.synthetic_functions) {
    for (auto& st : fn.body)
      expandStmtMacros(*st, program);
  }
}

}  // namespace far
