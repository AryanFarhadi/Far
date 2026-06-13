#pragma once

#include "ast.h"
#include "types.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

enum class NetTy { I64, F64, Str, Bool, Void };

struct NetFn {
  const char* name;
  const char* rt_name;
  NetTy ret;
  int nargs;
  NetTy args[6];
};

const NetFn* lookupNet(const std::string& name);
void declareNetRuntime(std::ostringstream& out);
bool checkNetArgs(const NetFn* fn, const std::vector<CallArg>& args,
                  const std::function<TypeDesc(Expr&)>& typecheck_expr);
TypeDesc netRetType(const NetFn* fn);
std::string netArgLlvm(NetTy ty);
std::string netRetLlvm(NetTy ty);

}  // namespace far
