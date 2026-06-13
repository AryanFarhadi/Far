#pragma once

#include "ast.h"
#include "types.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

struct BuiltinInfo {
  const char* name;
  const char* rt_name;
  FarTypeId ret;
  int nargs;
  FarTypeId args[8];
};

const BuiltinInfo* lookupBuiltin(const std::string& name);

void declareBuiltins(std::ostringstream& out);

bool checkBuiltinArgs(const BuiltinInfo* builtin, const std::vector<CallArg>& args,
                      const std::function<FarTypeId(Expr&)>& typecheck_expr);

std::string builtinArgLlvm(FarTypeId id);
std::string builtinRetLlvm(const BuiltinInfo* b);

}  // namespace far
