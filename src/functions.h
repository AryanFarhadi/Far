#pragma once

#include "ast.h"
#include "object_model.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace far {

struct BindResult {
  bool ok = false;
  std::string error;
  std::vector<Expr*> args;
  std::vector<Expr*> variadic_args;
  std::vector<std::unique_ptr<Expr>> owned;
};

struct BoundCall {
  const Function* fn = nullptr;
  std::string llvm_name;
  BindResult bound;
};

std::string mangleType(const TypeDesc& td);
std::string mangleFunction(const Function& fn);
std::string specializeGenericName(const std::string& base, const std::vector<TypeDesc>& type_args);

bool inferGenericArgs(const Function& tmpl, const Call& call,
                      const std::function<TypeDesc(Expr&)>& type_of_expr, std::vector<TypeDesc>& out);

Function instantiateGeneric(const Function& tmpl, const std::vector<TypeDesc>& type_args);

std::string genericInstanceKey(const std::string& name, const std::vector<TypeDesc>& type_args);

BoundCall resolveCall(const std::string& name, Call& call,
                      std::unordered_map<std::string, std::vector<const Function*>>& overloads,
                      const std::function<TypeDesc(Expr&)>& type_of_expr, Program& program,
                      ObjectRegistry* reg = nullptr);

void registerFunctions(const Program& program,
                       std::unordered_map<std::string, std::vector<const Function*>>& out);

}  // namespace far
