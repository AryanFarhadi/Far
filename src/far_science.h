#pragma once

#include "ast.h"
#include "types.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

enum class ScienceTy { I64, F64, Arr, Void };

struct ScienceFn {
  const char* name;
  const char* rt_name;
  ScienceTy ret;
  int nargs;
  ScienceTy args[6];
};

const ScienceFn* lookupScience(const std::string& name);
void declareScienceRuntime(std::ostringstream& out);
bool checkScienceArgs(const ScienceFn* fn, const std::vector<CallArg>& args,
                      const std::function<TypeDesc(Expr&)>& typecheck_expr);
TypeDesc scienceRetType(const ScienceFn* fn);
std::string scienceArgLlvm(ScienceTy ty);
std::string scienceRetLlvm(ScienceTy ty);

}  // namespace far
