#pragma once

#include "ast.h"
#include "errors.h"

#include <functional>
#include <ostream>
#include <string>

namespace far {

struct ErrCodegenCtx {
  std::ostream& out;
  std::function<std::string(const std::string&)> fresh;
  std::function<std::string(const Expr&)> emit_expr;
};

std::string emitErrConstruct(ErrCodegenCtx ctx, const ErrConstructorInfo& ctor,
                             const std::vector<std::string>& arg_vals);
std::string emitErrMethod(ErrCodegenCtx ctx, const MethodCall& call, const TypeDesc& recv_ty,
                          const std::string& recv_val);
void emitThrow(ErrCodegenCtx ctx, int64_t type_tag, const std::string& value);
void emitTryEnter(ErrCodegenCtx ctx, const std::string& body_label);
void emitTrySuccess(ErrCodegenCtx ctx);
std::string emitCaughtValue(ErrCodegenCtx ctx);
std::string emitCaughtTag(ErrCodegenCtx ctx);
std::string emitCaughtMatches(ErrCodegenCtx ctx, int64_t expected_tag);
std::string emitCaughtValueGlobal(ErrCodegenCtx ctx);
std::string emitCaughtTagGlobal(ErrCodegenCtx ctx);
void emitCaughtBind(ErrCodegenCtx ctx, const std::string& catch_var);

}  // namespace far
