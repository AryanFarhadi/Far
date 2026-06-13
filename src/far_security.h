#pragma once

#include "ast.h"
#include "types.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

enum class SecTy { I64, F64, Str, Bool, Void };

struct SecFn {
  const char* name;
  const char* rt_name;
  SecTy ret;
  int nargs;
  SecTy args[6];
};

const SecFn* lookupSec(const std::string& name);
void declareSecRuntime(std::ostringstream& out);
bool checkSecArgs(const SecFn* fn, const std::vector<CallArg>& args,
                  const std::function<TypeDesc(Expr&)>& typecheck_expr);
TypeDesc secRetType(const SecFn* fn);
std::string secArgLlvm(SecTy ty);
std::string secRetLlvm(SecTy ty);

}  // namespace far
