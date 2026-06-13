#pragma once

#include "ast.h"
#include "types.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

enum class PerfTy { I64, F64, Str, Bool, Void };

struct PerfFn {
  const char* name;
  const char* rt_name;
  PerfTy ret;
  int nargs;
  PerfTy args[6];
};

const PerfFn* lookupPerf(const std::string& name);
void declarePerfRuntime(std::ostringstream& out);
bool checkPerfArgs(const PerfFn* fn, const std::vector<CallArg>& args,
                   const std::function<TypeDesc(Expr&)>& typecheck_expr);
TypeDesc perfRetType(const PerfFn* fn);
std::string perfArgLlvm(PerfTy ty);
std::string perfRetLlvm(PerfTy ty);

}  // namespace far
