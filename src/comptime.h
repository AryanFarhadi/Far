#pragma once

#include "ast.h"
#include "object_model.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace far {

struct ComptimeValue {
  enum class Kind { Int, Float, Bool, String, Dict, Array } kind = Kind::Int;
  int64_t i64 = 0;
  double f64 = 0.0;
  bool b = false;
  std::string str;
  std::vector<ComptimeValue> dict_keys;
  std::vector<ComptimeValue> dict_values;
  std::vector<ComptimeValue> array_elems;
};

struct ComptimeContext {
  Program* program = nullptr;
  ObjectRegistry* obj_reg = nullptr;
  std::unordered_map<std::string, ComptimeValue> vars;
  std::unordered_map<std::string, const Function*> fns;
  bool in_comptime = true;
  int depth = 0;
  int64_t loop_iters = 0;
};

bool tryEvalExpr(ComptimeContext& ctx, const Expr& expr, ComptimeValue& out);
ComptimeValue evalExpr(ComptimeContext& ctx, const Expr& expr);
int64_t reflectNameHash(const std::string& name);
void resolveComptimeTypes(TypeDesc& td, ComptimeContext& ctx);
void foldProgramExpressions(Program& program);
void prepareProgram(Program& program);

}  // namespace far
