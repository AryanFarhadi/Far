#pragma once

#include "ast.h"
#include "object_model.h"
#include "type_desc.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace far {

std::string userMangleTypeName(const std::string& base, const std::vector<TypeDesc>& type_args);
std::string userTypeKey(const TypeDesc& ty);

bool typeSatisfiesTrait(const TypeDesc& ty, const std::string& trait_name, const ObjectRegistry& reg);
void validateTypeConstraints(const std::vector<TypeParam>& params, const std::vector<TypeDesc>& type_args,
                             const ObjectRegistry& reg);

bool inferUserTypeArgs(const UserTypeDef& tmpl, const Call& call,
                       const std::function<TypeDesc(Expr&)>& type_of_expr, std::vector<TypeDesc>& out);

UserTypeDef instantiateUserType(const UserTypeDef& tmpl, const std::vector<TypeDesc>& type_args);

const UserTypeDef* findOrCreateUserMono(Program& program, ObjectRegistry& reg, const UserTypeDef& tmpl,
                                        const std::vector<TypeDesc>& type_args);

const UserTypeDef* resolveUserType(const TypeDesc& ty, ObjectRegistry& reg, Program& program);
TypeDesc userTypeDesc(const std::string& base, const std::vector<TypeDesc>& type_args);

}  // namespace far
