#pragma once

#include "ast.h"
#include "object_model.h"
#include "type_desc.h"

#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace far {

struct PatBind {
  std::string name;
  std::string value;
  TypeDesc type;
};

struct PatCodegenCtx {
  std::ostream& out;
  std::function<std::string(const std::string&)> fresh;
  std::function<std::string(const Expr&)> emit_expr;
  const ObjectRegistry* obj_reg = nullptr;
};

struct PatTestResult {
  std::string cond;
  bool always = false;
  std::vector<PatBind> binds;
};

PatTestResult emitPatternTest(PatCodegenCtx ctx, const Pattern& pat, const std::string& scrut_val,
                              const TypeDesc& scrut_ty);

std::string emitUnionConstruct(PatCodegenCtx ctx, const UnionVariantExpr& uv,
                               const std::vector<std::string>& arg_vals);

}  // namespace far
