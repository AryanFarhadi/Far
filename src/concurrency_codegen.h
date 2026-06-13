#pragma once

#include "ast.h"
#include "concurrency.h"

#include <functional>
#include <ostream>
#include <string>

namespace far {

struct ConcCodegenCtx {
  std::ostream& out;
  std::function<std::string(const std::string&)> fresh;
  std::function<std::string(const Expr&)> emit_expr;
  std::function<TypeDesc(const Expr&)> expr_type;
  std::function<std::string(const std::string&, const Function*)> fn_ptr;
};

std::string emitConcConstruct(ConcCodegenCtx ctx, TypeForm form, const TypeDesc& elem_ty,
                            const std::vector<std::string>& arg_vals);
std::string emitConcMethod(ConcCodegenCtx ctx, const MethodCall& call, const TypeDesc& recv_ty,
                           const std::string& recv_val);
void emitConcDrop(ConcCodegenCtx ctx, TypeForm form, const std::string& handle);
std::string emitParallelFor(ConcCodegenCtx ctx, const std::string& fn_sym, const std::string& start,
                            const std::string& end);
std::string emitActorMethod(ConcCodegenCtx ctx, const MethodCall& call, const std::string& recv_val);
std::string emitActorConstruct(ConcCodegenCtx ctx, const std::string& handler_sym, const std::string& init_val);

}  // namespace far
