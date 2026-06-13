#pragma once

#include "ast.h"
#include "types.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

enum class IoTy { I64, F64, Str, Bool, Void };

struct IoFn {
  const char* name;
  const char* rt_name;
  IoTy ret;
  int nargs;
  IoTy args[4];
};

const IoFn* lookupIo(const std::string& name);
const IoFn* resolveIoCall(const std::string& name, int nargs);
void declareIoRuntime(std::ostringstream& out);
bool checkIoArgs(const IoFn* fn, const std::string& display, const std::vector<CallArg>& args,
                 const std::function<TypeDesc(Expr&)>& typecheck_expr);
TypeDesc ioRetType(const IoFn* fn);
std::string ioArgLlvm(IoTy ty);
std::string ioRetLlvm(IoTy ty);

}  // namespace far
