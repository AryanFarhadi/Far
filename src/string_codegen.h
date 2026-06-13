#pragma once

#include "ast.h"
#include "type_desc.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

struct StrCodegenCtx {
  std::ostringstream& out;
  std::function<std::string(Expr&)> emit_expr;
  std::function<std::string(const std::string&)> fresh;
};

std::string emitStrMethod(StrCodegenCtx ctx, const MethodCall& call, const std::string& recv_val);

}  // namespace far
