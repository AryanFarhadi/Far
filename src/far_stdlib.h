#pragma once

#include "ast.h"
#include "types.h"

#include <functional>
#include <sstream>
#include <string>

namespace far {

enum class StdlibTy { I64, F64, Str, Bool, Void };

struct StdlibFn {
  const char* name;
  const char* rt_name;
  StdlibTy ret;
  int nargs;
  StdlibTy args[4];
};

const StdlibFn* lookupStdlib(const std::string& name);
void declareStdlibRuntime(std::ostringstream& out);
bool checkStdlibArgs(const StdlibFn* fn, const std::vector<CallArg>& args,
                       const std::function<FarTypeId(Expr&)>& typecheck_expr);
TypeDesc stdlibRetType(const StdlibFn* fn);
std::string stdlibArgLlvm(StdlibTy ty);
std::string stdlibRetLlvm(StdlibTy ty);

// Built-in stdlib module sources (embedded in far_stdlib_modules.cpp).
const char* lookupStdlibModuleSource(const std::string& import_path);
bool isStdlibModuleImport(const std::string& import_path);
bool isStdlibFunctionModule(const std::string& flat_name);
bool isStdlibTypeModule(const std::string& flat_name);
std::string stdlibModuleFlatName(const std::string& module_full_name);

}  // namespace far
