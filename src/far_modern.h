#pragma once

#include "ast.h"
#include "types.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

enum class ModernTy { I64, F64, Str, Bool, Void };

struct ModernFn {
  const char* name;
  const char* rt_name;
  ModernTy ret;
  int nargs;
  ModernTy args[6];
};

const ModernFn* lookupModern(const std::string& name);
void declareModernRuntime(std::ostringstream& out);
bool checkModernArgs(const ModernFn* fn, const std::vector<CallArg>& args,
                     const std::function<TypeDesc(Expr&)>& typecheck_expr);
TypeDesc modernRetType(const ModernFn* fn);
std::string modernArgLlvm(ModernTy ty);
std::string modernRetLlvm(ModernTy ty);

}  // namespace far
